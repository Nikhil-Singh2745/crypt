#ifndef CRYPT_ROUTER_H
#define CRYPT_ROUTER_H

#include "server.h"

/* dispatch a request on `fd`. returns when the handler is done.
 * caller closes the socket. */
void router_dispatch(int fd, Request *r);

#endif
