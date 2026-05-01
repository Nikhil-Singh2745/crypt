/*
 * session.c — fixed-size table of sessions keyed by random cookie id.
 *
 * we don't expire anything until we run out of slots, then we evict the
 * least-recently-used. this is "good enough"; if the server reboots,
 * everyone dies. on the brand.
 */

#include "session.h"
#include "game.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>

extern GameState *game_new(unsigned int seed);
extern void       game_free(GameState *g);

static Session g_table[MAX_SESSIONS];
static pthread_mutex_t g_table_mu = PTHREAD_MUTEX_INITIALIZER;

static long now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static unsigned int seed_random(void) {
    /* combine /dev/urandom with time so we don't all spawn the same dungeon */
    unsigned int s = 0;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        if (read(fd, &s, sizeof s) != sizeof s) s = 0;
        close(fd);
    }
    if (!s) s = (unsigned int)(now_ms() ^ ((long)getpid() << 16));
    return s;
}

static void make_sid(char *out) {
    static const char alphabet[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    unsigned char rnd[SID_LEN];
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        ssize_t got = read(fd, rnd, SID_LEN);
        close(fd);
        if (got != (ssize_t)SID_LEN) {
            for (int i = 0; i < SID_LEN; i++) rnd[i] = (unsigned char)rand();
        }
    } else {
        for (int i = 0; i < SID_LEN; i++) rnd[i] = (unsigned char)rand();
    }
    for (int i = 0; i < SID_LEN; i++) out[i] = alphabet[rnd[i] % (sizeof alphabet - 1)];
    out[SID_LEN] = '\0';
}

void session_init(void) {
    srand((unsigned int)now_ms());
    for (int i = 0; i < MAX_SESSIONS; i++) {
        pthread_mutex_init(&g_table[i].mu, NULL);
        g_table[i].in_use = 0;
        g_table[i].state = NULL;
    }
}

Session *session_find(const char *sid) {
    if (!sid || !*sid) return NULL;
    pthread_mutex_lock(&g_table_mu);
    Session *found = NULL;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (g_table[i].in_use && strcmp(g_table[i].id, sid) == 0) {
            g_table[i].last_used_ms = now_ms();
            found = &g_table[i];
            break;
        }
    }
    pthread_mutex_unlock(&g_table_mu);
    return found;
}

Session *session_create(char *out_id) {
    pthread_mutex_lock(&g_table_mu);

    int slot = -1;
    /* prefer a free slot */
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (!g_table[i].in_use) { slot = i; break; }
    }
    /* otherwise evict the LRU */
    if (slot < 0) {
        long oldest = now_ms() + 1;
        for (int i = 0; i < MAX_SESSIONS; i++) {
            if (g_table[i].last_used_ms < oldest) {
                oldest = g_table[i].last_used_ms;
                slot = i;
            }
        }
        if (g_table[slot].state) {
            game_free(g_table[slot].state);
            g_table[slot].state = NULL;
        }
        g_table[slot].in_use = 0;
    }

    Session *s = &g_table[slot];
    make_sid(s->id);
    s->state = game_new(seed_random());
    s->version = 1;
    s->last_used_ms = now_ms();
    s->in_use = 1;
    if (out_id) memcpy(out_id, s->id, SID_LEN + 1);

    pthread_mutex_unlock(&g_table_mu);
    return s;
}

void session_lock(Session *s)   { pthread_mutex_lock(&s->mu); }
void session_unlock(Session *s) { pthread_mutex_unlock(&s->mu); }
void session_bump_locked(Session *s) { s->version++; }
