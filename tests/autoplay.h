/*
 * A scripted player used by the trace and by the tests: it fires at the
 * nearest dot, and every 11th shot is deliberately wrong. It also presses a
 * button now and then while idle, so games start and restart. The same
 * routine lives in trace.js; keep them identical.
 */
#pragma once
#include "game.h"

static inline uint8_t lowest_bit(uint8_t mask)
{
    return (mask & 1) ? 1 : (mask & 2) ? 2 : 4;
}

static inline void autoplay_step(game_t *g, int32_t tick, int32_t *presses)
{
    if ((g->state == GAME_STATE_ATTRACT ||
         (g->state == GAME_STATE_GAME_OVER && g->state_ms >= 800)) && tick % 50 == 0) {
        game_press(g, GAME_RED);
        return;
    }
    if (g->state != GAME_STATE_PLAYING || tick % 7 != 0) return;

    const game_dot_t *nearest = NULL;
    for (int32_t i = 0; i < GAME_MAX_DOTS; i++) {
        const game_dot_t *d = &g->dots[i];
        if (!d->active) continue;
        if (nearest == NULL || d->pos < nearest->pos) nearest = d;
    }
    if (nearest == NULL || nearest->pos >= g->n_leds * 256 * 3 / 4) return;

    uint8_t color = lowest_bit(nearest->mask);
    (*presses)++;
    if (*presses % 11 == 0 && nearest->mask != GAME_WHITE) {
        if (!(nearest->mask & GAME_RED)) color = GAME_RED;
        else if (!(nearest->mask & GAME_YELLOW)) color = GAME_YELLOW;
        else color = GAME_BLUE;
    }
    game_press(g, color);
}

static inline uint32_t fnv1a_leds(const game_rgb_t *leds, int32_t n)
{
    uint32_t h = 2166136261u;
    for (int32_t i = 0; i < n; i++) {
        h = (h ^ leds[i].r) * 16777619u;
        h = (h ^ leds[i].g) * 16777619u;
        h = (h ^ leds[i].b) * 16777619u;
    }
    return h;
}
