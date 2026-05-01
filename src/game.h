#ifndef CRYPT_GAME_H
#define CRYPT_GAME_H

/*
 * the entire game in one struct. no oop, no entity-component-system,
 * no factory pattern, no service locator. just bytes.
 */

#define MAP_W 80
#define MAP_H 22
#define MAX_ENEMIES 24
#define FOV_RADIUS 8
#define MAX_DEPTH 5

typedef enum {
    TILE_VOID = 0,
    TILE_WALL,
    TILE_FLOOR,
    TILE_DOOR,
    TILE_STAIRS,
    TILE_POTION,
    TILE_GOLD,
} Tile;

typedef enum {
    VIS_HIDDEN = 0,
    VIS_EXPLORED,
    VIS_VISIBLE,
} Vis;

typedef struct {
    int x, y;
    int hp, hp_max;
    int atk;
    char glyph;
    int alive;
    int cooldown;       /* enemies skip a turn occasionally so combat isn't impossible */
} Entity;

typedef struct {
    unsigned char tiles[MAP_H][MAP_W];   /* Tile */
    unsigned char vis[MAP_H][MAP_W];     /* Vis */
    Entity player;
    Entity enemies[MAX_ENEMIES];
    int n_enemies;
    int depth;          /* 1..MAX_DEPTH */
    int gold;
    int turn;
    int alive;
    int won;
    char message[128];  /* the latest log line, shown in the HUD */
    unsigned int seed;
} GameState;

GameState *game_new(unsigned int seed);
void       game_free(GameState *g);
void       game_apply_input(GameState *g, char key);
void       game_reset(GameState *g);

#endif
