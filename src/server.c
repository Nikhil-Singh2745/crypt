/*
 * server.c — raw POSIX-socket HTTP/1.1 server.
 *
 * we accept(), spawn a detached thread per connection, read up to 8KB of
 * request line + headers, and dispatch. SSE handlers keep the socket open
 * and write events until the client goes away. everyone else gets a
 * one-shot response and we close the fd.
 *
 * yes, this would scale terribly under load. it's a roguelike. you and your
 * 30 friends will be fine.
 */

#define _GNU_SOURCE
#include "server.h"
#include "router.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define REQ_BUF 8192

/* ---- low-level write helpers ----------------------------------------- */

int write_all(int fd, const char *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t k = write(fd, buf + off, n - off);
        if (k < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)k;
    }
    return 0;
}

int write_status_line(int fd, int code, const char *reason) {
    char buf[128];
    int n = snprintf(buf, sizeof buf, "HTTP/1.1 %d %s\r\n", code, reason);
    return write_all(fd, buf, (size_t)n);
}

int write_header(int fd, const char *k, const char *v) {
    char buf[1024];
    int n = snprintf(buf, sizeof buf, "%s: %s\r\n", k, v);
    if (n < 0 || n >= (int)sizeof buf) return -1;
    return write_all(fd, buf, (size_t)n);
}

int write_headerf(int fd, const char *k, const char *fmt, ...) {
    char val[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(val, sizeof val, fmt, ap);
    va_end(ap);
    if (n < 0) return -1;
    return write_header(fd, k, val);
}

int end_headers(int fd) {
    return write_all(fd, "\r\n", 2);
}

int respond_simple(int fd, int code, const char *reason,
                   const char *content_type,
                   const char *body, size_t body_len,
                   const char *extra_header) {
    if (write_status_line(fd, code, reason) < 0) return -1;
    if (content_type && write_header(fd, "Content-Type", content_type) < 0) return -1;
    if (write_headerf(fd, "Content-Length", "%zu", body_len) < 0) return -1;
    if (write_header(fd, "Connection", "close") < 0) return -1;
    if (extra_header && write_all(fd, extra_header, strlen(extra_header)) < 0) return -1;
    if (end_headers(fd) < 0) return -1;
    if (body && body_len > 0) return write_all(fd, body, body_len);
    return 0;
}

/* ---- SSE helpers ----------------------------------------------------- */

int sse_send_headers(int fd, const char *set_cookie_or_null) {
    if (write_status_line(fd, 200, "OK") < 0) return -1;
    if (write_header(fd, "Content-Type", "text/event-stream") < 0) return -1;
    if (write_header(fd, "Cache-Control", "no-cache, no-transform") < 0) return -1;
    if (write_header(fd, "Connection", "keep-alive") < 0) return -1;
    if (write_header(fd, "X-Accel-Buffering", "no") < 0) return -1;
    if (set_cookie_or_null && write_all(fd, set_cookie_or_null, strlen(set_cookie_or_null)) < 0) return -1;
    if (end_headers(fd) < 0) return -1;
    /* a leading retry hint + a comment ping — keeps proxies from buffering */
    return write_all(fd, "retry: 2000\n: hi\n\n", 18);
}

int sse_send_frame(int fd, const char *html) {
    /* we wrap the entire frame in one event by emitting one `data: <line>`
     * per line, then a blank line. the EventSource concatenates them with
     * '\n' which is exactly what we want inside a <pre>. */
    if (!html) html = "";
    const char *p = html;
    while (1) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (write_all(fd, "data: ", 6) < 0) return -1;
        if (len && write_all(fd, p, len) < 0) return -1;
        if (write_all(fd, "\n", 1) < 0) return -1;
        if (!nl) break;
        p = nl + 1;
    }
    return write_all(fd, "\n", 1);
}

/* ---- request parser -------------------------------------------------- */

static int read_until_headers_end(int fd, char *buf, size_t cap, size_t *out_len) {
    size_t off = 0;
    while (off < cap - 1) {
        ssize_t k = read(fd, buf + off, cap - 1 - off);
        if (k < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (k == 0) break;
        off += (size_t)k;
        buf[off] = '\0';
        if (strstr(buf, "\r\n\r\n")) break;
    }
    *out_len = off;
    return 0;
}

/* case-insensitive prefix match against a header name like "Cookie:" */
static int header_match(const char *line, const char *name) {
    size_t n = strlen(name);
    for (size_t i = 0; i < n; i++) {
        char a = line[i], b = name[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) return 0;
    }
    return line[n] == ':';
}

static void copy_trimmed(char *dst, size_t cap, const char *src) {
    while (*src == ' ' || *src == '\t') src++;
    size_t i = 0;
    while (*src && *src != '\r' && *src != '\n' && i + 1 < cap) {
        dst[i++] = *src++;
    }
    dst[i] = '\0';
}

int http_read_request(int fd, Request *r) {
    char buf[REQ_BUF];
    size_t len = 0;
    memset(r, 0, sizeof *r);
    if (read_until_headers_end(fd, buf, sizeof buf, &len) < 0) return -1;
    if (len == 0) return -1;

    /* request line: METHOD SP URI SP VERSION CRLF */
    char *sp1 = strchr(buf, ' ');
    if (!sp1) return -1;
    char *sp2 = strchr(sp1 + 1, ' ');
    if (!sp2) return -1;
    size_t mlen = (size_t)(sp1 - buf);
    if (mlen >= sizeof r->method) mlen = sizeof r->method - 1;
    memcpy(r->method, buf, mlen);
    r->method[mlen] = '\0';

    char uri[768];
    size_t ulen = (size_t)(sp2 - (sp1 + 1));
    if (ulen >= sizeof uri) ulen = sizeof uri - 1;
    memcpy(uri, sp1 + 1, ulen);
    uri[ulen] = '\0';

    /* split path / query */
    char *q = strchr(uri, '?');
    if (q) {
        *q = '\0';
        copy_trimmed(r->query, sizeof r->query, q + 1);
    }
    copy_trimmed(r->path, sizeof r->path, uri);

    /* walk headers */
    char *line = strstr(buf, "\r\n");
    if (!line) return 0;
    line += 2;
    while (line < buf + len) {
        if (line[0] == '\r' && line[1] == '\n') break;
        if (header_match(line, "Cookie")) {
            const char *v = strchr(line, ':');
            if (v) copy_trimmed(r->cookie, sizeof r->cookie, v + 1);
        } else if (header_match(line, "User-Agent")) {
            const char *v = strchr(line, ':');
            if (v) copy_trimmed(r->ua, sizeof r->ua, v + 1);
        }
        char *nl = strstr(line, "\r\n");
        if (!nl) break;
        line = nl + 2;
    }
    return 0;
}

/* ---- accept loop ----------------------------------------------------- */

static void *conn_thread(void *arg) {
    int fd = (int)(intptr_t)arg;

    /* SSE benefits from low-latency writes */
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);

    Request r;
    if (http_read_request(fd, &r) == 0 && r.method[0] && r.path[0]) {
        router_dispatch(fd, &r);
    }
    /* router may have already closed-on-error; double close is harmless-ish
     * (EBADF) so we just ignore the error. */
    close(fd);
    return NULL;
}

int server_start(int port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); return 1; }

    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(s, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("bind"); return 1;
    }
    if (listen(s, 64) < 0) { perror("listen"); return 1; }

    while (1) {
        struct sockaddr_in cli;
        socklen_t cl = sizeof cli;
        int fd = accept(s, (struct sockaddr *)&cli, &cl);
        if (fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }
        pthread_t tid;
        pthread_attr_t at;
        pthread_attr_init(&at);
        pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&tid, &at, conn_thread, (void *)(intptr_t)fd) != 0) {
            close(fd);
        }
        pthread_attr_destroy(&at);
    }
    /* unreachable */
}
