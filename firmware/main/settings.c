#include "settings.h"
#include <string.h>
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "settings";

#define NS          "mns"
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
