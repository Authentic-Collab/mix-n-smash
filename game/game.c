/*
 * Mix 'n Smash game rules. See game.h for the overview.
 *
 * Keep this file free of floating point, library calls other than memset,
 * and anything platform specific: web/game.js mirrors it statement by
 * statement, and tests/trace compares the two.
 */
#include "game.h"
#include <string.h>

#define ONE 256                        /* positions are LED index * 256 */

enum {
    COUNTDOWN_FIRST_MS      = 3000,
    COUNTDOWN_NEXT_MS       = 2000,
    LEVEL_CLEAR_MS          = 2600,
    LEVEL_CLEAR_RAINBOW_MS  = 1400,
    GAME_OVER_MS            = 6000,
    GAME_OVER_PULSE_MS      = 1600,
    GAME_OVER_PULSE_PERIOD  = 800,
    GAME_OVER_RESTART_MS    = 800,    /* a button restarts after this */
    BLINK_MS                = 300,
    SHOT_CROSS_MS           = 900,    /* a shot crosses the playfield in this time */
    FIRST_SPAWN_MS          = 400,
    DT_MAX_MS               = 50,
    FLASH_CLEAR_MS          = 260,
    FLASH_REDUCE_MS         = 180,
    ZONE_FLASH_WRONG_MS     = 350,
    ZONE_FLASH_ESCAPE_MS    = 450,
    ATTRACT_COMETS          = 6,
    ATTRACT_PERIOD_MS       = 1500,   /* a comet moves one spacing in this time */
    COMET_TAIL              = 4,
    ZONE_MAX_LEDS           = 20,     /* feedback zone next to the player */
    RAINBOW_MAX_LEDS        = 12,
    SPAWN_SPACING_LEDS      = 4,      /* free LEDs needed at the far end to spawn */
    POINTS_PER_COLOR        = 10,
};

/* dots, cross_ms, mixed%, triple%, max_concurrent, gap_min_ms, gap_max_ms, speed_var% */
static const game_level_t LEVELS[GAME_LEVELS] = {
    { 8, 7000,  0,  0, 1, 1500, 2500,  0},
    {10, 6000, 25,  0, 2, 1300, 2300,  0},
    {12, 5200, 40,  0, 2, 1100, 2000,  0},
    {14, 4600, 50,  0, 3,  950, 1800,  0},
    {16, 4000, 55, 10, 3,  850, 1600, 10},
    {18, 3500, 60, 15, 4,  750, 1400, 15},
    {20, 3100, 65, 20, 4,  650, 1200, 20},
    {22, 2800, 70, 25, 5,  600, 1100, 20},
    {24, 2500, 70, 30, 5,  550, 1000, 25},
    {26, 2200, 75, 35, 6,  500,  900, 25},
};

/* Paint-mixing colour wheel, used by the attract comets and the rainbow. */
static const uint8_t WHEEL[6] = {
    GAME_RED, GAME_ORANGE, GAME_YELLOW, GAME_GREEN, GAME_BLUE, GAME_PURPLE
};
static const uint8_t PRIMARIES[3] = {GAME_RED, GAME_YELLOW, GAME_BLUE};
static const uint8_t SECONDARIES[3] = {GAME_ORANGE, GAME_PURPLE, GAME_GREEN};

static const game_rgb_t COL_OFF       = {0, 0, 0};
static const game_rgb_t COL_LIFE      = {90, 90, 90};
static const game_rgb_t COL_COUNTDOWN = {80, 80, 80};
static const game_rgb_t COL_LEVEL     = {255, 255, 255};
static const game_rgb_t COL_FLASH_RED = {255, 0, 0};
static const game_rgb_t COL_CLEARED   = {255, 255, 255};

/* ---- Small helpers ------------------------------------------------------ */

static int32_t imin(int32_t a, int32_t b) { return a < b ? a : b; }
static int32_t imax(int32_t a, int32_t b) { return a > b ? a : b; }

/* Integer interpolation a..b at k/n. C truncates towards zero; so does the JS port. */
static int32_t lerp_i(int32_t a, int32_t b, int32_t k, int32_t n)
{
    return a + (b - a) * k / n;
}

static int32_t popcount3(uint8_t m)
{
    return (m & 1) + ((m >> 1) & 1) + ((m >> 2) & 1);
}

static uint32_t rng_next(game_t *g)
{
    uint32_t x = g->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g->rng = x;
    return x;
}

/* Uniform-ish in 0..n-1 (n > 0). */
static int32_t rng_range(game_t *g, int32_t n)
{
    return (int32_t)(rng_next(g) % (uint32_t)n);
}

static int32_t play_len(const game_t *g) { return g->n_leds - GAME_PLAY_START; }

/* Dots are longer on dense strips so they stay visible from a distance. */
static int32_t dot_len(const game_t *g)
{
    return g->n_leds >= 200 ? 3 : g->n_leds >= 100 ? 2 : 1;
}

static int32_t zone_len(const game_t *g)
{
    return imin(ZONE_MAX_LEDS, play_len(g) / 3);
}

static game_rgb_t scale(game_rgb_t c, int32_t num, int32_t den)
{
    game_rgb_t r = {
        (uint8_t)(c.r * num / den),
        (uint8_t)(c.g * num / den),
        (uint8_t)(c.b * num / den),
    };
    return r;
}

/* ---- Public colour table ------------------------------------------------ */

game_rgb_t game_color_for_mask(uint8_t mask)
{
    switch (mask) {
    case GAME_RED:    { game_rgb_t c = {255, 0, 0};     return c; }
    case GAME_YELLOW: { game_rgb_t c = {255, 190, 0};   return c; }
    case GAME_BLUE:   { game_rgb_t c = {0, 0, 255};     return c; }
    case GAME_ORANGE: { game_rgb_t c = {255, 70, 0};    return c; }
    case GAME_PURPLE: { game_rgb_t c = {170, 0, 255};   return c; }
    case GAME_GREEN:  { game_rgb_t c = {0, 255, 0};     return c; }
    case GAME_WHITE:  { game_rgb_t c = {200, 200, 200}; return c; }  /* three channels: kept dimmer */
    default:          return COL_OFF;
    }
}

const uint8_t game_palette_order[GAME_PALETTE_COLORS] = {
    GAME_RED, GAME_ORANGE, GAME_YELLOW, GAME_GREEN, GAME_BLUE, GAME_PURPLE, GAME_WHITE
};

/* The colour shown for a mask, from the adjustable palette. */
static game_rgb_t pal(const game_t *g, uint8_t mask)
{
    return g->palette[mask & 7];
}

void game_set_color(game_t *g, uint8_t mask, game_rgb_t color)
{
    if (mask >= 1 && mask <= 7) g->palette[mask] = color;
}

game_rgb_t game_get_color(const game_t *g, uint8_t mask)
{
    return pal(g, mask);
}

void game_reset_colors(game_t *g)
{
    for (int32_t m = 0; m < 8; m++) g->palette[m] = game_color_for_mask((uint8_t)m);
}

void game_level_params(int32_t level, game_level_t *out)
{
    int32_t idx = level - 1;
    if (idx < 0) idx = 0;
    if (idx >= GAME_LEVELS) idx = GAME_LEVELS - 1;
    *out = LEVELS[idx];
}

const char *game_state_name(game_state_t state)
{
    switch (state) {
    case GAME_STATE_ATTRACT:     return "attract";
    case GAME_STATE_COUNTDOWN:   return "countdown";
    case GAME_STATE_PLAYING:     return "playing";
    case GAME_STATE_LEVEL_CLEAR: return "level_clear";
    case GAME_STATE_GAME_OVER:   return "game_over";
    default:                     return "?";
    }
}

int32_t game_active_dots(const game_t *g)
{
    int32_t n = 0;
    for (int32_t i = 0; i < GAME_MAX_DOTS; i++) {
        if (g->dots[i].active) n++;
    }
    return n;
}

/* ---- State changes ------------------------------------------------------ */

static void set_state(game_t *g, game_state_t state)
{
    g->state = state;
    g->state_ms = 0;
}

static void clear_shots(game_t *g) { memset(g->shots, 0, sizeof(g->shots)); }
static void clear_dots(game_t *g)  { memset(g->dots, 0, sizeof(g->dots)); }

static void clear_field(game_t *g)
{
    clear_dots(g);
    clear_shots(g);
    memset(g->flashes, 0, sizeof(g->flashes));
    g->zone_flash_ms = 0;
    g->zone_flash_total_ms = 0;
}

static void enter_attract(game_t *g)
{
    clear_field(g);
    set_state(g, GAME_STATE_ATTRACT);
}

static void start_countdown(game_t *g, int32_t ms)
{
    clear_field(g);
    g->countdown_ms = ms;
    set_state(g, GAME_STATE_COUNTDOWN);
}

static void start_level(game_t *g)
{
    game_level_t p;
    game_level_params(g->level, &p);
    clear_dots(g);
    clear_shots(g);
    g->dots_spawned = 0;
    g->dots_total = p.dots;
    g->spawn_timer_ms = FIRST_SPAWN_MS;
    set_state(g, GAME_STATE_PLAYING);
}

static void new_game(game_t *g)
{
    g->level = 1;
    g->lives = GAME_MAX_LIVES;
    g->score = 0;
    g->events |= GAME_EV_START;
    start_countdown(g, COUNTDOWN_FIRST_MS);
}

static void zone_flash(game_t *g, int32_t ms)
{
    g->zone_flash_ms = ms;
    g->zone_flash_total_ms = ms;
}

static void add_flash(game_t *g, int32_t led, game_rgb_t color, int32_t ms)
{
    /* Take a free slot, or the one closest to fading out. */
    int32_t best = 0;
    for (int32_t i = 0; i < GAME_MAX_FLASHES; i++) {
        if (!g->flashes[i].active) { best = i; break; }
        if (g->flashes[i].ttl_ms < g->flashes[best].ttl_ms) best = i;
    }
    g->flashes[best].active = true;
    g->flashes[best].led = led;
    g->flashes[best].color = color;
    g->flashes[best].ttl_ms = ms;
    g->flashes[best].total_ms = ms;
}

static void lose_life(game_t *g, int32_t flash_ms)
{
    g->lives--;
    g->events |= GAME_EV_LIFE_LOST;
    zone_flash(g, flash_ms);
    if (g->lives <= 0) {
        g->lives = 0;
        clear_dots(g);
        clear_shots(g);
        g->events |= GAME_EV_GAME_OVER;
        set_state(g, GAME_STATE_GAME_OVER);
    }
}

/* ---- Playing ------------------------------------------------------------ */

static void spawn_dot(game_t *g)
{
    game_level_t p, next;
    game_level_params(g->level, &p);
    game_level_params(g->level + 1, &next);

    /* The speed creeps from this level's value to the next level's. */
    int32_t cross_ms = lerp_i(p.cross_ms, next.cross_ms, g->dots_spawned, g->dots_total);
    int32_t speed = play_len(g) * ONE * 1000 / cross_ms;
    if (p.speed_var_pct > 0) {
        int32_t pct = 100 - p.speed_var_pct + rng_range(g, 2 * p.speed_var_pct + 1);
        speed = speed * pct / 100;
    }

    uint8_t mask;
    int32_t r = rng_range(g, 100);
    if (r < p.triple_pct) {
        mask = GAME_WHITE;
    } else if (r < p.triple_pct + p.mixed_pct) {
        mask = SECONDARIES[rng_range(g, 3)];
    } else {
        mask = PRIMARIES[rng_range(g, 3)];
    }

    for (int32_t i = 0; i < GAME_MAX_DOTS; i++) {
        game_dot_t *d = &g->dots[i];
        if (d->active) continue;
        d->active = true;
        d->mask = mask;
        d->colors_at_spawn = (uint8_t)popcount3(mask);
        d->pos = g->n_leds * ONE;         /* slides in from beyond the end */
        d->speed = speed;
        break;
    }
    g->dots_spawned++;
    g->spawn_timer_ms = p.gap_min_ms + rng_range(g, p.gap_max_ms - p.gap_min_ms + 1);
}

static bool spawn_point_free(const game_t *g)
{
    int32_t limit = (g->n_leds - SPAWN_SPACING_LEDS - dot_len(g)) * ONE;
    for (int32_t i = 0; i < GAME_MAX_DOTS; i++) {
        if (g->dots[i].active && g->dots[i].pos > limit) return false;
    }
    return true;
}

static void resolve_hit(game_t *g, game_shot_t *s, game_dot_t *d)
{
    int32_t led = d->pos / ONE;
    if (d->mask & s->mask) {
        d->mask &= (uint8_t)~s->mask;
        if (d->mask == 0) {
            g->score += (uint32_t)(POINTS_PER_COLOR * d->colors_at_spawn * g->level);
            add_flash(g, led, COL_CLEARED, FLASH_CLEAR_MS);
            g->events |= GAME_EV_DOT_CLEARED;
            d->active = false;
        } else {
            add_flash(g, led, pal(g, s->mask), FLASH_REDUCE_MS);
            g->events |= GAME_EV_DOT_REDUCED;
        }
    } else {
        g->events |= GAME_EV_WRONG_HIT;
        add_flash(g, led, COL_FLASH_RED, FLASH_REDUCE_MS);
        if (!g->opt.wrong_hit_keeps_dot) d->active = false;
        lose_life(g, ZONE_FLASH_WRONG_MS);
    }
}

static void tick_playing(game_t *g, int32_t dt)
{
    game_level_t p;
    game_level_params(g->level, &p);

    g->spawn_timer_ms -= dt;
    if (g->dots_spawned < g->dots_total && g->spawn_timer_ms <= 0 &&
        game_active_dots(g) < p.max_concurrent && spawn_point_free(g)) {
        spawn_dot(g);
    }

    int32_t end = g->n_leds * ONE;
    for (int32_t i = 0; i < GAME_MAX_SHOTS; i++) {
        game_shot_t *s = &g->shots[i];
        if (!s->active) continue;
        s->pos += s->speed * dt / 1000;
        if (s->pos >= end) s->active = false;
    }
    for (int32_t i = 0; i < GAME_MAX_DOTS; i++) {
        game_dot_t *d = &g->dots[i];
        if (d->active) d->pos -= d->speed * dt / 1000;
    }

    /* Each shot hits the nearest dot whose front it has reached. */
    for (int32_t i = 0; i < GAME_MAX_SHOTS; i++) {
        game_shot_t *s = &g->shots[i];
        if (!s->active) continue;
        int32_t head = s->pos + ONE;
        game_dot_t *hit = NULL;
        for (int32_t j = 0; j < GAME_MAX_DOTS; j++) {
            game_dot_t *d = &g->dots[j];
            if (!d->active || d->pos > head) continue;
            if (hit == NULL || d->pos < hit->pos) hit = d;
        }
        if (hit == NULL) continue;
        s->active = false;
        resolve_hit(g, s, hit);
        if (g->state != GAME_STATE_PLAYING) return;
    }

    /* Dots that cross the player's line. */
    for (int32_t i = 0; i < GAME_MAX_DOTS; i++) {
        game_dot_t *d = &g->dots[i];
        if (!d->active || d->pos >= GAME_PLAY_START * ONE) continue;
        d->active = false;
        g->events |= GAME_EV_DOT_ESCAPED;
        lose_life(g, ZONE_FLASH_ESCAPE_MS);
        if (g->state != GAME_STATE_PLAYING) return;
    }

    if (g->dots_spawned >= g->dots_total && game_active_dots(g) == 0) {
        g->score += (uint32_t)(50 * g->level + 25 * g->lives);
        g->lives = imin(g->lives + 1, GAME_MAX_LIVES);
        clear_shots(g);
        g->events |= GAME_EV_LEVEL_CLEAR;
        set_state(g, GAME_STATE_LEVEL_CLEAR);
    }
}

/* ---- Public API --------------------------------------------------------- */

void game_init(game_t *g, int32_t n_leds, uint32_t seed, const game_options_t *opt)
{
    memset(g, 0, sizeof(*g));
    g->n_leds = imax(GAME_MIN_LEDS, imin(GAME_MAX_LEDS, n_leds));
    if (opt) g->opt = *opt;
    g->rng = seed ? seed : 0x9E3779B9u;
    g->lives = GAME_MAX_LIVES;
    g->level = 1;
    game_reset_colors(g);
    enter_attract(g);
}

void game_tick(game_t *g, int32_t dt_ms)
{
    int32_t dt = imax(1, imin(DT_MAX_MS, dt_ms));
    g->time_ms += (uint32_t)dt;
    g->state_ms += dt;

    if (g->zone_flash_ms > 0) g->zone_flash_ms = imax(0, g->zone_flash_ms - dt);
    for (int32_t i = 0; i < GAME_MAX_FLASHES; i++) {
        game_flash_t *f = &g->flashes[i];
        if (!f->active) continue;
        f->ttl_ms -= dt;
        if (f->ttl_ms <= 0) f->active = false;
    }

    switch (g->state) {
    case GAME_STATE_ATTRACT:
        break;
    case GAME_STATE_COUNTDOWN:
        if (g->state_ms >= g->countdown_ms) start_level(g);
        break;
    case GAME_STATE_PLAYING:
        tick_playing(g, dt);
        break;
    case GAME_STATE_LEVEL_CLEAR:
        if (g->state_ms >= LEVEL_CLEAR_MS) {
            g->level++;
            start_countdown(g, COUNTDOWN_NEXT_MS);
        }
        break;
    case GAME_STATE_GAME_OVER:
        if (g->state_ms >= GAME_OVER_MS) enter_attract(g);
        break;
    }
}

void game_press(game_t *g, uint8_t color)
{
    if (color != GAME_RED && color != GAME_YELLOW && color != GAME_BLUE) return;

    switch (g->state) {
    case GAME_STATE_ATTRACT:
        new_game(g);
        break;
    case GAME_STATE_GAME_OVER:
        if (g->state_ms >= GAME_OVER_RESTART_MS) new_game(g);
        break;
    case GAME_STATE_PLAYING:
        for (int32_t i = 0; i < GAME_MAX_SHOTS; i++) {
            game_shot_t *s = &g->shots[i];
            if (s->active) continue;
            s->active = true;
            s->mask = color;
            s->pos = GAME_PLAY_START * ONE;
            s->speed = play_len(g) * ONE * 1000 / SHOT_CROSS_MS;
            g->events |= GAME_EV_SHOT;
            break;
        }
        break;
    default:
        break;
    }
}

uint32_t game_take_events(game_t *g)
{
    uint32_t ev = g->events;
    g->events = 0;
    return ev;
}

/* ---- Rendering ---------------------------------------------------------- */

static void add_px(const game_t *g, game_rgb_t *out, int32_t led, game_rgb_t c, int32_t cov)
{
    if (led < 0 || led >= g->n_leds || cov <= 0) return;
    if (cov > ONE) cov = ONE;
    int32_t r = out[led].r + c.r * cov / ONE;
    int32_t gg = out[led].g + c.g * cov / ONE;
    int32_t b = out[led].b + c.b * cov / ONE;
    out[led].r = (uint8_t)imin(255, r);
    out[led].g = (uint8_t)imin(255, gg);
    out[led].b = (uint8_t)imin(255, b);
}

/* Anti-aliased segment [a, b) in LED*256 units, limited to LEDs lo..hi. */
static void add_segment(const game_t *g, game_rgb_t *out, int32_t a, int32_t b,
                        game_rgb_t c, int32_t lo, int32_t hi)
{
    if (b <= a) return;
    int32_t first = imax(lo, a / ONE);
    int32_t last = imin(hi, (b - 1) / ONE);
    for (int32_t i = first; i <= last; i++) {
        int32_t s = imax(a, i * ONE);
        int32_t e = imin(b, (i + 1) * ONE);
        add_px(g, out, i, c, e - s);
    }
}

static void render_lives(const game_t *g, game_rgb_t *out)
{
    int32_t lives = g->state == GAME_STATE_ATTRACT ? GAME_MAX_LIVES : g->lives;
    for (int32_t i = 0; i < GAME_LIVES_LEDS && i < lives; i++) out[i] = COL_LIFE;
}

static void render_attract(const game_t *g, game_rgb_t *out)
{
    int32_t len = play_len(g) * ONE;
    int32_t spacing = len / ATTRACT_COMETS;
    int32_t phase = (int32_t)(g->time_ms % ATTRACT_PERIOD_MS) * spacing / ATTRACT_PERIOD_MS;
    for (int32_t j = 0; j < ATTRACT_COMETS; j++) {
        int32_t head = GAME_PLAY_START * ONE + (j * spacing + (len - phase)) % len;
        game_rgb_t c = scale(pal(g, WHEEL[j]), 6, 10);
        int32_t led = head / ONE;
        for (int32_t k = 0; k < COMET_TAIL; k++) {
            if (led + k >= g->n_leds) break;
            add_px(g, out, led + k, c, (COMET_TAIL - k) * ONE / COMET_TAIL);
        }
    }
}

static void render_countdown(const game_t *g, game_rgb_t *out)
{
    int32_t bar = zone_len(g) * ONE;
    int32_t remaining = imax(0, g->countdown_ms - g->state_ms);
    int32_t lit = bar * remaining / g->countdown_ms;
    int32_t a = GAME_PLAY_START * ONE;
    add_segment(g, out, a, a + lit, COL_COUNTDOWN, GAME_PLAY_START, g->n_leds - 1);
}

static void render_playing(const game_t *g, game_rgb_t *out)
{
    int32_t lo = GAME_PLAY_START, hi = g->n_leds - 1;
    int32_t dl = dot_len(g) * ONE;
    for (int32_t i = 0; i < GAME_MAX_DOTS; i++) {
        const game_dot_t *d = &g->dots[i];
        if (!d->active) continue;
        add_segment(g, out, d->pos, d->pos + dl, pal(g, d->mask), lo, hi);
    }
    for (int32_t i = 0; i < GAME_MAX_SHOTS; i++) {
        const game_shot_t *s = &g->shots[i];
        if (!s->active) continue;
        game_rgb_t c = pal(g, s->mask);
        add_segment(g, out, s->pos, s->pos + ONE, c, lo, hi);
        add_segment(g, out, s->pos - ONE, s->pos, scale(c, 4, 10), lo, hi);
    }
}

static void render_level_number(const game_t *g, game_rgb_t *out, int32_t level, int32_t t_ms)
{
    if ((t_ms / BLINK_MS) % 2 != 0) return;
    int32_t n = imin(level, play_len(g));
    for (int32_t i = 0; i < n; i++) out[GAME_PLAY_START + i] = COL_LEVEL;
}

static void render_level_clear(const game_t *g, game_rgb_t *out)
{
    if (g->state_ms < LEVEL_CLEAR_RAINBOW_MS) {
        /* A rainbow comet runs away from the player. */
        int32_t len = imin(RAINBOW_MAX_LEDS, play_len(g) / 4);
        int32_t head = GAME_PLAY_START + play_len(g) * g->state_ms / LEVEL_CLEAR_RAINBOW_MS;
        for (int32_t k = 0; k < len; k++) {
            int32_t led = head - k;
            if (led < GAME_PLAY_START || led >= g->n_leds) continue;
            game_rgb_t c = pal(g, WHEEL[(k * 6 / len) % 6]);
            add_px(g, out, led, c, (len - k) * ONE / len);
        }
    } else {
        render_level_number(g, out, g->level + 1, g->state_ms - LEVEL_CLEAR_RAINBOW_MS);
    }
}

static void render_game_over(const game_t *g, game_rgb_t *out)
{
    if (g->state_ms < GAME_OVER_PULSE_MS) {
        int32_t v = g->state_ms % GAME_OVER_PULSE_PERIOD;
        int32_t half = GAME_OVER_PULSE_PERIOD / 2;
        int32_t inten = v < half ? v * ONE / half : (GAME_OVER_PULSE_PERIOD - v) * ONE / half;
        int32_t n = GAME_PLAY_START + zone_len(g);
        for (int32_t i = 0; i < n; i++) add_px(g, out, i, COL_FLASH_RED, inten);
    } else {
        render_level_number(g, out, g->level, g->state_ms - GAME_OVER_PULSE_MS);
    }
}

static void render_overlays(const game_t *g, game_rgb_t *out)
{
    if (g->zone_flash_ms > 0) {
        int32_t inten = g->zone_flash_ms * ONE / g->zone_flash_total_ms;
        int32_t n = GAME_PLAY_START + zone_len(g);
        for (int32_t i = 0; i < n; i++) add_px(g, out, i, COL_FLASH_RED, inten);
    }
    for (int32_t i = 0; i < GAME_MAX_FLASHES; i++) {
        const game_flash_t *f = &g->flashes[i];
        if (!f->active) continue;
        int32_t inten = f->ttl_ms * ONE / f->total_ms;
        add_px(g, out, f->led, f->color, inten);
        if (f->led - 1 >= GAME_PLAY_START) add_px(g, out, f->led - 1, f->color, inten / 2);
        add_px(g, out, f->led + 1, f->color, inten / 2);
    }
}

void game_render(const game_t *g, game_rgb_t *out)
{
    memset(out, 0, sizeof(game_rgb_t) * (size_t)g->n_leds);
    render_lives(g, out);
    switch (g->state) {
    case GAME_STATE_ATTRACT:     render_attract(g, out); break;
    case GAME_STATE_COUNTDOWN:   render_countdown(g, out); break;
    case GAME_STATE_PLAYING:     render_playing(g, out); break;
    case GAME_STATE_LEVEL_CLEAR: render_level_clear(g, out); break;
    case GAME_STATE_GAME_OVER:   render_game_over(g, out); break;
    }
    render_overlays(g, out);
}

void game_render_palette(const game_t *g, game_rgb_t *out)
{
    memset(out, 0, sizeof(game_rgb_t) * (size_t)g->n_leds);
    for (int32_t i = 0; i < GAME_PALETTE_COLORS; i++) {
        game_rgb_t c = pal(g, game_palette_order[i]);
        for (int32_t k = 0; k < 3; k++) {
            int32_t led = i * 5 + k;              /* three on, two off */
            if (led < g->n_leds) out[led] = c;
        }
    }
}
