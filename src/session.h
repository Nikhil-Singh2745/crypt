#ifndef CRYPT_SESSION_H
#define CRYPT_SESSION_H

#include <pthread.h>
#include "game.h"

#define SID_LEN 24
#define MAX_SESSIONS 256

typedef struct {
    char id[SID_LEN + 1];
    GameState *state;
    pthread_mutex_t mu;
    unsigned long version;   /* bumped on every state mutation */
    long last_used_ms;
    int in_use;
} Session;

void     session_init(void);
/* find session by sid (from cookie). returns NULL if missing. */
Session *session_find(const char *sid);
/* allocate a fresh session, copy id into out (size >= SID_LEN+1). */
Session *session_create(char *out_id);
void     session_lock(Session *s);
void     session_unlock(Session *s);
void     session_bump_locked(Session *s);  /* call with lock held */

#endif
