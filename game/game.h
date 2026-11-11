/*
 * Mix 'n Smash: the game rules, independent of any hardware.
 *
 * Coloured dots travel along an LED strip towards the player, who sits at
 * LED 0. The player has three buttons: red, yellow and blue. Pressing one
 * fires a dot of that colour away from the player. When a shot meets an
 * incoming dot:
 *
 *   - a dot of the same colour is neutralised;
 *   - a mixed colour loses the shot's colour (orange hit by yellow turns red,
 *     and is then neutralised by a red shot);
 *   - a colour the dot does not contain costs the player a life.
 *
 * A dot that reaches the player also costs a life. The first three LEDs show
 * the remaining lives. Levels get faster, with more mixed colours and more
 * dots at once; the speed also creeps up inside a level.
 *
 * The strip can be long (300 LEDs is the target), so no effect ever lights
 * the whole strip: feedback stays near the player, celebrations are short
 * comets, and the firmware adds a current limiter on top.
 *
 * Everything is integer arithmetic (positions in 1/256 LED, times in ms) and
 * the random generator is part of the state, so a game is fully
 * deterministic: the same seed and the same inputs give the same LEDs. The
 * browser version (web/game.js) is a line-by-line port of this file and the
 * tests check that both produce identical traces.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GAME_MIN_LEDS      16
#define GAME_MAX_LEDS      400
#define GAME_LIVES_LEDS    3      /* LEDs 0..2 show the lives */
#define GAME_PLAY_START    4      /* LED 3 stays dark: the player's line */
#define GAME_MAX_LIVES     3
#define GAME_MAX_DOTS      16
#define GAME_MAX_SHOTS     12
#define GAME_MAX_FLASHES   8
#define GAME_LEVELS        10     /* levels past the last one reuse it */
#define GAME_PALETTE_COLORS 7     /* red, orange, yellow, green, blue, purple, white */

/* Colours are bit masks: mixing is OR, a hit removes bits with AND NOT. */
#define GAME_RED           1u
#define GAME_YELLOW        2u
#define GAME_BLUE          4u
#define GAME_ORANGE        (GAME_RED | GAME_YELLOW)
#define GAME_PURPLE        (GAME_RED | GAME_BLUE)
#define GAME_GREEN         (GAME_YELLOW | GAME_BLUE)
#define GAME_WHITE         (GAME_RED | GAME_YELLOW | GAME_BLUE)

typedef struct {
    uint8_t r, g, b;
} game_rgb_t;

typedef enum {
    GAME_STATE_ATTRACT = 0,   /* idle colour wave, any button starts a game */
    GAME_STATE_COUNTDOWN,     /* shrinking bar before a level starts */
    GAME_STATE_PLAYING,
    GAME_STATE_LEVEL_CLEAR,   /* rainbow, then the next level number */
    GAME_STATE_GAME_OVER,     /* red pulses, then the level reached */
} game_state_t;

/* Events since the last game_take_events(), for sound, logging, scores. */
#define GAME_EV_START        (1u << 0)   /* a new game begins */
#define GAME_EV_SHOT         (1u << 1)
#define GAME_EV_DOT_CLEARED  (1u << 2)
#define GAME_EV_DOT_REDUCED  (1u << 3)   /* mixed dot lost one colour */
#define GAME_EV_WRONG_HIT    (1u << 4)
#define GAME_EV_DOT_ESCAPED  (1u << 5)   /* a dot reached the player */
#define GAME_EV_LIFE_LOST    (1u << 6)
#define GAME_EV_LEVEL_CLEAR  (1u << 7)
#define GAME_EV_GAME_OVER    (1u << 8)
#define GAME_EV_ABORTED      (1u << 9)   /* a running game was stopped from outside */

typedef struct {
    /* Stricter rule: a dot hit by a wrong colour keeps coming instead of
     * disappearing, so it can cost a second life at the player. */
    bool wrong_hit_keeps_dot;
    int32_t level1_dots;      /* dots in level 1 (default 8) */
    int32_t dots_per_level;   /* extra dots for every next level (default 2) */
    int32_t speed_pct;        /* 100 = normal; 50 = everything takes twice as long */
} game_options_t;

#define GAME_DEFAULT_LEVEL1_DOTS   8
#define GAME_DEFAULT_DOTS_PER_LEVEL 2
#define GAME_MIN_SPEED_PCT         25
#define GAME_MAX_SPEED_PCT         300

typedef struct {
    bool active;
    uint8_t mask;           /* colours still to be hit */
    uint8_t colors_at_spawn;
    int32_t pos;            /* LED index * 256, decreasing towards the player */
    int32_t speed;          /* LED/s * 256 */
} game_dot_t;

typedef struct {
    bool active;
    uint8_t mask;           /* one primary colour */
    int32_t pos;            /* LED index * 256, increasing */
    int32_t speed;          /* LED/s * 256 */
} game_shot_t;

typedef struct {
    bool active;
    int32_t led;            /* centre LED */
    game_rgb_t color;
    int32_t ttl_ms;
    int32_t total_ms;
} game_flash_t;

typedef struct {
    int32_t dots;           /* dots in the level */
    int32_t cross_ms;       /* time for a dot to cross the playfield, at the start of the level */
    int32_t mixed_pct;      /* chance of a two-colour dot */
    int32_t triple_pct;     /* chance of a three-colour (white) dot */
    int32_t max_concurrent; /* dots on the strip at the same time */
    int32_t gap_min_ms;     /* pause between spawns */
    int32_t gap_max_ms;
    int32_t speed_var_pct;  /* per-dot speed variation, +- percent */
} game_level_t;

typedef struct {
    int32_t n_leds;
    game_options_t opt;
    uint32_t rng;

    game_state_t state;
    int32_t state_ms;       /* time spent in the current state */
    int32_t countdown_ms;   /* length of the current countdown */
    uint32_t time_ms;       /* since game_init(), drives idle animations */

    int32_t level;          /* 1-based */
    int32_t lives;
    uint32_t score;
    int32_t dots_spawned;   /* in this level */
    int32_t dots_total;
    int32_t spawn_timer_ms; /* counts down to the next spawn */

    int32_t zone_flash_ms;  /* red overlay near the player, counts down */
    int32_t zone_flash_total_ms;

    game_dot_t dots[GAME_MAX_DOTS];
    game_shot_t shots[GAME_MAX_SHOTS];
    game_flash_t flashes[GAME_MAX_FLASHES];

    game_rgb_t palette[8];  /* colour shown for each colour mask (index = mask) */

    uint32_t events;
} game_t;

/** Fill opt with the default rules. */
void game_default_options(game_options_t *opt);

/**
 * Start in the attract state. n_leds is clamped to [GAME_MIN_LEDS,
 * GAME_MAX_LEDS]. opt may be NULL for the default rules.
 */
void game_init(game_t *g, int32_t n_leds, uint32_t seed, const game_options_t *opt);

/**
 * Change the rules. Values are clamped (level1_dots >= 1, dots_per_level
 * >= 0, speed 25..300 %). Takes effect at the next level or spawn, so call
 * it between games to keep a running game consistent.
 */
void game_set_options(game_t *g, const game_options_t *opt);

/** Stop a running game at once: back to the attract state, no score kept. */
void game_abort(game_t *g);

/** Advance the game. dt_ms is clamped to 1..50 ms. */
void game_tick(game_t *g, int32_t dt_ms);

/** A button was pressed: GAME_RED, GAME_YELLOW or GAME_BLUE. */
void game_press(game_t *g, uint8_t color);

/** Return the accumulated GAME_EV_* bits and clear them. */
uint32_t game_take_events(game_t *g);

/** Write the colour of every LED (n_leds entries) for the current state. */
void game_render(const game_t *g, game_rgb_t *out);

/** The default colour for these colour bits (white is kept dimmer to save power). */
game_rgb_t game_color_for_mask(uint8_t mask);

/* ---- Adjustable palette ------------------------------------------------- */

/** The colour masks in colour-wheel order, then white; for settings pages. */
extern const uint8_t game_palette_order[GAME_PALETTE_COLORS];

/** Change the colour shown for a mask (1..7). Takes effect at the next render. */
void game_set_color(game_t *g, uint8_t mask, game_rgb_t color);

/** The colour currently shown for a mask. */
game_rgb_t game_get_color(const game_t *g, uint8_t mask);

/** Back to the defaults of game_color_for_mask(). */
void game_reset_colors(game_t *g);

/**
 * Colour chart for tuning the palette: from LED 0, every colour in
 * game_palette_order as three LEDs on and two off. Writes n_leds entries.
 */
void game_render_palette(const game_t *g, game_rgb_t *out);

/**
 * The parameters of a level (1-based; past the table the last row). With a
 * game, its options apply: dots from level1_dots/dots_per_level and the
 * crossing time scaled by speed_pct. g may be NULL for the table values.
 */
void game_level_params(const game_t *g, int32_t level, game_level_t *out);

/** Number of dots currently on the strip. */
int32_t game_active_dots(const game_t *g);

const char *game_state_name(game_state_t state);

#ifdef __cplusplus
}
#endif
