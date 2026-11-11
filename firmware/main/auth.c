#include "auth.h"
#include <stdio.h>
#include <string.h>
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define MAX_SESSIONS   24          /* players on their phones plus a few admins */
#define LOGIN_DELAY_US (2 * 1000 * 1000)

typedef struct {
    char token[AUTH_TOKEN_LEN + 1];
    auth_role_t role;
    int64_t created_us;
} session_t;

static session_t sessions[MAX_SESSIONS];
static int64_t next_login_us;
static SemaphoreHandle_t lock;

void auth_init(void)
{
    lock = xSemaphoreCreateMutex();
}

static void new_token(char out[AUTH_TOKEN_LEN + 1])
{
    uint8_t raw[AUTH_TOKEN_LEN / 2];
    esp_fill_random(raw, sizeof(raw));
    for (size_t i = 0; i < sizeof(raw); i++) sprintf(out + i * 2, "%02x", raw[i]);
    out[AUTH_TOKEN_LEN] = '\0';
}

auth_login_result_t auth_login(auth_role_t role, const char *password, char token_out[AUTH_TOKEN_LEN + 1])
{
    if (!settings_password_set(role)) return AUTH_LOGIN_NOT_SET;
    int64_t now = esp_timer_get_time();
    if (now < next_login_us) return AUTH_LOGIN_TOO_SOON;
    if (!settings_password_check(role, password)) {
        next_login_us = now + LOGIN_DELAY_US;
        return AUTH_LOGIN_WRONG;
    }

    xSemaphoreTake(lock, portMAX_DELAY);
    int slot = 0;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].token[0] == '\0') { slot = i; break; }
        if (sessions[i].created_us < sessions[slot].created_us) slot = i;   /* oldest */
    }
    new_token(sessions[slot].token);
    sessions[slot].role = role;
    sessions[slot].created_us = now;
    strcpy(token_out, sessions[slot].token);
    xSemaphoreGive(lock);
    return AUTH_LOGIN_OK;
}

void auth_logout(const char *token)
{
    if (!token || !token[0]) return;
    xSemaphoreTake(lock, portMAX_DELAY);
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (strcmp(sessions[i].token, token) == 0) sessions[i].token[0] = '\0';
    }
    xSemaphoreGive(lock);
}

bool auth_valid(auth_role_t role, const char *token)
{
    if (!token || strlen(token) != AUTH_TOKEN_LEN) return false;
    bool ok = false;
    xSemaphoreTake(lock, portMAX_DELAY);
    for (int i = 0; i < MAX_SESSIONS && !ok; i++) {
        ok = sessions[i].role == role && strcmp(sessions[i].token, token) == 0;
    }
    xSemaphoreGive(lock);
    return ok;
}

void auth_revoke_all(auth_role_t role)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].role == role) sessions[i].token[0] = '\0';
    }
    xSemaphoreGive(lock);
}
