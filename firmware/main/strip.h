#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "game.h"

/** Set up the WS2812 strip. rgb_order: the strip takes RGB instead of GRB. */
esp_err_t strip_init(int32_t n_leds, int32_t gpio, bool rgb_order, int32_t brightness, int32_t max_current_ma);

/** Brightness (1..255) and the current limit can change at any time. */
void strip_set_brightness(int32_t brightness);
void strip_set_max_current(int32_t ma);

/**
 * Show a frame: gamma, brightness, the current limit and the strip direction
 * are applied here. leds has n_leds entries in game order (0 = player).
 */
void strip_show(const game_rgb_t *leds);

/** Estimated current of the last frame, after limiting, in mA. */
uint32_t strip_last_current_ma(void);
