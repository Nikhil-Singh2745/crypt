/*
 * map.c — BSP map generation. classic recipe:
 *   1. take a big rect
 *   2. split it into two children along the long axis
 *   3. recurse until children are small enough to be a room
 *   4. carve a randomly-sized room inside each leaf
 *   5. for each non-leaf, connect the centers of its two children with
 *      an L-shaped corridor
 *
 * not academic but it produces dungeon-shaped output reliably.
 */

#include "map.h"
#include "game.h"

#include <stdlib.h>
#include <string.h>

#define MIN_LEAF 8

unsigned int xrand(unsigned int *state) {
    /* xorshift32. fine. */
    unsigned int x = *state ? *state : 0x12345678u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}
int xrand_range(unsigned int *state, int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + (int)(xrand(state) % (unsigned int)(hi - lo + 1));
}

typedef struct Leaf {
    int x, y, w, h;
    int rx, ry, rw, rh;       /* the room inside */
    int cx, cy;               /* room center */
    struct Leaf *a, *b;
} Leaf;

static Leaf *leaf_new(int x, int y, int w, int h) {
    Leaf *l = (Leaf *)calloc(1, sizeof *l);
    l->x = x; l->y = y; l->w = w; l->h = h;
    return l;
}
static void leaf_free(Leaf *l) {
    if (!l) return;
    leaf_free(l->a); leaf_free(l->b);
    free(l);
}

static int leaf_split(Leaf *l, unsigned int *st) {
    if (l->a || l->b) return 0;
    int split_h;
    if (l->w > l->h && (double)l->w / l->h >= 1.25) split_h = 0;
    else if (l->h > l->w && (double)l->h / l->w >= 1.25) split_h = 1;
    else split_h = (xrand(st) & 1);

    int max = (split_h ? l->h : l->w) - MIN_LEAF;
    if (max <= MIN_LEAF) return 0;
    int s = xrand_range(st, MIN_LEAF, max);
    if (split_h) {
        l->a = leaf_new(l->x, l->y, l->w, s);
        l->b = leaf_new(l->x, l->y + s, l->w, l->h - s);
    } else {
        l->a = leaf_new(l->x, l->y, s, l->h);
        l->b = leaf_new(l->x + s, l->y, l->w - s, l->h);
    }
    return 1;
}

static void leaf_recurse(Leaf *l, unsigned int *st, int depth) {
    if (depth > 6) return;
    if (l->w > 16 || l->h > 12 || (xrand(st) % 100) < 75) {
        if (leaf_split(l, st)) {
            leaf_recurse(l->a, st, depth + 1);
            leaf_recurse(l->b, st, depth + 1);
        }
    }
}

static void carve_box(GameState *g, int x, int y, int w, int h, Tile t) {
    for (int yy = y; yy < y + h; yy++) {
        if (yy < 0 || yy >= MAP_H) continue;
        for (int xx = x; xx < x + w; xx++) {
            if (xx < 0 || xx >= MAP_W) continue;
            g->tiles[yy][xx] = (unsigned char)t;
        }
    }
}

static void carve_room(GameState *g, Leaf *l, unsigned int *st) {
    int rw = xrand_range(st, 4, l->w - 2);
    int rh = xrand_range(st, 3, l->h - 2);
    int rx = l->x + xrand_range(st, 1, l->w - rw - 1);
    int ry = l->y + xrand_range(st, 1, l->h - rh - 1);
    if (rw < 3) rw = 3;
    if (rh < 3) rh = 3;
    l->rx = rx; l->ry = ry; l->rw = rw; l->rh = rh;
    l->cx = rx + rw / 2; l->cy = ry + rh / 2;
    carve_box(g, rx, ry, rw, rh, TILE_FLOOR);
}

static void carve_corridor(GameState *g, int x1, int y1, int x2, int y2, unsigned int *st) {
    /* L-shape: pick which leg first */
    int horiz_first = xrand(st) & 1;
    int x = x1, y = y1;
    if (horiz_first) {
        while (x != x2) { if (g->tiles[y][x] == TILE_VOID) g->tiles[y][x] = TILE_FLOOR; x += (x2 > x) ? 1 : -1; }
        while (y != y2) { if (g->tiles[y][x] == TILE_VOID) g->tiles[y][x] = TILE_FLOOR; y += (y2 > y) ? 1 : -1; }
    } else {
        while (y != y2) { if (g->tiles[y][x] == TILE_VOID) g->tiles[y][x] = TILE_FLOOR; y += (y2 > y) ? 1 : -1; }
        while (x != x2) { if (g->tiles[y][x] == TILE_VOID) g->tiles[y][x] = TILE_FLOOR; x += (x2 > x) ? 1 : -1; }
    }
    g->tiles[y][x] = TILE_FLOOR;
}

/* find any leaf's room center */
static void any_center(Leaf *l, int *x, int *y) {
    if (l->a) { any_center(l->a, x, y); return; }
    *x = l->cx; *y = l->cy;
}

static void connect(GameState *g, Leaf *l, unsigned int *st) {
    if (!l->a || !l->b) return;
    int ax, ay, bx, by;
    any_center(l->a, &ax, &ay);
    any_center(l->b, &bx, &by);
    carve_corridor(g, ax, ay, bx, by, st);
    connect(g, l->a, st);
    connect(g, l->b, st);
}

static void leaves_to_rooms(Leaf *l, unsigned int *st, GameState *g) {
    if (l->a) {
        leaves_to_rooms(l->a, st, g);
        leaves_to_rooms(l->b, st, g);
    } else {
        carve_room(g, l, st);
    }
}

/* surround floor with walls. anything still TILE_VOID adjacent to a floor
 * becomes a wall. */
static void wallify(GameState *g) {
    for (int y = 0; y < MAP_H; y++) {
        for (int x = 0; x < MAP_W; x++) {
            if (g->tiles[y][x] != TILE_VOID) continue;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= MAP_W || ny >= MAP_H) continue;
                    if (g->tiles[ny][nx] == TILE_FLOOR) { g->tiles[y][x] = TILE_WALL; goto next; }
                }
            }
        next:;
        }
    }
}

/* pick a random floor tile, optionally rejecting tiles already occupied */
static int random_floor(GameState *g, unsigned int *st, int *ox, int *oy) {
    for (int tries = 0; tries < 1000; tries++) {
        int x = xrand_range(st, 1, MAP_W - 2);
        int y = xrand_range(st, 1, MAP_H - 2);
        if (g->tiles[y][x] != TILE_FLOOR) continue;
        if (g->player.alive && g->player.x == x && g->player.y == y) continue;
        int taken = 0;
        for (int i = 0; i < g->n_enemies; i++) {
            if (g->enemies[i].alive && g->enemies[i].x == x && g->enemies[i].y == y) { taken = 1; break; }
        }
        if (taken) continue;
        *ox = x; *oy = y;
        return 1;
    }
    return 0;
}

static void place_enemy(GameState *g, unsigned int *st, char glyph, int hp, int atk) {
    if (g->n_enemies >= MAX_ENEMIES) return;
    int x, y;
    if (!random_floor(g, st, &x, &y)) return;
    Entity *e = &g->enemies[g->n_enemies++];
    e->x = x; e->y = y;
    e->hp = hp; e->hp_max = hp;
    e->atk = atk;
    e->glyph = glyph;
    e->alive = 1;
    e->cooldown = 0;
}

void map_generate(GameState *g, int depth, unsigned int seed) {
    /* clear */
    memset(g->tiles, 0, sizeof g->tiles);
    memset(g->vis, VIS_HIDDEN, sizeof g->vis);
    g->n_enemies = 0;
    g->depth = depth;

    unsigned int st = seed ? seed : 0xdeadbeefu;

    /* one big leaf, with a 1-tile margin */
    Leaf *root = leaf_new(1, 1, MAP_W - 2, MAP_H - 2);
    leaf_recurse(root, &st, 0);
    leaves_to_rooms(root, &st, g);
    connect(g, root, &st);
    wallify(g);

    /* place player in some leaf's room center */
    int px, py;
    any_center(root, &px, &py);
    g->player.x = px; g->player.y = py;
    if (g->player.hp_max == 0) {
        g->player.hp_max = 16; g->player.hp = 16; g->player.atk = 4;
    }
    g->player.glyph = '@';
    g->player.alive = 1;

    /* stairs somewhere far from the player */
    int sx, sy, best = -1;
    for (int tries = 0; tries < 200; tries++) {
        int x, y;
        if (!random_floor(g, &st, &x, &y)) break;
        int d = (x - px) * (x - px) + (y - py) * (y - py);
        if (d > best) { best = d; sx = x; sy = y; }
    }
    if (best >= 0) g->tiles[sy][sx] = TILE_STAIRS;

    /* enemies — more of them, and tougher, deeper down */
    int n_goblins = 4 + depth;
    int n_orcs    = depth;            /* 0 on level 1 */
    for (int i = 0; i < n_goblins; i++) place_enemy(g, &st, 'g', 3 + depth, 2);
    for (int i = 0; i < n_orcs; i++)    place_enemy(g, &st, 'o', 6 + depth * 2, 3 + depth);

    /* a couple of potions and gold piles */
    int n_pot = 2;
    for (int i = 0; i < n_pot; i++) {
        int x, y;
        if (random_floor(g, &st, &x, &y)) g->tiles[y][x] = TILE_POTION;
    }
    int n_gold = 3 + depth;
    for (int i = 0; i < n_gold; i++) {
        int x, y;
        if (random_floor(g, &st, &x, &y)) g->tiles[y][x] = TILE_GOLD;
    }

    leaf_free(root);
    g->seed = st;
}
