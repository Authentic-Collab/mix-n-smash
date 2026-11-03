#pragma once

#include <stddef.h>
#include <stdint.h>

#define HS_MAX       10
#define HS_NAME_LEN  16

typedef struct {
    char name[HS_NAME_LEN + 1];
    uint8_t level;
    uint8_t reserved[2];
    uint32_t score;
} hs_entry_t;

/** Bring up NVS and load the table and the player name. */
void highscores_init(void);

/** Insert a result. Returns its 1-based rank, or 0 if it did not make the table. */
int highscores_add(const char *name, uint32_t score, uint8_t level);

/** Copy up to max entries, best first. Returns how many were written. */
int highscores_get(hs_entry_t *out, int max);

uint32_t highscores_best(void);

void highscores_clear(void);

/** The name the next games are played under (kept in flash). */
void highscores_set_player(const char *name);
void highscores_get_player(char *out, size_t cap);

/** Keep letters, digits, space and -_. ; trim; cut to HS_NAME_LEN. */
void highscores_sanitize(char *dst, size_t cap, const char *src);
