/*
 * crypt.c — main()
 *
 * a turn-based ASCII roguelike running entirely inside an HTTP server
 * we wrote against POSIX sockets. yes really.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "server.h"
#include "session.h"

static void usage(const char *argv0) {
    fprintf(stderr,
        "crypt.c — a roguelike running inside an HTTP server in C\n"
        "usage: %s [--port N]\n"
        "  PORT env var also works. defaults to 8080.\n",
        argv0);
}

int main(int argc, char **argv) {
    int port = 8080;

    const char *env_port = getenv("PORT");
    if (env_port && *env_port) port = atoi(env_port);

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--port") && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "crypt: unknown arg '%s'\n", argv[i]);
            usage(argv[0]);
            return 2;
        }
    }

    /* if a client disconnects mid-stream, write() raises SIGPIPE. ignore it
     * so we get EPIPE on the syscall instead of dying. */
    signal(SIGPIPE, SIG_IGN);

    session_init();

    fprintf(stderr,
        "crypt.c listening on :%d\n"
        "open http://localhost:%d/ in a browser. wasd to move.\n",
        port, port);

    return server_start(port);
}
