#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "game.h"

/*
 * Settings live in NVS and can be changed from the web page. The values in
 * menuconfig are the defaults, used until something is saved and again
 * after a reset (web page, or red + blue held while powering on).
 */

#define SET_SSID_LEN  32
#define SET_PASS_LEN  64

typedef struct {
    /* Wi-Fi */
    char ap_ssid[SET_SSID_LEN + 1];
    char ap_password[SET_PASS_LEN + 1];   /* empty: open access point */
    char sta_ssid[SET_SSID_LEN + 1];      /* empty: start the access point */
    char sta_password[SET_PASS_LEN + 1];
    char hostname[SET_SSID_LEN + 1];
    /* Strip */
    int32_t led_count;
    int32_t led_gpio;
    int32_t brightness;
    int32_t max_current_ma;
    bool rgb_order;
    /* Game */
    bool wrong_hit_keeps_dot;
    int32_t level1_dots;
    int32_t dots_per_level;
    int32_t speed_pct;
    int32_t games_per_turn;               /* 0: the player keeps the turn */
    /* Buttons */
    int32_t btn_gpio[3];                  /* red, yellow, blue */
    bool btn_active_high;
    /* Lights in the buttons */
    bool lights;
    int32_t light_gpio[3];
    bool lights_active_high;
} mns_settings_t;

typedef enum { AUTH_ADMIN = 0, AUTH_PLAYER = 1 } auth_role_t;

void settings_defaults(mns_settings_t *s);
/** Load from NVS (needs NVS initialised) and remember what runs now. */
void settings_init(void);
void settings_get(mns_settings_t *out);
/** NULL when s is acceptable, otherwise a message for the web page. */
const char *settings_validate(const mns_settings_t *s);
/** Validate and store. */
bool settings_save(const mns_settings_t *s);
/** Back to the menuconfig defaults; passwords are removed too. */
void settings_reset(void);
/** The stored settings differ from the running ones in a way that needs a reboot. */
bool settings_restart_needed(void);

/* The colour palette (8 entries, index = colour mask), also in NVS. */
bool settings_load_palette(game_rgb_t palette[8]);
bool settings_save_palette(const game_rgb_t palette[8]);
void settings_erase_palette(void);

/* Passwords are stored as salted SHA-256; an empty password removes it. */
bool settings_password_set(auth_role_t role);
bool settings_password_check(auth_role_t role, const char *password);
bool settings_password_store(auth_role_t role, const char *password);
