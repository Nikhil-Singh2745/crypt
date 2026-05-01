/*
 * render.c — turn the game state into a chunk of HTML.
 *
 * each tile becomes one character with a CSS class. enemies and the player
 * render on top of the underlying tile. tiles outside fov but explored are
 * rendered with .dim; never-seen tiles are spaces.
 *
 * the output has explicit newlines between rows. the SSE writer turns each
 * line into a separate `data:` field so the browser receives them
 * concatenated by '\n', which is exactly what <pre> expects.
 */

#include "render.h"
#include "game.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUF_INIT 16384

typedef struct {
    char  *buf;
    size_t len, cap;
} Buf;

static void bput(Buf *b, const char *s) {
    size_t n = strlen(s);
    if (b->len + n + 1 > b->cap) {
        while (b->len + n + 1 > b->cap) b->cap *= 2;
        b->buf = (char *)realloc(b->buf, b->cap);
    }
    memcpy(b->buf + b->len, s, n);
    b->len += n;
    b->buf[b->len] = '\0';
}

static void bputc(Buf *b, char c) {
    char tmp[2] = { c, 0 };
    bput(b, tmp);
}

static void bputf(Buf *b, const char *fmt, ...) {
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    bput(b, tmp);
}

static const char *tile_class(Tile t) {
    switch (t) {
        case TILE_WALL:   return "wall";
        case TILE_FLOOR:  return "floor";
        case TILE_DOOR:   return "door";
        case TILE_STAIRS: return "stairs";
        case TILE_POTION: return "item";
        case TILE_GOLD:   return "gold";
        default:          return "floor";
    }
}

static char tile_glyph(Tile t) {
    switch (t) {
        case TILE_WALL:   return '#';
        case TILE_FLOOR:  return '.';
        case TILE_DOOR:   return '+';
        case TILE_STAIRS: return '>';
        case TILE_POTION: return '!';
        case TILE_GOLD:   return '$';
        default:          return ' ';
    }
}

/* basic HTML escape — we render '<', '>', '&' raw in the message string */
static void put_escaped_char(Buf *b, char c) {
    switch (c) {
        case '<': bput(b, "&lt;");   break;
        case '>': bput(b, "&gt;");   break;
        case '&': bput(b, "&amp;");  break;
        case ' ': bput(b, "&nbsp;"); break; /* preserve runs of spaces in the HUD */
        default:  bputc(b, c);
    }
}

static void put_escaped(Buf *b, const char *s) {
    for (; *s; s++) put_escaped_char(b, *s);
}

char *render_frame_html(const GameState *g) {
    Buf b = { malloc(BUF_INIT), 0, BUF_INIT };
    b.buf[0] = '\0';

    /* HUD line 1: stats */
    bput(&b, "<span class=\"hud\">");
    char hud[256];
    snprintf(hud, sizeof hud,
             "HP %d/%d  ATK %d  GOLD %d  DEPTH %d/%d  TURN %d",
             g->player.hp, g->player.hp_max, g->player.atk,
             g->gold, g->depth, MAX_DEPTH, g->turn);
    put_escaped(&b, hud);
    bput(&b, "</span>\n");

    /* HUD line 2: most recent message, or hints if empty */
    bput(&b, "<span class=\"msg\">");
    if (g->message[0]) put_escaped(&b, g->message);
    else put_escaped(&b, "wasd / arrows to move. bump enemies to attack. > on stairs to descend. r to restart.");
    bput(&b, "</span>\n");

    /* the map itself */
    for (int y = 0; y < MAP_H; y++) {
        for (int x = 0; x < MAP_W; x++) {
            Vis v = (Vis)g->vis[y][x];
            Tile t = (Tile)g->tiles[y][x];

            if (v == VIS_HIDDEN) {
                bput(&b, "&nbsp;");
                continue;
            }

            /* is there an entity here we should draw on top of the tile?
             * only if the tile is currently visible (we don't reveal monsters
             * through fog of war). */
            char ch = tile_glyph(t);
            const char *cls = tile_class(t);

            if (v == VIS_VISIBLE) {
                if (g->player.alive && g->player.x == x && g->player.y == y) {
                    ch = '@'; cls = "player";
                } else {
                    for (int i = 0; i < g->n_enemies; i++) {
                        const Entity *e = &g->enemies[i];
                        if (e->alive && e->x == x && e->y == y) {
                            ch = e->glyph; cls = "enemy"; break;
                        }
                    }
                }
                bputf(&b, "<span class=\"%s\">", cls);
                put_escaped_char(&b, ch);
                bput(&b, "</span>");
            } else {
                /* explored but not currently visible */
                bput(&b, "<span class=\"dim\">");
                put_escaped_char(&b, ch);
                bput(&b, "</span>");
            }
        }
        bputc(&b, '\n');
    }

    /* end-of-game banner */
    if (g->won) {
        bput(&b, "<span class=\"good\">YOU ESCAPED THE CRYPT. press r to crawl again.</span>\n");
    } else if (!g->player.alive) {
        bput(&b, "<span class=\"bad\">YOU DIED. press r to crawl again.</span>\n");
    }

    return b.buf;
}
