/*
 * Mix 'n Smash firmware for a Wemos ESP-WROOM-32 (any classic ESP32 works).
 *
 * A WS2812 strip shows the game, three buttons play it, and a small web
 * page shows the high scores, lets a player enter a name and mirrors the
 * strip live. The rules live in ../../game and are shared with the tests
 * and the browser version.
 *
 * The game task runs on core 1 at a fixed frame rate (16 ms by default):
 * poll the buttons, advance the game, render, push the frame to the strip,
 * and keep a copy for the web server. Wi-Fi and the HTTP server stay on
 * core 0 so they do not disturb the frame timing.
 */
#include <string.h>
#include "app.h"
#include "buttons.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "game.h"
#include "highscores.h"
#include "net.h"
#include "sdkconfig.h"
#include "strip.h"
#include "web_server.h"

static const char *TAG = "mns";

static game_t game;
static game_rgb_t frame[GAME_MAX_LEDS];
static app_snapshot_t snapshot;
static SemaphoreHandle_t snapshot_lock;
static QueueHandle_t remote_presses;

void app_snapshot_get(app_snapshot_t *out)
{
    xSemaphoreTake(snapshot_lock, portMAX_DELAY);
    memcpy(out, &snapshot, sizeof(*out));
    xSemaphoreGive(snapshot_lock);
}

void app_remote_press(uint8_t color)
{
    xQueueSend(remote_presses, &color, 0);
}

static void publish_snapshot(uint32_t frame_no)
{
    xSemaphoreTake(snapshot_lock, portMAX_DELAY);
    snapshot.state = game.state;
    snapshot.level = game.level;
    snapshot.lives = game.lives;
    snapshot.score = game.score;
    snapshot.n_leds = game.n_leds;
    snapshot.frame = frame_no;
    snapshot.current_ma = strip_last_current_ma();
    memcpy(snapshot.leds, frame, sizeof(game_rgb_t) * (size_t)game.n_leds);
    xSemaphoreGive(snapshot_lock);
}

static void handle_events(uint32_t ev)
{
    if (ev & GAME_EV_START) {
        char player[HS_NAME_LEN + 1];
        highscores_get_player(player, sizeof(player));
        ESP_LOGI(TAG, "new game for '%s'", player);
    }
    if (ev & GAME_EV_LEVEL_CLEAR) {
        ESP_LOGI(TAG, "level %d cleared, score %u", (int)game.level, (unsigned)game.score);
    }
    if (ev & GAME_EV_LIFE_LOST) {
        ESP_LOGI(TAG, "life lost (%s), %d left", (ev & GAME_EV_WRONG_HIT) ? "wrong colour" : "dot got through",
                 (int)game.lives);
    }
    if (ev & GAME_EV_GAME_OVER) {
        char player[HS_NAME_LEN + 1];
        highscores_get_player(player, sizeof(player));
        int rank = highscores_add(player, game.score, (uint8_t)game.level);
        ESP_LOGI(TAG, "game over: level %d, score %u%s", (int)game.level, (unsigned)game.score,
                 rank ? " (in the top 10)" : "");
    }
}

static void game_task(void *arg)
{
    (void)arg;
    const TickType_t period = pdMS_TO_TICKS(CONFIG_MNS_FRAME_MS);
    TickType_t last_wake = xTaskGetTickCount();
    int64_t prev_us = esp_timer_get_time();
    uint32_t frame_no = 0;

    while (true) {
        vTaskDelayUntil(&last_wake, period);
        int64_t now_us = esp_timer_get_time();
        int32_t dt_ms = (int32_t)((now_us - prev_us) / 1000);
        prev_us = now_us;

        uint8_t pressed = buttons_poll();
        uint8_t remote;
        while (xQueueReceive(remote_presses, &remote, 0) == pdTRUE) pressed |= remote;
        if (pressed & GAME_RED) game_press(&game, GAME_RED);
        if (pressed & GAME_YELLOW) game_press(&game, GAME_YELLOW);
        if (pressed & GAME_BLUE) game_press(&game, GAME_BLUE);

        game_tick(&game, dt_ms);
        handle_events(game_take_events(&game));

        game_render(&game, frame);
        strip_show(frame);
        publish_snapshot(++frame_no);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Mix 'n Smash");
    snapshot_lock = xSemaphoreCreateMutex();
    remote_presses = xQueueCreate(8, sizeof(uint8_t));

    highscores_init();
    ESP_ERROR_CHECK(strip_init(CONFIG_MNS_LED_COUNT));
    buttons_init();

    game_options_t opt = {
#ifdef CONFIG_MNS_WRONG_HIT_KEEPS_DOT
        .wrong_hit_keeps_dot = true,
#else
        .wrong_hit_keeps_dot = false,
#endif
    };
    game_init(&game, CONFIG_MNS_LED_COUNT, esp_random(), &opt);
    game_render(&game, frame);
    strip_show(frame);
    publish_snapshot(0);

    net_init();
    web_server_start();

    xTaskCreatePinnedToCore(game_task, "game", 8192, NULL, 5, NULL, 1);
}
