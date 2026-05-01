/*
 * game.c — turn logic. the world only ticks when the player presses a key.
 *
 * a player turn:
 *   1. interpret the key (move / wait / restart / descend)
 *   2. resolve player action: bump-attack if enemy in target tile, else step
 *   3. pick up gold/potion if we stepped onto one
 *   4. for each enemy, run AI: step toward player if visible, attack on bump
 *   5. recompute fov
 *
 * if the player dies, further inputs except 'r' are ignored.
 */

#include "game.h"
#include "map.h"
#include "fov.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

GameState *game_new(unsigned int seed) {
    GameState *g = (GameState *)calloc(1, sizeof *g);
    if (!g) return NULL;
    g->depth = 1;
    g->turn = 0;
    g->gold = 0;
    g->alive = 1;
    g->won = 0;
    g->player.hp_max = 16; g->player.hp = 16; g->player.atk = 4;
    g->player.glyph = '@'; g->player.alive = 1;
    snprintf(g->message, sizeof g->message,
             "you wake in the crypt. something is breathing nearby.");
    map_generate(g, 1, seed);
    fov_compute(g);
    return g;
}

void game_free(GameState *g) {
    if (!g) return;
    free(g);
}

void game_reset(GameState *g) {
    unsigned int s = (unsigned int)time(NULL) ^ ((unsigned int)(uintptr_t)g << 1);
    memset(g, 0, sizeof *g);
    g->depth = 1; g->alive = 1;
    g->player.hp_max = 16; g->player.hp = 16; g->player.atk = 4;
    g->player.glyph = '@'; g->player.alive = 1;
    snprintf(g->message, sizeof g->message,
             "another life. another crypt. another mistake.");
    map_generate(g, 1, s);
    fov_compute(g);
}

static int in_bounds(int x, int y) {
    return x >= 0 && y >= 0 && x < MAP_W && y < MAP_H;
}
static int walkable(const GameState *g, int x, int y) {
    if (!in_bounds(x, y)) return 0;
    Tile t = (Tile)g->tiles[y][x];
    return t == TILE_FLOOR || t == TILE_DOOR || t == TILE_STAIRS
        || t == TILE_POTION || t == TILE_GOLD;
}

static Entity *enemy_at(GameState *g, int x, int y) {
    for (int i = 0; i < g->n_enemies; i++) {
        Entity *e = &g->enemies[i];
        if (e->alive && e->x == x && e->y == y) return e;
    }
    return NULL;
}

static void msg(GameState *g, const char *fmt, ...) {
    char tmp[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    snprintf(g->message, sizeof g->message, "%s", tmp);
}

static void player_attack(GameState *g, Entity *e) {
    int dmg = g->player.atk - 1 + (rand() % 3); /* atk..atk+1 */
    if (dmg < 1) dmg = 1;
    e->hp -= dmg;
    if (e->hp <= 0) {
        e->alive = 0;
        msg(g, "you slay the %c for %d damage.", e->glyph, dmg);
        /* drop a tiny bit of gold sometimes */
        if ((rand() % 3) == 0) g->gold += 1 + (rand() % 3);
    } else {
        msg(g, "you hit the %c for %d. (it has %d hp left)", e->glyph, dmg, e->hp);
    }
}

static void enemy_attack(GameState *g, Entity *e) {
    int dmg = e->atk - 1 + (rand() % 3);
    if (dmg < 1) dmg = 1;
    g->player.hp -= dmg;
    if (g->player.hp <= 0) {
        g->player.hp = 0;
        g->player.alive = 0;
        msg(g, "the %c kills you. (%d damage)", e->glyph, dmg);
    } else {
        msg(g, "the %c hits you for %d.", e->glyph, dmg);
    }
}

static int sign(int x) { return (x > 0) - (x < 0); }

static void run_enemy_turn(GameState *g) {
    for (int i = 0; i < g->n_enemies; i++) {
        Entity *e = &g->enemies[i];
        if (!e->alive) continue;
        if (!g->player.alive) return;

        /* only chase if currently visible to the player (cheap proxy:
         * we just check if our tile is VIS_VISIBLE — same view area) */
        if (g->vis[e->y][e->x] != VIS_VISIBLE) continue;

        int dx = sign(g->player.x - e->x);
        int dy = sign(g->player.y - e->y);

        /* if adjacent, attack */
        int adj = (e->x + dx == g->player.x && e->y + dy == g->player.y)
               && (abs(g->player.x - e->x) <= 1 && abs(g->player.y - e->y) <= 1);
        if (adj) {
            enemy_attack(g, e);
            continue;
        }

        /* try diagonal first then axis-aligned fallback */
        int candidates[3][2] = { {dx, dy}, {dx, 0}, {0, dy} };
        for (int c = 0; c < 3; c++) {
            int nx = e->x + candidates[c][0];
            int ny = e->y + candidates[c][1];
            if (candidates[c][0] == 0 && candidates[c][1] == 0) continue;
            if (!walkable(g, nx, ny)) continue;
            if (g->player.x == nx && g->player.y == ny) continue; /* don't step onto player */
            if (enemy_at(g, nx, ny)) continue;
            e->x = nx; e->y = ny;
            break;
        }
    }
}

static void try_descend(GameState *g) {
    if (g->tiles[g->player.y][g->player.x] != TILE_STAIRS) {
        msg(g, "no stairs here.");
        return;
    }
    if (g->depth >= MAX_DEPTH) {
        g->won = 1;
        msg(g, "you climb the final stair. daylight.");
        return;
    }
    int new_depth = g->depth + 1;
    msg(g, "you descend to depth %d. the air gets colder.", new_depth);
    map_generate(g, new_depth, g->seed ^ 0x9e3779b9u);
    g->depth = new_depth;
    fov_compute(g);
}

static void try_pickup(GameState *g) {
    Tile t = (Tile)g->tiles[g->player.y][g->player.x];
    if (t == TILE_GOLD) {
        int amt = 1 + (rand() % 5);
        g->gold += amt;
        g->tiles[g->player.y][g->player.x] = TILE_FLOOR;
        msg(g, "+%d gold.", amt);
    } else if (t == TILE_POTION) {
        int heal = 6 + (rand() % 4);
        g->player.hp += heal;
        if (g->player.hp > g->player.hp_max) g->player.hp = g->player.hp_max;
        g->tiles[g->player.y][g->player.x] = TILE_FLOOR;
        msg(g, "you drink. +%d hp.", heal);
    }
}

void game_apply_input(GameState *g, char key) {
    if (!g->player.alive || g->won) {
        if (key == 'r') game_reset(g);
        return;
    }

    int dx = 0, dy = 0;
    int wait = 0;
    switch (key) {
        case 'w': dy = -1; break;
        case 's': dy =  1; break;
        case 'a': dx = -1; break;
        case 'd': dx =  1; break;
        case '.': wait = 1; break;
        case '>': try_descend(g); g->turn++; run_enemy_turn(g); fov_compute(g); return;
        case 'r': game_reset(g); return;
        default: return;
    }

    if (wait) {
        msg(g, "you wait.");
    } else {
        int nx = g->player.x + dx;
        int ny = g->player.y + dy;
        if (!in_bounds(nx, ny) || g->tiles[ny][nx] == TILE_WALL || g->tiles[ny][nx] == TILE_VOID) {
            msg(g, "the wall does not yield.");
            return;
        }
        Entity *e = enemy_at(g, nx, ny);
        if (e) {
            player_attack(g, e);
        } else {
            g->player.x = nx; g->player.y = ny;
            try_pickup(g);
        }
    }

    g->turn++;
    run_enemy_turn(g);
    fov_compute(g);
}
