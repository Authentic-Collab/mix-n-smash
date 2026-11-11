#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "game.h"

/**
 * Lights in the three buttons (optional, settings → Buttons).
 *
 * What they do: chase red → yellow → blue while waiting for a player, all
 * on during the countdown and the game, a short blink of the button that
 * was pressed, fast blinking when a level is cleared, slow blinking
 * together at game over, off while the colour chart is shown.
 *
 * active_high = false: the GPIO is open-drain and sinks the LED current
 * (LED plus on +5 V, minus via its resistor to the pin, low = on).
 * active_high = true: the GPIO drives a transistor, high = on.
 */
void btn_leds_init(bool enabled, const int32_t gpio[3], bool active_high);

/** Call once per frame. pressed: GAME_RED/YELLOW/BLUE bits pressed this frame. */
void btn_leds_update(const game_t *g, uint8_t pressed, bool preview, int32_t dt_ms);
