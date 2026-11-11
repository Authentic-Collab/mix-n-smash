/*
 * Prints a deterministic trace of a few scripted games: one line per tick
 * with the state, level, lives, score, events and a hash of the LEDs.
 * trace.js prints the same from the JavaScript port; `make trace` compares.
 */
#include <stdio.h>
#include "autoplay.h"

typedef struct {
    int32_t n_leds;
    uint32_t seed;
    bool keeps_dot;
    int32_t ticks;
} trace_config_t;

static const trace_config_t CONFIGS[] = {
    {60, 1, false, 6000},
    {300, 12345, false, 6000},
    {300, 777, true, 4000},
    {16, 99, false, 2000},
};

int main(void)
{
    static game_t g;
    static game_rgb_t leds[GAME_MAX_LEDS];
    for (size_t c = 0; c < sizeof(CONFIGS) / sizeof(CONFIGS[0]); c++) {
        const trace_config_t *cfg = &CONFIGS[c];
        game_options_t opt;
        game_default_options(&opt);
        opt.wrong_hit_keeps_dot = cfg->keeps_dot;
        if (c == 3) {
            opt.level1_dots = 3;
            opt.dots_per_level = 1;
            opt.speed_pct = 60;
        }
        game_init(&g, cfg->n_leds, cfg->seed, &opt);
        if (c == 2) {
            game_rgb_t purple = {120, 0, 200};   /* a custom colour must show up in the frames */
            game_set_color(&g, GAME_PURPLE, purple);
        }
        int32_t presses = 0;
        for (int32_t tick = 0; tick < cfg->ticks; tick++) {
            if (c == 1 && tick == 1234) game_abort(&g);
            autoplay_step(&g, tick, &presses);
            game_tick(&g, 16);
            uint32_t ev = game_take_events(&g);
            game_render(&g, leds);
            printf("%zu %d %s %d %d %u %u %08x\n", c, (int)tick, game_state_name(g.state),
                   (int)g.level, (int)g.lives, (unsigned)g.score, (unsigned)ev,
                   (unsigned)fnv1a_leds(leds, g.n_leds));
        }
        game_render_palette(&g, leds);
        printf("%zu palette %08x\n", c, (unsigned)fnv1a_leds(leds, g.n_leds));
    }
    return 0;
}
