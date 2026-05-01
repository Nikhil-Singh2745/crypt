#ifndef CRYPT_SERVER_H
#define CRYPT_SERVER_H

#include <stddef.h>

/*
 * a request as we (barely) parse it.
 * we read at most 8KB of headers and ignore the body. it's all GETs anyway.
 */
typedef struct {
    char method[8];
    char path[256];
    char query[512];
    char cookie[256];   /* raw value of the Cookie: header */
    char ua[128];
} Request;

int  server_start(int port);

/* low-level helpers used by handlers. all of them set errno on failure
 * but otherwise we return -1 and the caller closes the fd. */
int  http_read_request(int fd, Request *r);
int  write_all(int fd, const char *buf, size_t n);
int  write_status_line(int fd, int code, const char *reason);
int  write_header(int fd, const char *k, const char *v);
int  write_headerf(int fd, const char *k, const char *fmt, ...);
int  end_headers(int fd);

/* convenience: complete simple response. body may be NULL. */
int  respond_simple(int fd, int code, const char *reason,
                    const char *content_type,
                    const char *body, size_t body_len,
                    const char *extra_header);

/* SSE helpers: send `data: <line>\n` for each line in `html`,
 * then a blank line to end the event. returns -1 on write failure. */
int  sse_send_frame(int fd, const char *html);
int  sse_send_headers(int fd, const char *set_cookie_or_null);

#endif
