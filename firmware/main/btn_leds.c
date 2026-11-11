#include "btn_leds.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "btn_leds";

#ifdef CONFIG_MNS_BTN_LEDS

#include "driver/gpio.h"

#define PRESS_BLINK_MS   120     /* the pressed button goes dark this long */
#define CHASE_STEP_MS    350
#define CLEAR_BLINK_MS   120
#define OVER_BLINK_MS    400

static const gpio_num_t PINS[3] = {
    CONFIG_MNS_BTN_LED_RED_GPIO, CONFIG_MNS_BTN_LED_YELLOW_GPIO, CONFIG_MNS_BTN_LED_BLUE_GPIO
};

#ifdef CONFIG_MNS_BTN_LED_ACTIVE_HIGH
#define ON_LEVEL  1                      /* drives a transistor: high = on */
#define PIN_MODE  GPIO_MODE_OUTPUT
#else
#define ON_LEVEL  0                      /* sinks the LED current: low = on */
#define PIN_MODE  GPIO_MODE_OUTPUT_OD
#endif

static int32_t dark_ms[3];
static uint8_t shown = 0xFF;

static void show(uint8_t mask)
{
    if (mask == shown) return;
    shown = mask;
    for (int i = 0; i < 3; i++) {
        bool on = (mask >> i) & 1;
        gpio_set_level(PINS[i], on ? ON_LEVEL : !ON_LEVEL);
    }
}

void btn_leds_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PINS[0]) | (1ULL << PINS[1]) | (1ULL << PINS[2]),
        .mode = PIN_MODE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    show(0);
    ESP_LOGI(TAG, "button lights on GPIO %d/%d/%d, %s", PINS[0], PINS[1], PINS[2],
             ON_LEVEL ? "high = on" : "open-drain, low = on");
}

/* Which lights the game state asks for (bit 0 red, 1 yellow, 2 blue). */
static uint8_t pattern(const game_t *g)
{
    switch (g->state) {
    case GAME_STATE_ATTRACT: {
        int32_t step = (int32_t)((g->time_ms / CHASE_STEP_MS) % 4);   /* red, yellow, blue, pause */
        return step == 3 ? 0 : (uint8_t)(1u << step);
    }
    case GAME_STATE_COUNTDOWN:
    case GAME_STATE_PLAYING:
        return 7;
    case GAME_STATE_LEVEL_CLEAR:
        return (g->state_ms / CLEAR_BLINK_MS) % 2 ? 0 : 7;
    case GAME_STATE_GAME_OVER:
        return (g->state_ms / OVER_BLINK_MS) % 2 ? 0 : 7;
    default:
        return 0;
    }
}

void btn_leds_update(const game_t *g, uint8_t pressed, bool preview, int32_t dt_ms)
{
    uint8_t mask = preview ? 0 : pattern(g);
    for (int i = 0; i < 3; i++) {
        if (pressed & (1u << i)) dark_ms[i] = PRESS_BLINK_MS;
        else if (dark_ms[i] > 0) dark_ms[i] -= dt_ms;
        if (dark_ms[i] > 0) mask &= (uint8_t)~(1u << i);
    }
    show(mask);
}

#else

void btn_leds_init(void)
{
    ESP_LOGI(TAG, "button lights disabled in menuconfig");
}

void btn_leds_update(const game_t *g, uint8_t pressed, bool preview, int32_t dt_ms)
{
    (void)g; (void)pressed; (void)preview; (void)dt_ms;
}

#endif
