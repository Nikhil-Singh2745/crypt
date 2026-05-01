#ifndef CRYPT_FOV_H
#define CRYPT_FOV_H

#include "game.h"

/* recompute visibility from player position. visible tiles get VIS_VISIBLE,
 * previously visible tiles outside fov degrade to VIS_EXPLORED. */
void fov_compute(GameState *g);

#endif
