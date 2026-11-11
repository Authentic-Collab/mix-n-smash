#include "players.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "auth.h"

static const char *TAG = "players";

#define QUIET_QUEUE_US   (30LL * 1000 * 1000)
#define QUIET_PLAYER_US  (30LL * 1000 * 1000)

typedef struct {
    char name[HS_NAME_LEN + 1];
    char token[AUTH_TOKEN_LEN + 1];
    int64_t last_seen_us;
    int32_t games;
} player_t;

static player_t current;
static bool has_current;
static player_t queue[PLAYERS_MAX_QUEUE];
static int32_t queue_len;
static int32_t pending;             /* 1-based queue index chosen by the admin, 0 = none */
static SemaphoreHandle_t lock;

void players_init(void)
{
    lock = xSemaphoreCreateMutex();
}

static int32_t find_token(const char *token)
{
    if (has_current && strcmp(current.token, token) == 0) return 0;
    for (int32_t i = 0; i < queue_len; i++) {
        if (strcmp(queue[i].token, token) == 0) return i + 1;
    }
    return -1;
}

static bool name_in_use(const char *name)
{
    if (has_current && strcasecmp(current.name, name) == 0) return true;
    for (int32_t i = 0; i < queue_len; i++) {
        if (strcasecmp(queue[i].name, name) == 0) return true;
    }
    return false;
}

/* "Jan" -> "Jan (2)", "Jan (3)", ... within the name length. */
static void suggest(const char *name, char out[HS_NAME_LEN + 1])
{
    for (int n = 2; n < 100; n++) {
        char suffix[8];
        snprintf(suffix, sizeof(suffix), " (%d)", n);
        size_t keep = HS_NAME_LEN - strlen(suffix);
        size_t len = strlen(name);
        if (len > keep) len = keep;
        memcpy(out, name, len);
        strcpy(out + len, suffix);
        if (!name_in_use(out)) return;
    }
    strcpy(out, name);
}

static void remove_queue(int32_t i)
{
    memmove(&queue[i], &queue[i + 1], sizeof(player_t) * (size_t)(queue_len - 1 - i));
    queue_len--;
    if (pending == i + 1) pending = 0;
    else if (pending > i + 1) pending--;
}

static void promote(int32_t i)
{
    current = queue[i];
    current.games = 0;
    has_current = true;
    remove_queue(i);
    ESP_LOGI(TAG, "'%s' plays now", current.name);
}

static void drop_current(void)
{
    has_current = false;
    memset(&current, 0, sizeof(current));
    if (queue_len > 0) promote(0);
}

players_result_t players_join(const char *token, const char *name, bool accept_suffix,
                              char suggestion[HS_NAME_LEN + 1], int32_t *position)
{
    char clean[HS_NAME_LEN + 1];
    highscores_sanitize(clean, sizeof(clean), name);
    suggestion[0] = '\0';
    if (!clean[0]) return PLAYERS_BAD_NAME;

    xSemaphoreTake(lock, portMAX_DELAY);
    int64_t now = esp_timer_get_time();
    int32_t pos = find_token(token);
    if (pos >= 0) {                        /* already here: just refresh */
        player_t *p = pos == 0 ? &current : &queue[pos - 1];
        p->last_seen_us = now;
        *position = pos;
        xSemaphoreGive(lock);
        return PLAYERS_OK;
    }
    players_result_t res = PLAYERS_OK;
    if (name_in_use(clean)) {
        suggest(clean, suggestion);
        if (!accept_suffix) {
            res = PLAYERS_NAME_TAKEN;
        } else {
            strcpy(clean, suggestion);
        }
    }
    if (res == PLAYERS_OK) {
        if (!has_current) {
            memset(&current, 0, sizeof(current));
            strcpy(current.name, clean);
            strcpy(current.token, token);
            current.last_seen_us = now;
            has_current = true;
            *position = 0;
            ESP_LOGI(TAG, "'%s' plays now", clean);
        } else if (queue_len < PLAYERS_MAX_QUEUE) {
            player_t *p = &queue[queue_len++];
            memset(p, 0, sizeof(*p));
            strcpy(p->name, clean);
            strcpy(p->token, token);
            p->last_seen_us = now;
            *position = queue_len;
            ESP_LOGI(TAG, "'%s' waits at %d", clean, (int)queue_len);
        } else {
            res = PLAYERS_FULL;
        }
    }
    xSemaphoreGive(lock);
    return res;
}

void players_leave(const char *token)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    int32_t pos = find_token(token);
    if (pos == 0) {
        ESP_LOGI(TAG, "'%s' stops", current.name);
        drop_current();
    } else if (pos > 0) {
        remove_queue(pos - 1);
    }
    xSemaphoreGive(lock);
}

int32_t players_seen(const char *token)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    int32_t pos = find_token(token);
    if (pos == 0) current.last_seen_us = esp_timer_get_time();
    else if (pos > 0) queue[pos - 1].last_seen_us = esp_timer_get_time();
    xSemaphoreGive(lock);
    return pos;
}

bool players_is_current(const char *token)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    bool yes = has_current && strcmp(current.token, token) == 0;
    xSemaphoreGive(lock);
    return yes;
}

bool players_has_current(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    bool yes = has_current;
    xSemaphoreGive(lock);
    return yes;
}

bool players_current_name(char out[HS_NAME_LEN + 1])
{
    xSemaphoreTake(lock, portMAX_DELAY);
    bool yes = has_current;
    if (yes) strcpy(out, current.name);
    xSemaphoreGive(lock);
    return yes;
}

int32_t players_list(player_info_t *cur, player_info_t *list, int32_t max)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    if (has_current) {
        strcpy(cur->name, current.name);
        cur->games = current.games;
    } else {
        cur->name[0] = '\0';
        cur->games = -1;
    }
    int32_t n = queue_len < max ? queue_len : max;
    for (int32_t i = 0; i < n; i++) {
        strcpy(list[i].name, queue[i].name);
        list[i].games = queue[i].games;
    }
    xSemaphoreGive(lock);
    return n;
}

int32_t players_pending(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    int32_t p = pending;
    xSemaphoreGive(lock);
    return p;
}

void players_game_started(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    if (has_current) current.games++;
    xSemaphoreGive(lock);
}

/* Put the current player at the back of the queue and let index i play. */
static void swap_in(int32_t i)
{
    player_t old = current;
    promote(i);
    if (old.token[0] && queue_len < PLAYERS_MAX_QUEUE) {
        old.games = 0;
        old.last_seen_us = esp_timer_get_time();
        queue[queue_len++] = old;
    }
}

void players_game_finished(int32_t games_per_turn, bool aborted)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    if (has_current) {
        if (pending >= 1 && pending <= queue_len) {
            int32_t i = pending - 1;
            pending = 0;
            swap_in(i);
        } else if (!aborted && games_per_turn > 0 && current.games >= games_per_turn && queue_len > 0) {
            swap_in(0);
        }
    }
    xSemaphoreGive(lock);
}

void players_tick(bool game_running)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    int64_t now = esp_timer_get_time();
    for (int32_t i = queue_len - 1; i >= 0; i--) {
        if (now - queue[i].last_seen_us > QUIET_QUEUE_US) {
            ESP_LOGI(TAG, "'%s' left the queue (quiet)", queue[i].name);
            remove_queue(i);
        }
    }
    if (has_current && !game_running && now - current.last_seen_us > QUIET_PLAYER_US) {
        ESP_LOGI(TAG, "'%s' went quiet", current.name);
        drop_current();
    }
    xSemaphoreGive(lock);
}

bool players_admin_activate(int32_t index, bool game_running)
{
    bool ok = false;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (index >= 1 && index <= queue_len) {
        if (game_running && has_current) {
            pending = index;               /* from the next game on */
        } else {
            pending = 0;
            if (has_current) swap_in(index - 1);
            else promote(index - 1);
        }
        ok = true;
    }
    xSemaphoreGive(lock);
    return ok;
}

bool players_admin_remove(int32_t index)
{
    bool ok = false;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (index == 0 && has_current) {
        ESP_LOGI(TAG, "'%s' removed by the admin", current.name);
        drop_current();
        ok = true;
    } else if (index >= 1 && index <= queue_len) {
        remove_queue(index - 1);
        ok = true;
    }
    xSemaphoreGive(lock);
    return ok;
}
