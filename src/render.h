#ifndef CRYPT_RENDER_H
#define CRYPT_RENDER_H

#include "game.h"

/* allocate a NUL-terminated HTML frame for the given state.
 * caller free()s. each row is on its own line so the SSE writer
 * can emit one `data:` field per line. */
char *render_frame_html(const GameState *g);

#endif
