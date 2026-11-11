#include "settings.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "sha256.h"

static const char *TAG = "settings";

#define NS            "mns"
#define KEY_SETTINGS  "settings"
#define KEY_PW_ADMIN  "pw_admin"
#define KEY_PW_PLAYER "pw_player"
#define VERSION       1u

typedef struct {
    uint32_t version;
    mns_settings_t s;
} blob_t;

static mns_settings_t saved;       /* what is in NVS (or the defaults) */
static mns_settings_t running;     /* what the firmware started with */
static SemaphoreHandle_t lock;

static void copy_str(char *dst, size_t cap, const char *src)
{
    strlcpy(dst, src, cap);
}

void settings_defaults(mns_settings_t *s)
{
    memset(s, 0, sizeof(*s));
#ifdef CONFIG_MNS_WIFI_ENABLE
    copy_str(s->ap_ssid, sizeof(s->ap_ssid), CONFIG_MNS_AP_SSID);
    copy_str(s->ap_password, sizeof(s->ap_password), CONFIG_MNS_AP_PASSWORD);
    copy_str(s->sta_ssid, sizeof(s->sta_ssid), CONFIG_MNS_WIFI_SSID);
    copy_str(s->sta_password, sizeof(s->sta_password), CONFIG_MNS_WIFI_PASSWORD);
    copy_str(s->hostname, sizeof(s->hostname), CONFIG_MNS_HOSTNAME);
#else
    copy_str(s->ap_ssid, sizeof(s->ap_ssid), "mix-n-smash");
    copy_str(s->hostname, sizeof(s->hostname), "mix-n-smash");
#endif
    s->led_count = CONFIG_MNS_LED_COUNT;
    s->led_gpio = CONFIG_MNS_LED_GPIO;
    s->brightness = CONFIG_MNS_BRIGHTNESS;
    s->max_current_ma = CONFIG_MNS_MAX_CURRENT_MA;
#ifdef CONFIG_MNS_LED_RGB_ORDER
    s->rgb_order = true;
#endif
#ifdef CONFIG_MNS_WRONG_HIT_KEEPS_DOT
    s->wrong_hit_keeps_dot = true;
#endif
    s->level1_dots = 8;
    s->dots_per_level = 2;
    s->speed_pct = 100;
    s->games_per_turn = 0;
    s->btn_gpio[0] = CONFIG_MNS_BTN_RED_GPIO;
    s->btn_gpio[1] = CONFIG_MNS_BTN_YELLOW_GPIO;
    s->btn_gpio[2] = CONFIG_MNS_BTN_BLUE_GPIO;
#ifdef CONFIG_MNS_BTN_ACTIVE_HIGH
    s->btn_active_high = true;
#endif
#ifdef CONFIG_MNS_BTN_LEDS
    s->lights = true;
    s->light_gpio[0] = CONFIG_MNS_BTN_LED_RED_GPIO;
    s->light_gpio[1] = CONFIG_MNS_BTN_LED_YELLOW_GPIO;
    s->light_gpio[2] = CONFIG_MNS_BTN_LED_BLUE_GPIO;
#ifdef CONFIG_MNS_BTN_LED_ACTIVE_HIGH
    s->lights_active_high = true;
#endif
#else
    s->light_gpio[0] = 32;
    s->light_gpio[1] = 33;
    s->light_gpio[2] = 14;
#endif
}

static bool load_blob(mns_settings_t *out)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    blob_t b;
    size_t len = sizeof(b);
    bool ok = nvs_get_blob(h, KEY_SETTINGS, &b, &len) == ESP_OK && len == sizeof(b) && b.version == VERSION;
    nvs_close(h);
    if (!ok) return false;
    *out = b.s;
    /* Strings from flash: make sure they end. */
    out->ap_ssid[SET_SSID_LEN] = out->sta_ssid[SET_SSID_LEN] = out->hostname[SET_SSID_LEN] = '\0';
    out->ap_password[SET_PASS_LEN] = out->sta_password[SET_PASS_LEN] = '\0';
    return settings_validate(out) == NULL;
}

void settings_init(void)
{
    lock = xSemaphoreCreateMutex();
    settings_defaults(&saved);
    if (load_blob(&saved)) ESP_LOGI(TAG, "settings loaded from flash");
    else ESP_LOGI(TAG, "using the menuconfig defaults");
    running = saved;
}

void settings_get(mns_settings_t *out)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    *out = saved;
    xSemaphoreGive(lock);
}

static bool gpio_ok_output(int32_t g) { return g >= 0 && g <= 33 && !(g >= 6 && g <= 11); }
static bool gpio_ok_input(int32_t g)  { return g >= 0 && g <= 39 && !(g >= 6 && g <= 11); }

const char *settings_validate(const mns_settings_t *s)
{
    size_t n;
    if ((n = strlen(s->ap_ssid)) < 1 || n > SET_SSID_LEN) return "Access point name must be 1 to 32 characters";
    n = strlen(s->ap_password);
    if (n != 0 && (n < 8 || n > SET_PASS_LEN)) return "Access point password must be empty or 8 to 63 characters";
    if (strlen(s->sta_ssid) > SET_SSID_LEN) return "Network name too long";
    if (strlen(s->sta_password) > SET_PASS_LEN) return "Network password too long";
    if ((n = strlen(s->hostname)) < 1 || n > SET_SSID_LEN) return "Hostname must be 1 to 32 characters";
    for (size_t i = 0; i < n; i++) {
        char c = s->hostname[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        if (!ok) return "Hostname: only a-z, 0-9 and -";
    }
    if (s->led_count < 16 || s->led_count > 400) return "LED count must be 16 to 400";
    if (!gpio_ok_output(s->led_gpio)) return "Strip data GPIO must be 0 to 33 (not 6 to 11)";
    if (s->brightness < 1 || s->brightness > 255) return "Brightness must be 1 to 255";
    if (s->max_current_ma < 100 || s->max_current_ma > 20000) return "Current limit must be 100 to 20000 mA";
    if (s->level1_dots < 1 || s->level1_dots > 200) return "Dots in level 1 must be 1 to 200";
    if (s->dots_per_level < 0 || s->dots_per_level > 50) return "Extra dots per level must be 0 to 50";
    if (s->speed_pct < 25 || s->speed_pct > 300) return "Speed must be 25 to 300 %";
    if (s->games_per_turn < 0 || s->games_per_turn > 3) return "Games per turn must be 0 to 3";

    int32_t used[7];
    int count = 0;
    used[count++] = s->led_gpio;
    for (int i = 0; i < 3; i++) {
        if (!gpio_ok_input(s->btn_gpio[i])) return "Button GPIOs must be 0 to 39 (not 6 to 11)";
        if (!s->btn_active_high && s->btn_gpio[i] >= 34) return "GPIO 34 to 39 have no pull-up: use them only with buttons to 3.3 V";
        used[count++] = s->btn_gpio[i];
    }
    if (s->lights) {
        for (int i = 0; i < 3; i++) {
            if (!gpio_ok_output(s->light_gpio[i])) return "Light GPIOs must be 0 to 33 (not 6 to 11)";
            used[count++] = s->light_gpio[i];
        }
    }
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            if (used[i] == used[j]) return "A GPIO is used twice";
        }
    }
    return NULL;
}

bool settings_save(const mns_settings_t *s)
{
    if (settings_validate(s) != NULL) return false;
    blob_t b = { .version = VERSION, .s = *s };
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_blob(h, KEY_SETTINGS, &b, sizeof(b)) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    if (ok) {
        xSemaphoreTake(lock, portMAX_DELAY);
        saved = *s;
        xSemaphoreGive(lock);
        ESP_LOGI(TAG, "settings saved");
    }
    return ok;
}

void settings_reset(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, KEY_SETTINGS);
        nvs_erase_key(h, KEY_PW_ADMIN);
        nvs_erase_key(h, KEY_PW_PLAYER);
        nvs_erase_key(h, "palette");
        nvs_commit(h);
        nvs_close(h);
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    settings_defaults(&saved);
    xSemaphoreGive(lock);
    ESP_LOGW(TAG, "settings and passwords reset to the defaults");
}

bool settings_restart_needed(void)
{
    mns_settings_t s;
    settings_get(&s);
    const mns_settings_t *r = &running;
    bool same = strcmp(s.ap_ssid, r->ap_ssid) == 0 && strcmp(s.ap_password, r->ap_password) == 0 &&
                strcmp(s.sta_ssid, r->sta_ssid) == 0 && strcmp(s.sta_password, r->sta_password) == 0 &&
                strcmp(s.hostname, r->hostname) == 0 &&
                s.led_count == r->led_count && s.led_gpio == r->led_gpio && s.rgb_order == r->rgb_order &&
                memcmp(s.btn_gpio, r->btn_gpio, sizeof(s.btn_gpio)) == 0 && s.btn_active_high == r->btn_active_high &&
                s.lights == r->lights && memcmp(s.light_gpio, r->light_gpio, sizeof(s.light_gpio)) == 0 &&
                s.lights_active_high == r->lights_active_high;
    return !same;
}

/* ---- Passwords ---------------------------------------------------------- */

static const char *pw_key(auth_role_t role) { return role == AUTH_ADMIN ? KEY_PW_ADMIN : KEY_PW_PLAYER; }

static void hash_hex(const uint8_t *salt, const char *password, char out[65])
{
    uint8_t buf[8 + SET_PASS_LEN + 1];
    size_t n = strlen(password);
    if (n > SET_PASS_LEN) n = SET_PASS_LEN;
    memcpy(buf, salt, 8);
    memcpy(buf + 8, password, n);
    uint8_t digest[32];
    sha256(buf, 8 + n, digest);
    for (int i = 0; i < 32; i++) sprintf(out + i * 2, "%02x", digest[i]);
    out[64] = '\0';
}

static bool read_pw(auth_role_t role, char *out, size_t cap)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = cap;
    bool ok = nvs_get_str(h, pw_key(role), out, &len) == ESP_OK;
    nvs_close(h);
    return ok && strlen(out) == 16 + 1 + 64;
}

bool settings_password_set(auth_role_t role)
{
    char stored[96];
    return read_pw(role, stored, sizeof(stored));
}

bool settings_password_check(auth_role_t role, const char *password)
{
    char stored[96];
    if (!read_pw(role, stored, sizeof(stored))) return false;
    uint8_t salt[8];
    for (int i = 0; i < 8; i++) {
        unsigned v;
        if (sscanf(stored + i * 2, "%2x", &v) != 1) return false;
        salt[i] = (uint8_t)v;
    }
    char hex[65];
    hash_hex(salt, password, hex);
    /* Constant-time compare, for form's sake. */
    int diff = 0;
    for (int i = 0; i < 64; i++) diff |= stored[17 + i] ^ hex[i];
    return diff == 0;
}

bool settings_password_store(auth_role_t role, const char *password)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok;
    if (password[0] == '\0') {
        esp_err_t e = nvs_erase_key(h, pw_key(role));
        ok = e == ESP_OK || e == ESP_ERR_NVS_NOT_FOUND;
    } else {
        uint8_t salt[8];
        esp_fill_random(salt, sizeof(salt));
        char stored[96];
        for (int i = 0; i < 8; i++) sprintf(stored + i * 2, "%02x", salt[i]);
        stored[16] = ':';
        hash_hex(salt, password, stored + 17);
        ok = nvs_set_str(h, pw_key(role), stored) == ESP_OK;
    }
    ok = ok && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    ESP_LOGI(TAG, "%s password %s", role == AUTH_ADMIN ? "settings" : "player", password[0] ? "set" : "removed");
    return ok;
}

/* ---- Palette ------------------------------------------------------------ */

#define KEY_PALETTE "palette"

bool settings_load_palette(game_rgb_t palette[8])
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    uint8_t raw[24];
    size_t len = sizeof(raw);
    bool ok = nvs_get_blob(h, KEY_PALETTE, raw, &len) == ESP_OK && len == sizeof(raw);
    nvs_close(h);
    if (!ok) return false;
    for (int m = 0; m < 8; m++) {
        palette[m].r = raw[m * 3];
        palette[m].g = raw[m * 3 + 1];
        palette[m].b = raw[m * 3 + 2];
    }
    ESP_LOGI(TAG, "palette loaded from flash");
    return true;
}

bool settings_save_palette(const game_rgb_t palette[8])
{
    uint8_t raw[24];
    for (int m = 0; m < 8; m++) {
        raw[m * 3] = palette[m].r;
        raw[m * 3 + 1] = palette[m].g;
        raw[m * 3 + 2] = palette[m].b;
    }
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_blob(h, KEY_PALETTE, raw, sizeof(raw)) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    ESP_LOGI(TAG, "palette %s", ok ? "saved" : "NOT saved");
    return ok;
}

void settings_erase_palette(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_key(h, KEY_PALETTE);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "palette back to defaults");
}
