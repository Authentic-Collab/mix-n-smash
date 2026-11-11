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

/** A game is going on (countdown, playing or level clear). */
bool app_game_running(void);

/** Press a button from the web page. GAME_RED/YELLOW/BLUE. */
void app_remote_press(uint8_t color);

/** Stop the running game (admin). */
void app_abort_game(void);

/** Settings were saved: apply what can change without a reboot. */
void app_settings_changed(void);

/** Reboot in half a second, so the HTTP answer still gets out. */
void app_restart(void);

/* ---- Colour palette (index = colour mask) ------------------------------- */

void app_get_palette(game_rgb_t out[8]);
void app_set_color(uint8_t mask, game_rgb_t color);
void app_reset_palette(void);
bool app_save_palette(void);

/**
 * Show the colour chart instead of the game. on = true keeps it up for
 * another 5 seconds (the web page repeats the call while its colour panel
 * is open); on = false ends it at once.
 */
void app_set_preview(bool on);
