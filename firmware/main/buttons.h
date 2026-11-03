#pragma once

#include <stdint.h>

/** Configure the three button GPIOs. */
void buttons_init(void);

/**
 * Poll the buttons; call it once per frame. Returns the buttons that were
 * pressed since the last call as GAME_RED | GAME_YELLOW | GAME_BLUE bits.
 * A press counts after two consecutive polls, which debounces it.
 */
uint8_t buttons_poll(void);
