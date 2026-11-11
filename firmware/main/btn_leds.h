#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "game.h"

/**
 * Lights in the three buttons (optional, see menuconfig → Buttons).
 *
 * What they do: chase red → yellow → blue while waiting for a player, all
 * on during the countdown and the game, a short blink of the button that
 * was pressed, fast blinking when a level is cleared, slow blinking
 * together at game over, off while the colour chart is shown.
 */
void btn_leds_init(void);

/** Call once per frame. pressed: GAME_RED/YELLOW/BLUE bits pressed this frame. */
void btn_leds_update(const game_t *g, uint8_t pressed, bool preview, int32_t dt_ms);
