/*
 * Mix 'n Smash firmware for a Wemos ESP-WROOM-32 (any classic ESP32 works).
 *
 * A WS2812 strip shows the game, three buttons play it, and a web page
 * shows the strip live, keeps the high scores, lets players queue up from
 * their phones and holds the settings. The rules live in ../../game and
 * are shared with the tests and the browser version.
 *
 * The game task runs on core 1 at a fixed frame rate (16 ms by default):
 * poll the buttons, advance the game, render, push the frame to the strip,
 * keep a copy for the web server and push it to the WebSocket clients every
 * 100 ms. Wi-Fi and the HTTP server stay on core 0 so they do not disturb
 * the frame timing.
 *
 * Settings come from NVS (set on the web page), with menuconfig as the
 * defaults. Holding the red and blue button while powering on resets them.
 */
#include <string.h>
#include "app.h"
#include "auth.h"
#include "btn_leds.h"
#include "buttons.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "game.h"
#include "highscores.h"
#include "net.h"
#include "players.h"
#include "sdkconfig.h"
#include "settings.h"
#include "strip.h"
#include "web_server.h"

static const char *TAG = "mns";

#define PREVIEW_HOLD_US  (5 * 1000 * 1000)
#define BROADCAST_MS     100
#define RESET_HOLD_MS    3000
#define RESET_BAR_LEDS   20

static game_t game;
static game_rgb_t frame[GAME_MAX_LEDS];
static mns_settings_t cfg;                /* the running settings */
static app_snapshot_t snapshot;
static SemaphoreHandle_t lock;            /* snapshot, palette, flags */
static QueueHandle_t remote_presses;

static game_rgb_t palette[8];             /* source of truth; the game task copies it */
static bool palette_changed;
static int64_t preview_until_us;
static bool abort_requested;
static bool settings_changed;

/* ---- Shared with the web server ----------------------------------------- */

void app_snapshot_get(app_snapshot_t *out)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    memcpy(out, &snapshot, sizeof(*out));
    xSemaphoreGive(lock);
}

bool app_game_running(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    game_state_t st = snapshot.state;
    xSemaphoreGive(lock);
    return st == GAME_STATE_COUNTDOWN || st == GAME_STATE_PLAYING || st == GAME_STATE_LEVEL_CLEAR;
}

void app_remote_press(uint8_t color)
{
    xQueueSend(remote_presses, &color, 0);
}

void app_abort_game(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    abort_requested = true;
    xSemaphoreGive(lock);
}

void app_settings_changed(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    settings_changed = true;
    xSemaphoreGive(lock);
}

static void restart_cb(void *arg)
{
    (void)arg;
    esp_restart();
}

void app_restart(void)
{
    static esp_timer_handle_t t;
    if (!t) {
        const esp_timer_create_args_t args = { .callback = restart_cb, .name = "restart" };
        ESP_ERROR_CHECK(esp_timer_create(&args, &t));
    }
    ESP_LOGW(TAG, "restarting");
    esp_timer_start_once(t, 500 * 1000);
}

void app_get_palette(game_rgb_t out[8])
{
    xSemaphoreTake(lock, portMAX_DELAY);
    memcpy(out, palette, sizeof(palette));
    xSemaphoreGive(lock);
}

void app_set_color(uint8_t mask, game_rgb_t color)
{
    if (mask < 1 || mask > 7) return;
    xSemaphoreTake(lock, portMAX_DELAY);
    palette[mask] = color;
    palette_changed = true;
    xSemaphoreGive(lock);
}

void app_reset_palette(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    for (int m = 0; m < 8; m++) palette[m] = game_color_for_mask((uint8_t)m);
    palette_changed = true;
    xSemaphoreGive(lock);
    settings_erase_palette();
}

bool app_save_palette(void)
{
    game_rgb_t copy[8];
    app_get_palette(copy);
    return settings_save_palette(copy);
}

void app_set_preview(bool on)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    preview_until_us = on ? esp_timer_get_time() + PREVIEW_HOLD_US : 0;
    xSemaphoreGive(lock);
}

/* ---- Game task ---------------------------------------------------------- */

static void game_options_from(const mns_settings_t *s, game_options_t *opt)
{
    game_default_options(opt);
    opt->wrong_hit_keeps_dot = s->wrong_hit_keeps_dot;
    opt->level1_dots = s->level1_dots;
    opt->dots_per_level = s->dots_per_level;
    opt->speed_pct = s->speed_pct;
}

static bool game_idle(void)
{
    return game.state == GAME_STATE_ATTRACT || game.state == GAME_STATE_GAME_OVER;
}

/* Apply what the web server changed; returns whether to show the colour chart. */
static bool sync_from_web(bool *abort_now)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    if (palette_changed) {
        for (uint8_t m = 1; m < 8; m++) game_set_color(&game, m, palette[m]);
        palette_changed = false;
    }
    bool preview = esp_timer_get_time() < preview_until_us;
    *abort_now = abort_requested;
    abort_requested = false;
    bool reload = settings_changed;
    settings_changed = false;
    xSemaphoreGive(lock);

    if (reload) {
        settings_get(&cfg);
        strip_set_brightness(cfg.brightness);
        strip_set_max_current(cfg.max_current_ma);
    }
    if (game_idle()) {                    /* rules change between games only */
        game_options_t opt;
        game_options_from(&cfg, &opt);
        game_set_options(&game, &opt);
    }
    return preview;
}

static void publish_snapshot(uint32_t frame_no, bool preview)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    snapshot.preview = preview;
    snapshot.state = game.state;
    snapshot.level = game.level;
    snapshot.lives = game.lives;
    snapshot.score = game.score;
    snapshot.n_leds = game.n_leds;
    snapshot.frame = frame_no;
    snapshot.current_ma = strip_last_current_ma();
    memcpy(snapshot.leds, frame, sizeof(game_rgb_t) * (size_t)game.n_leds);
    xSemaphoreGive(lock);
}

static void handle_events(uint32_t ev)
{
    char player[HS_NAME_LEN + 1];
    if (ev & GAME_EV_START) {
        players_game_started();
        if (!players_current_name(player)) strcpy(player, "Speler");
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
        if (!players_current_name(player)) strcpy(player, "Speler");
        int rank = highscores_add(player, game.score, (uint8_t)game.level);
        ESP_LOGI(TAG, "game over for '%s': level %d, score %u%s", player, (int)game.level,
                 (unsigned)game.score, rank ? " (in the top 10)" : "");
        players_game_finished(cfg.games_per_turn, false);
    }
    if (ev & GAME_EV_ABORTED) {
        ESP_LOGI(TAG, "game stopped from the settings page");
        players_game_finished(cfg.games_per_turn, true);
    }
}

static void game_task(void *arg)
{
    (void)arg;
    const TickType_t period = pdMS_TO_TICKS(CONFIG_MNS_FRAME_MS);
    TickType_t last_wake = xTaskGetTickCount();
    int64_t prev_us = esp_timer_get_time();
    int64_t last_broadcast_us = 0;
    uint32_t frame_no = 0;

    while (true) {
        vTaskDelayUntil(&last_wake, period);
        int64_t now_us = esp_timer_get_time();
        int32_t dt_ms = (int32_t)((now_us - prev_us) / 1000);
        prev_us = now_us;

        uint8_t physical = buttons_poll();
        uint8_t remote = 0, r;
        while (xQueueReceive(remote_presses, &r, 0) == pdTRUE) remote |= r;
        /* While someone plays from a phone, the buttons on the bar are off. */
        uint8_t pressed = players_has_current() ? remote : (uint8_t)(physical | remote);

        bool abort_now;
        bool preview = sync_from_web(&abort_now);
        if (preview) {
            game_render_palette(&game, frame);
            strip_show(frame);
            btn_leds_update(&game, pressed, true, dt_ms);
            publish_snapshot(++frame_no, true);
        } else {
            if (abort_now) game_abort(&game);
            if (pressed & GAME_RED) game_press(&game, GAME_RED);
            if (pressed & GAME_YELLOW) game_press(&game, GAME_YELLOW);
            if (pressed & GAME_BLUE) game_press(&game, GAME_BLUE);

            game_tick(&game, dt_ms);
            handle_events(game_take_events(&game));

            game_render(&game, frame);
            strip_show(frame);
            btn_leds_update(&game, pressed, false, dt_ms);
            publish_snapshot(++frame_no, false);
        }

        players_tick(!game_idle());
        if (now_us - last_broadcast_us >= BROADCAST_MS * 1000) {
            last_broadcast_us = now_us;
            web_server_broadcast();
        }
    }
}

/* ---- Reset by holding red + blue while powering on ---------------------- */

static void show_bar(int32_t lit, game_rgb_t color)
{
    memset(frame, 0, sizeof(game_rgb_t) * (size_t)cfg.led_count);
    for (int32_t i = 0; i < lit && i < cfg.led_count; i++) frame[i] = color;
    strip_show(frame);
}

static void check_factory_reset(void)
{
    const uint8_t combo = GAME_RED | GAME_BLUE;
    if ((buttons_raw() & combo) != combo) return;
    ESP_LOGW(TAG, "red + blue held: keep holding for %d s to reset the settings", RESET_HOLD_MS / 1000);
    const game_rgb_t red = {255, 0, 0}, white = {255, 255, 255};
    for (int32_t t = 0; t <= RESET_HOLD_MS; t += 50) {
        if ((buttons_raw() & combo) != combo) {
            ESP_LOGI(TAG, "released: nothing reset");
            return;
        }
        show_bar(RESET_BAR_LEDS * t / RESET_HOLD_MS, red);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    settings_reset();
    for (int i = 0; i < 3; i++) {
        show_bar(RESET_BAR_LEDS, white);
        vTaskDelay(pdMS_TO_TICKS(200));
        show_bar(0, white);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    esp_restart();
}

void app_main(void)
{
    ESP_LOGI(TAG, "Mix 'n Smash");
    lock = xSemaphoreCreateMutex();
    remote_presses = xQueueCreate(8, sizeof(uint8_t));

    highscores_init();                    /* brings up NVS */
    settings_init();
    settings_get(&cfg);
    auth_init();
    players_init();

    ESP_ERROR_CHECK(strip_init(cfg.led_count, cfg.led_gpio, cfg.rgb_order, cfg.brightness, cfg.max_current_ma));
    buttons_init(cfg.btn_gpio, cfg.btn_active_high);
    check_factory_reset();
    btn_leds_init(cfg.lights, cfg.light_gpio, cfg.lights_active_high);

    game_options_t opt;
    game_options_from(&cfg, &opt);
    game_init(&game, cfg.led_count, esp_random(), &opt);
    for (int m = 0; m < 8; m++) palette[m] = game_color_for_mask((uint8_t)m);
    if (settings_load_palette(palette)) {
        for (uint8_t m = 1; m < 8; m++) game_set_color(&game, m, palette[m]);
    }
    game_render(&game, frame);
    strip_show(frame);
    publish_snapshot(0, false);

    net_init(&cfg);
    web_server_start();

    xTaskCreatePinnedToCore(game_task, "game", 8192, NULL, 5, NULL, 1);
}
