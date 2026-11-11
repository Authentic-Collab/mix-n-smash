#pragma once

#include <stdbool.h>
#include "game.h"

/**
 * The colour palette in NVS. highscores_init() must have brought up NVS.
 * load: overwrites palette (8 entries, index = colour mask) when a stored
 * one exists and returns true; otherwise leaves it alone and returns false.
 */
bool settings_load_palette(game_rgb_t palette[8]);
bool settings_save_palette(const game_rgb_t palette[8]);
void settings_erase_palette(void);
