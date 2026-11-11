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
    bool preview;               /* the strip shows the colour chart, game paused */
    game_rgb_t leds[GAME_MAX_LEDS];
} app_snapshot_t;

/** Copy of the last rendered frame and the game status. */
void app_snapshot_get(app_snapshot_t *out);

/** Press a button from elsewhere (the web page). GAME_RED/YELLOW/BLUE. */
void app_remote_press(uint8_t color);

/* ---- Colour palette (index = colour mask) ------------------------------- */

void app_get_palette(game_rgb_t out[8]);

/** Change one colour; the strip shows it from the next frame. Not saved. */
void app_set_color(uint8_t mask, game_rgb_t color);

/** Back to the defaults, in flash too. */
void app_reset_palette(void);

/** Keep the current colours across reboots. */
bool app_save_palette(void);

/**
 * Show the colour chart instead of the game. on = true keeps it up for
 * another 5 seconds (the web page repeats the call while its colour panel
 * is open); on = false ends it at once.
 */
void app_set_preview(bool on);
