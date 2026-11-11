#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Configure the three button GPIOs (red, yellow, blue). */
void buttons_init(const int32_t gpio[3], bool active_high);

/**
 * Poll the buttons; call it once per frame. Returns the buttons that were
 * pressed since the last call as GAME_RED | GAME_YELLOW | GAME_BLUE bits.
 * A press counts after two consecutive polls, which debounces it.
 */
uint8_t buttons_poll(void);

/** The buttons held down right now, same bits, without debouncing. */
uint8_t buttons_raw(void);
