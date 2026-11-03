/*
 * WS2812 output through the espressif/led_strip component (RMT backend).
 *
 * The game renders nominal colours. Here they go through a gamma curve
 * scaled to the configured brightness, then the frame's current is
 * estimated and, if it would exceed the limit, the whole frame is dimmed.
 * The estimate is 1 mA per LED idle plus 20 mA per fully lit colour channel,
 * which is what a WS2812B does at 5 V.
 */
#include "strip.h"
#include <math.h>
#include "esp_check.h"
#include "esp_log.h"
#include "led_strip.h"
#include "sdkconfig.h"

static const char *TAG = "strip";

#define IDLE_MA_PER_LED    1
#define MA_PER_CHANNEL     20

static led_strip_handle_t handle;
static int32_t n_leds;
static uint8_t lut[256];
static uint32_t last_ma;

esp_err_t strip_init(int32_t count)
{
    n_leds = count;
    for (int i = 0; i < 256; i++) {
        double v = pow(i / 255.0, 2.2) * CONFIG_MNS_BRIGHTNESS;
        long out = lround(v);
        if (i > 0 && out < 1) out = 1;       /* keep faint anti-aliasing visible */
        if (out > 255) out = 255;
        lut[i] = (uint8_t)out;
    }

    led_strip_config_t cfg = {
        .strip_gpio_num = CONFIG_MNS_LED_GPIO,
        .max_leds = (uint32_t)count,
        .led_model = LED_MODEL_WS2812,
#ifdef CONFIG_MNS_LED_RGB_ORDER
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_RGB,
#else
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
#endif
        .flags = { .invert_out = false },
    };
    led_strip_rmt_config_t rmt = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 256,          /* bigger buffer: fewer refill interrupts */
        .flags = { .with_dma = false },
    };
    ESP_RETURN_ON_ERROR(led_strip_new_rmt_device(&cfg, &rmt, &handle), TAG, "led_strip_new_rmt_device");
    ESP_RETURN_ON_ERROR(led_strip_clear(handle), TAG, "led_strip_clear");
    ESP_LOGI(TAG, "%d LEDs on GPIO %d, brightness %d/255, limit %d mA%s",
             (int)count, CONFIG_MNS_LED_GPIO, CONFIG_MNS_BRIGHTNESS, CONFIG_MNS_MAX_CURRENT_MA,
#ifdef CONFIG_MNS_LED_REVERSED
             ", reversed");
#else
             "");
#endif
    return ESP_OK;
}

void strip_show(const game_rgb_t *leds)
{
    static uint8_t out[GAME_MAX_LEDS][3];
    uint32_t sum = 0;
    for (int32_t i = 0; i < n_leds; i++) {
        out[i][0] = lut[leds[i].r];
        out[i][1] = lut[leds[i].g];
        out[i][2] = lut[leds[i].b];
        sum += out[i][0] + out[i][1] + out[i][2];
    }

    /* Current budget for the colour channels, in units of 1/255 channel. */
    int32_t budget = (CONFIG_MNS_MAX_CURRENT_MA - n_leds * IDLE_MA_PER_LED) * 255 / MA_PER_CHANNEL;
    if (budget < 255) budget = 255;
    if (sum > (uint32_t)budget) {
        for (int32_t i = 0; i < n_leds; i++) {
            for (int c = 0; c < 3; c++) {
                out[i][c] = (uint8_t)((uint32_t)out[i][c] * (uint32_t)budget / sum);
            }
        }
        sum = (uint32_t)budget;
    }
    last_ma = (uint32_t)n_leds * IDLE_MA_PER_LED + sum * MA_PER_CHANNEL / 255;

    for (int32_t i = 0; i < n_leds; i++) {
#ifdef CONFIG_MNS_LED_REVERSED
        uint32_t idx = (uint32_t)(n_leds - 1 - i);
#else
        uint32_t idx = (uint32_t)i;
#endif
        led_strip_set_pixel(handle, idx, out[i][0], out[i][1], out[i][2]);
    }
    esp_err_t err = led_strip_refresh(handle);
    if (err != ESP_OK) ESP_LOGW(TAG, "refresh: %s", esp_err_to_name(err));
}

uint32_t strip_last_current_ma(void)
{
    return last_ma;
}
