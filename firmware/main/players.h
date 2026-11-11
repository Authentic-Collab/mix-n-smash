#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "highscores.h"

/*
 * Who plays from the web page. One current player, the others wait in a
 * queue. Each is known by the session token of their phone.
 */

#define PLAYERS_MAX_QUEUE 10

typedef enum {
    PLAYERS_OK = 0,
    PLAYERS_NAME_TAKEN,     /* suggestion holds a free variant such as "Jan (2)" */
    PLAYERS_BAD_NAME,
    PLAYERS_FULL,
} players_result_t;

typedef struct {
    char name[HS_NAME_LEN + 1];
    int32_t games;          /* games started in this turn */
} player_info_t;

void players_init(void);

/**
 * Join with a name. accept_suffix: when the name is in use, take the
 * suggested variant instead of refusing. Joining again with the same token
 * just reports the current position. position: 0 = playing now, 1.. = place
 * in the queue.
 */
players_result_t players_join(const char *token, const char *name, bool accept_suffix,
                              char suggestion[HS_NAME_LEN + 1], int32_t *position);
void players_leave(const char *token);
/** Heartbeat from a phone; returns the position or -1 when unknown. */
int32_t players_seen(const char *token);
bool players_is_current(const char *token);
bool players_has_current(void);
/** Name of the current player; false when nobody plays from the web. */
bool players_current_name(char out[HS_NAME_LEN + 1]);

/** Snapshot for the pages: current (games == -1 when none) and the queue. */
int32_t players_list(player_info_t *current, player_info_t *queue, int32_t max);
/** Index (1-based in the queue) of the player chosen to go next, or 0. */
int32_t players_pending(void);

/* The game task calls these. */
void players_game_started(void);
/** A game ended (or was aborted): rotate after games_per_turn, apply the admin's choice. */
void players_game_finished(int32_t games_per_turn, bool aborted);
/** Drop phones that went quiet. game_running: do not drop the current player mid-game. */
void players_tick(bool game_running);

/* Admin actions. index: 0 = the current player, 1.. = queue position. */
bool players_admin_activate(int32_t index, bool game_running);
bool players_admin_remove(int32_t index);
