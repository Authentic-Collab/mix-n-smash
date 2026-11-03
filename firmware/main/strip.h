#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "game.h"

/** Set up the WS2812 strip on the configured GPIO. */
esp_err_t strip_init(int32_t n_leds);

/**
 * Show a frame: gamma, brightness, the current limit and the strip direction
 * are applied here. leds has n_leds entries in game order (0 = player).
 */
void strip_show(const game_rgb_t *leds);

/** Estimated current of the last frame, after limiting, in mA. */
uint32_t strip_last_current_ma(void);
