/*
 * fov.c — recursive shadowcasting FOV.
 *
 * adapted from the canonical 8-octant Bjorn-Bergstrom recipe, kept
 * intentionally short. blockers are walls and void tiles; everything else
 * is transparent.
 *
 * after fov_compute(): visible tiles == VIS_VISIBLE, tiles seen previously
 * but not currently visible == VIS_EXPLORED, never-seen == VIS_HIDDEN.
 */

#include "fov.h"
#include "game.h"

static int blocks_sight(const GameState *g, int x, int y) {
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return 1;
    Tile t = (Tile)g->tiles[y][x];
    return t == TILE_WALL || t == TILE_VOID;
}

static void cast(GameState *g, int cx, int cy, int row,
                 double start, double end, int radius,
                 int xx, int xy, int yx, int yy)
{
    if (start < end) return;
    double new_start = 0;
    int blocked = 0;
    for (int j = row; j <= radius && !blocked; j++) {
        int dy = -j;
        for (int dx = -j; dx <= 0; dx++) {
            int X = cx + dx * xx + dy * xy;
            int Y = cy + dx * yx + dy * yy;
            double l_slope = (dx - 0.5) / (dy + 0.5);
            double r_slope = (dx + 0.5) / (dy - 0.5);
            if (start < r_slope) continue;
            if (end > l_slope) break;

            if (X >= 0 && Y >= 0 && X < MAP_W && Y < MAP_H) {
                int dist2 = dx * dx + dy * dy;
                if (dist2 <= radius * radius) {
                    g->vis[Y][X] = VIS_VISIBLE;
                }
                if (blocked) {
                    if (blocks_sight(g, X, Y)) {
                        new_start = r_slope;
                        continue;
                    } else {
                        blocked = 0;
                        start = new_start;
                    }
                } else if (blocks_sight(g, X, Y) && j < radius) {
                    blocked = 1;
                    cast(g, cx, cy, j + 1, start, l_slope, radius,
                         xx, xy, yx, yy);
                    new_start = r_slope;
                }
            }
        }
    }
}

static const int OCTANTS[8][4] = {
    { 1,  0,  0,  1},
    { 0,  1,  1,  0},
    { 0, -1,  1,  0},
    {-1,  0,  0,  1},
    {-1,  0,  0, -1},
    { 0, -1, -1,  0},
    { 0,  1, -1,  0},
    { 1,  0,  0, -1},
};

void fov_compute(GameState *g) {
    /* demote previously visible tiles */
    for (int y = 0; y < MAP_H; y++) {
        for (int x = 0; x < MAP_W; x++) {
            if (g->vis[y][x] == VIS_VISIBLE) g->vis[y][x] = VIS_EXPLORED;
        }
    }
    /* player always sees their own tile */
    g->vis[g->player.y][g->player.x] = VIS_VISIBLE;
    for (int oct = 0; oct < 8; oct++) {
        cast(g, g->player.x, g->player.y, 1, 1.0, 0.0, FOV_RADIUS,
             OCTANTS[oct][0], OCTANTS[oct][1],
             OCTANTS[oct][2], OCTANTS[oct][3]);
    }
}
