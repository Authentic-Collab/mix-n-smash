/*
 * High score table and player name in NVS (namespace "mns").
 */
#include "highscores.h"
#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "scores";

#define NS        "mns"
#define KEY_TABLE "table"
#define KEY_NAME  "player"
#define ANONYMOUS "Speler"

static hs_entry_t table[HS_MAX];
static char player[HS_NAME_LEN + 1] = ANONYMOUS;
static SemaphoreHandle_t lock;

static void save_table(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, KEY_TABLE, table, sizeof(table));
    nvs_commit(h);
    nvs_close(h);
}

static void load(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t len = sizeof(table);
    if (nvs_get_blob(h, KEY_TABLE, table, &len) != ESP_OK || len != sizeof(table)) {
        memset(table, 0, sizeof(table));
    }
    char raw[HS_NAME_LEN + 1];
    len = sizeof(raw);
    if (nvs_get_str(h, KEY_NAME, raw, &len) == ESP_OK) {
        highscores_sanitize(player, sizeof(player), raw);
        if (!player[0]) strcpy(player, ANONYMOUS);
    }
    nvs_close(h);
    for (int i = 0; i < HS_MAX; i++) {
        table[i].name[HS_NAME_LEN] = '\0';
        char clean[HS_NAME_LEN + 1];
        highscores_sanitize(clean, sizeof(clean), table[i].name);
        strcpy(table[i].name, clean);
    }
}

void highscores_init(void)
{
    lock = xSemaphoreCreateMutex();
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    load();
    int count = 0;
    for (int i = 0; i < HS_MAX; i++) if (table[i].score) count++;
    ESP_LOGI(TAG, "%d high scores loaded, player '%s'", count, player);
}

void highscores_sanitize(char *dst, size_t cap, const char *src)
{
    size_t n = 0;
    bool pending_space = false;
    for (const char *p = src; *p && n + 1 < cap && n < HS_NAME_LEN; p++) {
        unsigned char c = (unsigned char)*p;
        bool ok = isalnum(c) || c == '-' || c == '_' || c == '.';
        if (c == ' ') {
            if (n > 0) pending_space = true;    /* collapse and trim spaces */
            continue;
        }
        if (!ok) continue;
        if (pending_space && n + 2 < cap && n + 1 < HS_NAME_LEN) dst[n++] = ' ';
        pending_space = false;
        dst[n++] = (char)c;
    }
    dst[n] = '\0';
}

int highscores_add(const char *name, uint32_t score, uint8_t level)
{
    if (score == 0) return 0;
    char clean[HS_NAME_LEN + 1];
    highscores_sanitize(clean, sizeof(clean), name);
    if (!clean[0]) strcpy(clean, ANONYMOUS);

    xSemaphoreTake(lock, portMAX_DELAY);
    int rank = 0;
    for (int i = 0; i < HS_MAX; i++) {
        if (score > table[i].score) {
            memmove(&table[i + 1], &table[i], sizeof(hs_entry_t) * (size_t)(HS_MAX - 1 - i));
            memset(&table[i], 0, sizeof(hs_entry_t));
            strcpy(table[i].name, clean);
            table[i].score = score;
            table[i].level = level;
            rank = i + 1;
            save_table();
            break;
        }
    }
    xSemaphoreGive(lock);
    if (rank) ESP_LOGI(TAG, "'%s' scored %u (level %d): rank %d", clean, (unsigned)score, level, rank);
    return rank;
}

int highscores_get(hs_entry_t *out, int max)
{
    int n = 0;
    xSemaphoreTake(lock, portMAX_DELAY);
    for (int i = 0; i < HS_MAX && n < max; i++) {
        if (table[i].score) out[n++] = table[i];
    }
    xSemaphoreGive(lock);
    return n;
}

uint32_t highscores_best(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    uint32_t best = table[0].score;
    xSemaphoreGive(lock);
    return best;
}

void highscores_clear(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    memset(table, 0, sizeof(table));
    save_table();
    xSemaphoreGive(lock);
    ESP_LOGI(TAG, "high scores cleared");
}

void highscores_set_player(const char *name)
{
    char clean[HS_NAME_LEN + 1];
    highscores_sanitize(clean, sizeof(clean), name);
    if (!clean[0]) strcpy(clean, ANONYMOUS);
    xSemaphoreTake(lock, portMAX_DELAY);
    strcpy(player, clean);
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, KEY_NAME, player);
        nvs_commit(h);
        nvs_close(h);
    }
    xSemaphoreGive(lock);
    ESP_LOGI(TAG, "player is now '%s'", clean);
}

void highscores_get_player(char *out, size_t cap)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    strncpy(out, player, cap - 1);
    out[cap - 1] = '\0';
    xSemaphoreGive(lock);
}
