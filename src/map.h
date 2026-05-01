#ifndef CRYPT_MAP_H
#define CRYPT_MAP_H

#include "game.h"

/* generate level `depth` into g, place player, enemies, items, stairs */
void map_generate(GameState *g, int depth, unsigned int seed);

/* simple uniform random for the troll */
unsigned int xrand(unsigned int *state);
int xrand_range(unsigned int *state, int lo, int hi); /* inclusive */

#endif
