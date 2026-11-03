#include "buttons.h"
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "buttons";

#define STABLE_POLLS 2

static const gpio_num_t PINS[3] = {
    CONFIG_MNS_BTN_RED_GPIO, CONFIG_MNS_BTN_YELLOW_GPIO, CONFIG_MNS_BTN_BLUE_GPIO
};

#ifdef CONFIG_MNS_BTN_ACTIVE_HIGH
#define ACTIVE_LEVEL 1
#else
#define ACTIVE_LEVEL 0
#endif

static bool pressed[3];
static uint8_t count[3];

void buttons_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PINS[0]) | (1ULL << PINS[1]) | (1ULL << PINS[2]),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = ACTIVE_LEVEL ? GPIO_PULLUP_DISABLE : GPIO_PULLUP_ENABLE,
        .pull_down_en = ACTIVE_LEVEL ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    ESP_LOGI(TAG, "red GPIO %d, yellow GPIO %d, blue GPIO %d, active %s",
             PINS[0], PINS[1], PINS[2], ACTIVE_LEVEL ? "high" : "low");
}

uint8_t buttons_poll(void)
{
    uint8_t edges = 0;
    for (int i = 0; i < 3; i++) {
        bool raw = gpio_get_level(PINS[i]) == ACTIVE_LEVEL;
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
