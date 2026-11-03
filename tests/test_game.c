/*
 * Unit tests for the game rules. Build and run with `make test`.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "autoplay.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        failures++; \
        printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define ONE 256
#define TICK 16

static game_t g;
static game_rgb_t leds[GAME_MAX_LEDS];

static void run_ms(int32_t ms)
{
    while (ms > 0) {
        game_tick(&g, TICK);
        ms -= TICK;
    }
}

/* Tick until one of the events shows up, or the time runs out. */
static uint32_t run_until_event(uint32_t wanted, int32_t max_ms)
{
    uint32_t seen = 0;
    while (max_ms > 0) {
        game_tick(&g, TICK);
        seen |= game_take_events(&g);
        if (seen & wanted) return seen;
        max_ms -= TICK;
    }
    return seen;
}

static void setup_playing(int32_t n_leds)
{
    game_init(&g, n_leds, 42, NULL);
    g.state = GAME_STATE_PLAYING;
    g.state_ms = 0;
    g.level = 1;
    g.lives = GAME_MAX_LIVES;
    g.score = 0;
    g.dots_total = 1000;          /* the level never ends by itself */
    g.dots_spawned = 999;         /* ...and nothing spawns */
    g.spawn_timer_ms = 1 << 30;
}

static game_dot_t *place_dot(uint8_t mask, int32_t led, int32_t speed_leds_per_s)
{
    for (int32_t i = 0; i < GAME_MAX_DOTS; i++) {
        if (g.dots[i].active) continue;
        g.dots[i].active = true;
        g.dots[i].mask = mask;
        g.dots[i].colors_at_spawn = (uint8_t)((mask & 1) + ((mask >> 1) & 1) + ((mask >> 2) & 1));
        g.dots[i].pos = led * ONE;
        g.dots[i].speed = speed_leds_per_s * ONE;
        return &g.dots[i];
    }
    return NULL;
}

static int32_t lit_leds(void)
{
    int32_t n = 0;
    for (int32_t i = 0; i < g.n_leds; i++) {
        if (leds[i].r || leds[i].g || leds[i].b) n++;
    }
    return n;
}

static bool led_off(int32_t i) { return !leds[i].r && !leds[i].g && !leds[i].b; }

static void test_init_and_attract(void)
{
    printf("init and attract\n");
    game_init(&g, 300, 1, NULL);
    CHECK(g.state == GAME_STATE_ATTRACT);
    CHECK(g.n_leds == 300);
    game_render(&g, leds);
    CHECK(leds[0].r == 90 && leds[1].r == 90 && leds[2].r == 90);
    CHECK(led_off(3));
    CHECK(lit_leds() <= 3 + 6 * 4);

    game_init(&g, 5, 1, NULL);
    CHECK(g.n_leds == GAME_MIN_LEDS);
    game_init(&g, 5000, 1, NULL);
    CHECK(g.n_leds == GAME_MAX_LEDS);
    game_init(&g, 60, 0, NULL);
    CHECK(g.rng != 0);
}

static void test_start_countdown_and_first_dot(void)
{
    printf("start, countdown, first dot\n");
    game_init(&g, 60, 7, NULL);
    game_press(&g, GAME_BLUE);
    CHECK(g.state == GAME_STATE_COUNTDOWN);
    CHECK(game_take_events(&g) & GAME_EV_START);
    CHECK(g.level == 1 && g.lives == 3 && g.score == 0);

    game_render(&g, leds);
    CHECK(!led_off(GAME_PLAY_START));            /* the bar is full */
    run_ms(1500);
    game_render(&g, leds);
    CHECK(!led_off(GAME_PLAY_START));
    CHECK(led_off(GAME_PLAY_START + 19));         /* ...and already shrunk */
    CHECK(g.state == GAME_STATE_COUNTDOWN);
    run_ms(1600);
    CHECK(g.state == GAME_STATE_PLAYING);

    run_ms(500);
    CHECK(game_active_dots(&g) == 1);
    const game_dot_t *d = NULL;
    for (int32_t i = 0; i < GAME_MAX_DOTS; i++) if (g.dots[i].active) d = &g.dots[i];
    CHECK(d != NULL);
    if (d) {
        CHECK(d->mask == GAME_RED || d->mask == GAME_YELLOW || d->mask == GAME_BLUE);
        CHECK(d->pos > 50 * ONE && d->pos <= 60 * ONE);
        CHECK(d->colors_at_spawn == 1);
    }
    /* Level 1 allows one dot at a time. */
    run_ms(3000);
    CHECK(game_active_dots(&g) <= 1);
}

static void test_primary_hit(void)
{
    printf("primary colour hit\n");
    setup_playing(60);
    place_dot(GAME_RED, 30, 0);
    game_press(&g, GAME_RED);
    CHECK(game_take_events(&g) & GAME_EV_SHOT);
    uint32_t ev = run_until_event(GAME_EV_DOT_CLEARED | GAME_EV_WRONG_HIT, 2000);
    CHECK(ev & GAME_EV_DOT_CLEARED);
    CHECK(!(ev & GAME_EV_WRONG_HIT));
    CHECK(game_active_dots(&g) == 0);
    CHECK(g.score == 10);
    CHECK(g.lives == 3);
    game_render(&g, leds);
    CHECK(!led_off(30));                          /* the white flash */
}

static void test_mixed_hit(void)
{
    printf("mixed colour needs two hits\n");
    setup_playing(60);
    game_dot_t *d = place_dot(GAME_ORANGE, 30, 0);
    game_press(&g, GAME_YELLOW);
    uint32_t ev = run_until_event(GAME_EV_DOT_REDUCED | GAME_EV_DOT_CLEARED | GAME_EV_WRONG_HIT, 2000);
    CHECK((ev & (GAME_EV_DOT_REDUCED | GAME_EV_DOT_CLEARED | GAME_EV_WRONG_HIT)) == GAME_EV_DOT_REDUCED);
    CHECK(d->active && d->mask == GAME_RED);
    game_render(&g, leds);
    CHECK(leds[30].r == 255 && leds[30].g <= 190 && leds[30].b == 0); /* red dot + fading yellow flash */

    game_press(&g, GAME_RED);
    ev = run_until_event(GAME_EV_DOT_CLEARED | GAME_EV_WRONG_HIT, 2000);
    CHECK(ev & GAME_EV_DOT_CLEARED);
    CHECK(!d->active);
    CHECK(g.score == 20);                         /* two colours at spawn */
    CHECK(g.lives == 3);
}

static void test_white_needs_three(void)
{
    printf("white needs all three colours\n");
    setup_playing(300);
    game_dot_t *d = place_dot(GAME_WHITE, 200, 0);
    game_press(&g, GAME_BLUE);
    run_until_event(GAME_EV_DOT_REDUCED, 2000);
    CHECK(d->active && d->mask == GAME_ORANGE);
    game_press(&g, GAME_RED);
    run_until_event(GAME_EV_DOT_REDUCED, 2000);
    CHECK(d->active && d->mask == GAME_YELLOW);
    game_press(&g, GAME_YELLOW);
    uint32_t ev = run_until_event(GAME_EV_DOT_CLEARED, 2000);
    CHECK(ev & GAME_EV_DOT_CLEARED);
    CHECK(!d->active && g.score == 30);
}

static void test_wrong_hit(void)
{
    printf("wrong colour costs a life\n");
    setup_playing(60);
    game_dot_t *d = place_dot(GAME_ORANGE, 30, 0);
    game_press(&g, GAME_BLUE);
    uint32_t ev = run_until_event(GAME_EV_WRONG_HIT | GAME_EV_DOT_CLEARED | GAME_EV_DOT_REDUCED, 2000);
    CHECK(ev & GAME_EV_WRONG_HIT);
    CHECK(ev & GAME_EV_LIFE_LOST);
    CHECK(!(ev & (GAME_EV_DOT_CLEARED | GAME_EV_DOT_REDUCED)));
    CHECK(g.lives == 2);
    CHECK(!d->active);
    CHECK(g.score == 0);
    CHECK(g.zone_flash_ms > 0);
    game_render(&g, leds);
    CHECK(leds[3].r > 0);                         /* red feedback near the player */
    CHECK(led_off(GAME_PLAY_START + 40));         /* ...but not far away */

    printf("wrong colour, stricter option\n");
    game_options_t opt = { .wrong_hit_keeps_dot = true };
    game_init(&g, 60, 42, &opt);
    g.state = GAME_STATE_PLAYING;
    g.dots_total = 1000; g.dots_spawned = 999; g.spawn_timer_ms = 1 << 30;
    d = place_dot(GAME_ORANGE, 30, 0);
    game_press(&g, GAME_BLUE);
    ev = run_until_event(GAME_EV_WRONG_HIT, 2000);
    CHECK(ev & GAME_EV_WRONG_HIT);
    CHECK(g.lives == 2);
    CHECK(d->active && d->mask == GAME_ORANGE);
}

static void test_escape_and_game_over(void)
{
    printf("escaped dot costs a life, three losses end the game\n");
    setup_playing(60);
    place_dot(GAME_RED, 6, 20);
    uint32_t ev = run_until_event(GAME_EV_DOT_ESCAPED, 1000);
    CHECK(ev & GAME_EV_DOT_ESCAPED);
    CHECK(ev & GAME_EV_LIFE_LOST);
    CHECK(g.lives == 2);
    CHECK(game_active_dots(&g) == 0);
    CHECK(g.state == GAME_STATE_PLAYING);

    place_dot(GAME_BLUE, 6, 20);
    run_until_event(GAME_EV_DOT_ESCAPED, 1000);
    CHECK(g.lives == 1);
    place_dot(GAME_YELLOW, 6, 20);
    ev = run_until_event(GAME_EV_DOT_ESCAPED, 1000);
    CHECK(ev & GAME_EV_DOT_ESCAPED);
    CHECK(ev & GAME_EV_GAME_OVER);
    CHECK(g.state == GAME_STATE_GAME_OVER);
    CHECK(g.state_ms == 0);
    CHECK(g.lives == 0);
    CHECK(game_active_dots(&g) == 0);

    game_render(&g, leds);
    CHECK(leds[0].g == 0 && leds[1].g == 0 && leds[2].g == 0); /* no lives shown */
    run_ms(400);
    game_render(&g, leds);
    CHECK(leds[GAME_PLAY_START].r > 0);           /* red pulse near the player */
    CHECK(led_off(GAME_PLAY_START + 30));

    game_press(&g, GAME_RED);                     /* too early to restart */
    CHECK(g.state == GAME_STATE_GAME_OVER);
    run_ms(500);
    game_press(&g, GAME_RED);
    CHECK(g.state == GAME_STATE_COUNTDOWN);
    CHECK(g.lives == 3 && g.level == 1 && g.score == 0);

    /* Game over by a wrong hit discards the shots still in flight. */
    setup_playing(60);
    g.lives = 1;
    place_dot(GAME_ORANGE, 30, 0);
    game_press(&g, GAME_BLUE);
    game_press(&g, GAME_RED);
    ev = run_until_event(GAME_EV_WRONG_HIT, 2000);
    CHECK(ev & GAME_EV_GAME_OVER);
    CHECK(g.state == GAME_STATE_GAME_OVER);
    int32_t in_flight = 0;
    for (int32_t i = 0; i < GAME_MAX_SHOTS; i++) if (g.shots[i].active) in_flight++;
    CHECK(in_flight == 0);

    /* Without a press the game goes back to attract. */
    setup_playing(60);
    g.lives = 1;
    place_dot(GAME_RED, 5, 30);
    run_until_event(GAME_EV_GAME_OVER, 1000);
    run_ms(2000);
    game_render(&g, leds);
    CHECK(g.state == GAME_STATE_GAME_OVER);
    CHECK(leds[GAME_PLAY_START].r == 255 || led_off(GAME_PLAY_START)); /* level 1 blinking */
    run_ms(4100);
    CHECK(g.state == GAME_STATE_ATTRACT);
}

static void test_level_clear(void)
{
    printf("level clear: bonus, life back, next level\n");
    setup_playing(60);
    g.lives = 2;
    g.dots_total = 5;
    g.dots_spawned = 5;
    game_tick(&g, TICK);
    CHECK(g.state == GAME_STATE_LEVEL_CLEAR);
    CHECK(game_take_events(&g) & GAME_EV_LEVEL_CLEAR);
    CHECK(g.score == 50 * 1 + 25 * 2);
    CHECK(g.lives == 3);
    run_ms(700);
    game_render(&g, leds);
    CHECK(lit_leds() <= 3 + 12);                  /* a short rainbow comet */
    run_ms(1000);
    game_render(&g, leds);
    CHECK(lit_leds() <= 3 + 2);                   /* "level 2" blinking */
    run_ms(1000);
    CHECK(g.state == GAME_STATE_COUNTDOWN);
    CHECK(g.level == 2);
    run_ms(2100);
    CHECK(g.state == GAME_STATE_PLAYING);
    game_level_t p;
    game_level_params(2, &p);
    CHECK(g.dots_total == p.dots);
    CHECK(g.dots_spawned == 0);
}

static void test_level_params(void)
{
    printf("level table\n");
    game_level_t a, b, c;
    game_level_params(0, &a);
    game_level_params(1, &b);
    game_level_params(99, &c);
    CHECK(a.dots == b.dots && a.cross_ms == 7000);
    CHECK(c.cross_ms == 2200 && c.max_concurrent == 6);
    for (int32_t l = 1; l < GAME_LEVELS; l++) {
        game_level_t cur, nxt;
        game_level_params(l, &cur);
        game_level_params(l + 1, &nxt);
        CHECK(nxt.cross_ms < cur.cross_ms);
        CHECK(nxt.dots >= cur.dots);
        CHECK(nxt.mixed_pct + nxt.triple_pct >= cur.mixed_pct + cur.triple_pct);
        CHECK(cur.gap_min_ms <= cur.gap_max_ms);
        CHECK(cur.max_concurrent <= GAME_MAX_DOTS);
    }
}

static void test_shot_miss_and_limits(void)
{
    printf("a missed shot costs nothing\n");
    setup_playing(60);
    game_press(&g, GAME_RED);
    game_press(&g, 7);                            /* not a button */
    run_ms(2000);
    CHECK(g.lives == 3);
    int32_t shots = 0;
    for (int32_t i = 0; i < GAME_MAX_SHOTS; i++) if (g.shots[i].active) shots++;
    CHECK(shots == 0);

    for (int32_t i = 0; i < GAME_MAX_SHOTS + 5; i++) game_press(&g, GAME_BLUE);
    shots = 0;
    for (int32_t i = 0; i < GAME_MAX_SHOTS; i++) if (g.shots[i].active) shots++;
    CHECK(shots == GAME_MAX_SHOTS);
}

static void test_rendering_limits(void)
{
    printf("rendering: dark line, clipping, few LEDs at once\n");
    setup_playing(300);
    place_dot(GAME_GREEN, 4, 0);
    game_render(&g, leds);
    CHECK(led_off(3));
    CHECK(leds[4].g == 255 && leds[5].g == 255 && leds[6].g == 255 && led_off(7));

    /* Half a LED over the line: the part below LED 4 is simply not drawn. */
    g.dots[0].pos = 4 * ONE - 128;
    game_render(&g, leds);
    CHECK(led_off(3));
    CHECK(leds[4].g == 255 && leds[6].g == 127);

    int32_t max_lit = 0, max_level = 0, games = 0;
    int32_t presses = 0;
    bool line_dark = true;
    game_init(&g, 300, 2024, NULL);
    for (int32_t tick = 0; tick < 20000; tick++) {
        autoplay_step(&g, tick, &presses);
        game_tick(&g, TICK);
        uint32_t ev = game_take_events(&g);
        if (ev & GAME_EV_START) games++;
        if (g.level > max_level) max_level = g.level;
        game_render(&g, leds);
        int32_t lit = lit_leds();
        if (lit > max_lit) max_lit = lit;
        if (g.state == GAME_STATE_PLAYING && g.zone_flash_ms == 0 && !led_off(3)) line_dark = false;
    }
    printf("  (at most %d of 300 LEDs lit at once over 20000 ticks; %d games, best level %d)\n",
           (int)max_lit, (int)games, (int)max_level);
    CHECK(max_lit <= 100);
    CHECK(line_dark);
    CHECK(max_level >= 2);                        /* the scripted player gets somewhere */
}

static void test_determinism(void)
{
    printf("same seed and inputs give the same LEDs\n");
    static game_t g2;
    static game_rgb_t leds2[GAME_MAX_LEDS];
    game_init(&g, 300, 555, NULL);
    game_init(&g2, 300, 555, NULL);
    int32_t p1 = 0, p2 = 0;
    bool same = true;
    for (int32_t tick = 0; tick < 5000 && same; tick++) {
        autoplay_step(&g, tick, &p1);
        autoplay_step(&g2, tick, &p2);
        game_tick(&g, TICK);
        game_tick(&g2, TICK);
        game_render(&g, leds);
        game_render(&g2, leds2);
        if (memcmp(leds, leds2, sizeof(game_rgb_t) * 300) != 0) same = false;
        if (g.score != g2.score || g.state != g2.state) same = false;
    }
    CHECK(same);
    CHECK(fnv1a_leds(leds, 300) == fnv1a_leds(leds2, 300));
}

int main(void)
{
    test_init_and_attract();
    test_start_countdown_and_first_dot();
    test_primary_hit();
    test_mixed_hit();
    test_white_needs_three();
    test_wrong_hit();
    test_escape_and_game_over();
    test_level_clear();
    test_level_params();
    test_shot_miss_and_limits();
    test_rendering_limits();
    test_determinism();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
