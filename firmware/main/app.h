/*
 * What the game task shares with the web server.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "game.h"

typedef struct {
    game_state_t state;
    int32_t level;
    int32_t lives;
    uint32_t score;
    int32_t n_leds;
    uint32_t frame;             /* frame counter, to see that the game runs */
    uint32_t current_ma;        /* estimated strip current of the last frame */
    game_rgb_t leds[GAME_MAX_LEDS];
} app_snapshot_t;

/** Copy of the last rendered frame and the game status. */
void app_snapshot_get(app_snapshot_t *out);

/** Press a button from elsewhere (the web page). GAME_RED/YELLOW/BLUE. */
void app_remote_press(uint8_t color);
