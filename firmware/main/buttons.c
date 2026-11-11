#include "buttons.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "buttons";

#define STABLE_POLLS 2

static gpio_num_t pins[3];
static int active_level;
static bool pressed[3];
static uint8_t count[3];

void buttons_init(const int32_t gpio[3], bool active_high)
{
    for (int i = 0; i < 3; i++) pins[i] = (gpio_num_t)gpio[i];
    active_level = active_high ? 1 : 0;
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << pins[0]) | (1ULL << pins[1]) | (1ULL << pins[2]),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = active_high ? GPIO_PULLUP_DISABLE : GPIO_PULLUP_ENABLE,
        .pull_down_en = active_high ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    ESP_LOGI(TAG, "red GPIO %d, yellow GPIO %d, blue GPIO %d, active %s",
             pins[0], pins[1], pins[2], active_high ? "high" : "low");
}

uint8_t buttons_raw(void)
{
    uint8_t mask = 0;
    for (int i = 0; i < 3; i++) {
        if (gpio_get_level(pins[i]) == active_level) mask |= (uint8_t)(1u << i);
    }
    return mask;
}

uint8_t buttons_poll(void)
{
    uint8_t edges = 0;
    for (int i = 0; i < 3; i++) {
        bool raw = gpio_get_level(pins[i]) == active_level;
        if (raw == pressed[i]) {
            count[i] = 0;
            continue;
        }
        if (++count[i] >= STABLE_POLLS) {
            pressed[i] = raw;
            count[i] = 0;
            if (raw) edges |= (uint8_t)(1u << i);   /* bit 0 red, 1 yellow, 2 blue */
        }
    }
    return edges;
}
