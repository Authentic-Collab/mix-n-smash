#include "web_server.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "auth.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "highscores.h"
#include "net.h"
#include "players.h"
#include "sdkconfig.h"
#include "settings.h"
#include "strip.h"
#include "web_page.h"

static const char *TAG = "web";

#ifdef CONFIG_MNS_WIFI_ENABLE

static httpd_handle_t server;

/* Handlers run one at a time in the server task, so static buffers are fine. */
static app_snapshot_t snap;
static char body[1024];
static char json[GAME_MAX_LEDS * 6 + 1024];

/* ---- Small JSON writer -------------------------------------------------- */

typedef struct {
    char *buf;
    size_t cap;
    size_t n;
} jw_t;

static void jw_init(jw_t *w, char *buf, size_t cap) { w->buf = buf; w->cap = cap; w->n = 0; buf[0] = '\0'; }

static void jw_printf(jw_t *w, const char *fmt, ...)
{
    if (w->n >= w->cap - 1) return;
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(w->buf + w->n, w->cap - w->n, fmt, ap);
    va_end(ap);
    if (k < 0) return;
    w->n += (size_t)k;
    if (w->n > w->cap - 1) w->n = w->cap - 1;
}

/* A quoted, escaped string. */
static void jw_str(jw_t *w, const char *s)
{
    jw_printf(w, "\"");
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') jw_printf(w, "\\%c", c);
        else if (c < 0x20) jw_printf(w, "\\u%04x", c);
        else jw_printf(w, "%c", c);
    }
    jw_printf(w, "\"");
}

static const char *bstr(bool b) { return b ? "true" : "false"; }

/* ---- Request helpers ---------------------------------------------------- */

static esp_err_t send_json(httpd_req_t *req, const char *text)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, text);
}

static esp_err_t send_error(httpd_req_t *req, const char *status, const char *message)
{
    char buf[200];
    jw_t w;
    jw_init(&w, buf, sizeof(buf));
    jw_printf(&w, "{\"error\":");
    jw_str(&w, message);
    jw_printf(&w, "}");
    httpd_resp_set_status(req, status);
    return send_json(req, buf);
}

/*
 * Pull one value out of a small JSON object such as {"name": "Bart"}.
 * Strings and numbers are copied as text; it is all the API needs, and it
 * keeps the firmware free of a JSON library. Returns false when absent.
 */
static bool json_get(const char *src, const char *key, char *out, size_t cap)
{
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(src, pat);
    if (!p) return false;
    p += strlen(pat);
    while (isspace((unsigned char)*p)) p++;
    if (*p != ':') return false;
    p++;
    while (isspace((unsigned char)*p)) p++;

    size_t n = 0;
    if (*p == '"') {
        p++;
        while (*p && *p != '"') {
            char c = *p++;
            if (c == '\\' && *p) {
                char e = *p++;
                if (e == 'u') {
                    for (int i = 0; i < 4 && *p; i++) p++;
                    c = '?';
                } else {
                    c = (e == 'n' || e == 't' || e == 'r') ? ' ' : e;
                }
            }
            if (n + 1 < cap) out[n++] = c;
        }
    } else {
        while (*p && (isalnum((unsigned char)*p) || *p == '-' || *p == '.')) {
            if (n + 1 < cap) out[n++] = *p;
            p++;
        }
    }
    out[n] = '\0';
    return true;
}

static bool json_true(const char *src, const char *key, bool *out)
{
    char v[8];
    if (!json_get(src, key, v, sizeof(v))) return false;
    *out = strcmp(v, "true") == 0 || strcmp(v, "1") == 0;
    return true;
}

static bool json_int(const char *src, const char *key, int32_t *out)
{
    char v[16];
    if (!json_get(src, key, v, sizeof(v))) return false;
    *out = (int32_t)strtol(v, NULL, 10);
    return true;
}

/* Read a small request body into the static buffer. Answers the request on error. */
static bool read_body(httpd_req_t *req)
{
    if (req->content_len >= sizeof(body)) {
        send_error(req, "400 Bad Request", "body too large");
        return false;
    }
    int got = 0;
    while (got < (int)req->content_len) {
        int r = httpd_req_recv(req, body + got, req->content_len - (size_t)got);
        if (r <= 0) {
            send_error(req, "400 Bad Request", "body not received");
            return false;
        }
        got += r;
    }
    body[got] = '\0';
    return true;
}

/* The value of one cookie, or false. Tokens are plain hex, so no decoding. */
static bool get_cookie(httpd_req_t *req, const char *name, char *out, size_t cap)
{
    char header[256];
    if (httpd_req_get_hdr_value_str(req, "Cookie", header, sizeof(header)) != ESP_OK) return false;
    char pat[32];
    snprintf(pat, sizeof(pat), "%s=", name);
    const char *p = header;
    while ((p = strstr(p, pat)) != NULL) {
        if (p == header || p[-1] == ' ' || p[-1] == ';') {
            p += strlen(pat);
            size_t n = 0;
            while (*p && *p != ';' && n + 1 < cap) out[n++] = *p++;
            out[n] = '\0';
            return n > 0;
        }
        p += strlen(pat);
    }
    return false;
}

static const char *cookie_name(auth_role_t role) { return role == AUTH_ADMIN ? "mns_admin" : "mns_player"; }

static bool has_role(httpd_req_t *req, auth_role_t role, char *token_out)
{
    char token[AUTH_TOKEN_LEN + 1];
    if (!get_cookie(req, cookie_name(role), token, sizeof(token))) return false;
    if (!auth_valid(role, token)) return false;
    if (token_out) strcpy(token_out, token);
    return true;
}

/* Admin or nothing: answers 401 when not logged in. */
static bool require_admin(httpd_req_t *req)
{
    if (has_role(req, AUTH_ADMIN, NULL)) return true;
    send_error(req, "401 Unauthorized", "settings password needed");
    return false;
}

static bool require_player(httpd_req_t *req, char *token_out)
{
    if (has_role(req, AUTH_PLAYER, token_out)) return true;
    send_error(req, "401 Unauthorized", "player password needed");
    return false;
}

static void set_cookie(httpd_req_t *req, auth_role_t role, const char *token)
{
    static char header[96];        /* must outlive the call: the header is sent later */
    snprintf(header, sizeof(header), "%s=%s; Path=/; HttpOnly; SameSite=Lax", cookie_name(role), token);
    httpd_resp_set_hdr(req, "Set-Cookie", header);
}

/* ---- Pages and state ---------------------------------------------------- */

static esp_err_t index_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, (const char *)WEB_INDEX_GZ, WEB_INDEX_GZ_LEN);
}

/* The state message, shared by GET /api/state and the WebSocket. */
static size_t build_state(char *buf, size_t cap)
{
    app_snapshot_get(&snap);
    char player[HS_NAME_LEN + 1] = "";
    players_current_name(player);
    mns_settings_t s;
    settings_get(&s);

    jw_t w;
    jw_init(&w, buf, cap);
    jw_printf(&w, "{\"state\":\"%s\",\"level\":%d,\"lives\":%d,\"score\":%u,\"player\":",
              game_state_name(snap.state), (int)snap.level, (int)snap.lives, (unsigned)snap.score);
    jw_str(&w, player);
    jw_printf(&w, ",\"best\":%u,\"n\":%d,\"keeps\":%s,\"preview\":%s,\"frame\":%u,\"ma\":%u,"
              "\"play_enabled\":%s,\"admin_set\":%s,\"leds\":\"",
              (unsigned)highscores_best(), (int)snap.n_leds, bstr(s.wrong_hit_keeps_dot), bstr(snap.preview),
              (unsigned)snap.frame, (unsigned)snap.current_ma,
              bstr(settings_password_set(AUTH_PLAYER)), bstr(settings_password_set(AUTH_ADMIN)));
    static const char hex[] = "0123456789abcdef";
    size_t n = w.n;
    for (int32_t i = 0; i < snap.n_leds && n + 8 < cap; i++) {
        const game_rgb_t c = snap.leds[i];
        buf[n++] = hex[c.r >> 4]; buf[n++] = hex[c.r & 15];
        buf[n++] = hex[c.g >> 4]; buf[n++] = hex[c.g & 15];
        buf[n++] = hex[c.b >> 4]; buf[n++] = hex[c.b & 15];
    }
    buf[n++] = '"';
    buf[n++] = '}';
    buf[n] = '\0';
    return n;
}

static esp_err_t state_get(httpd_req_t *req)
{
    build_state(json, sizeof(json));
    return send_json(req, json);
}

/* ---- WebSocket ---------------------------------------------------------- */

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) return ESP_OK;          /* handshake done */
    /* Clients send nothing we need; drain whatever arrives. */
    httpd_ws_frame_t frame = { .type = HTTPD_WS_TYPE_TEXT };
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) return err;
    if (frame.len > 0 && frame.len < 256) {
        uint8_t tmp[256];
        frame.payload = tmp;
        err = httpd_ws_recv_frame(req, &frame, frame.len);
    }
    return err;
}

static bool broadcast_queued;

static void ws_broadcast_work(void *arg)
{
    (void)arg;
    broadcast_queued = false;
    static char msg[GAME_MAX_LEDS * 6 + 1024];
    size_t len = build_state(msg, sizeof(msg));
    httpd_ws_frame_t frame = { .type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t *)msg, .len = len };

    size_t count = CONFIG_LWIP_MAX_SOCKETS;
    int fds[CONFIG_LWIP_MAX_SOCKETS];
    if (httpd_get_client_list(server, &count, fds) != ESP_OK) return;
    for (size_t i = 0; i < count; i++) {
        if (httpd_ws_get_fd_info(server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            httpd_ws_send_frame_async(server, fds[i], &frame);
        }
    }
}

void web_server_broadcast(void)
{
    if (!server || broadcast_queued) return;
    broadcast_queued = true;
    if (httpd_queue_work(server, ws_broadcast_work, NULL) != ESP_OK) broadcast_queued = false;
}

/* ---- High scores -------------------------------------------------------- */

static esp_err_t highscores_get_handler(httpd_req_t *req)
{
    hs_entry_t entries[HS_MAX];
    int count = highscores_get(entries, HS_MAX);
    jw_t w;
    jw_init(&w, json, sizeof(json));
    jw_printf(&w, "{\"best\":%u,\"scores\":[", (unsigned)highscores_best());
    for (int i = 0; i < count; i++) {
        jw_printf(&w, "%s{\"name\":", i ? "," : "");
        jw_str(&w, entries[i].name);
        jw_printf(&w, ",\"score\":%u,\"level\":%d}", (unsigned)entries[i].score, entries[i].level);
    }
    jw_printf(&w, "]}");
    return send_json(req, json);
}

static esp_err_t highscores_delete(httpd_req_t *req)
{
    if (!require_admin(req)) return ESP_OK;
    highscores_clear();
    return highscores_get_handler(req);
}

/* ---- Login -------------------------------------------------------------- */

static esp_err_t auth_get(httpd_req_t *req)
{
    jw_t w;
    jw_init(&w, json, sizeof(json));
    jw_printf(&w, "{\"admin_set\":%s,\"play_enabled\":%s,\"admin\":%s,\"player\":%s}",
              bstr(settings_password_set(AUTH_ADMIN)), bstr(settings_password_set(AUTH_PLAYER)),
              bstr(has_role(req, AUTH_ADMIN, NULL)), bstr(has_role(req, AUTH_PLAYER, NULL)));
    return send_json(req, json);
}

static bool parse_role(const char *src, auth_role_t *role)
{
    char r[16];
    if (!json_get(src, "role", r, sizeof(r))) return false;
    if (strcmp(r, "admin") == 0) { *role = AUTH_ADMIN; return true; }
    if (strcmp(r, "player") == 0) { *role = AUTH_PLAYER; return true; }
    return false;
}

static esp_err_t login_post(httpd_req_t *req)
{
    if (!read_body(req)) return ESP_OK;
    auth_role_t role;
    char password[SET_PASS_LEN + 1];
    if (!parse_role(body, &role) || !json_get(body, "password", password, sizeof(password))) {
        return send_error(req, "400 Bad Request", "expected {\"role\": \"admin\"|\"player\", \"password\": \"...\"}");
    }
    char token[AUTH_TOKEN_LEN + 1];
    switch (auth_login(role, password, token)) {
    case AUTH_LOGIN_OK:
        set_cookie(req, role, token);
        return auth_get(req);
    case AUTH_LOGIN_TOO_SOON:
        return send_error(req, "429 Too Many Requests", "wait two seconds and try again");
    case AUTH_LOGIN_NOT_SET:
        return send_error(req, "403 Forbidden", "no such password is set");
    default:
        return send_error(req, "401 Unauthorized", "wrong password");
    }
}

static esp_err_t logout_post(httpd_req_t *req)
{
    char token[AUTH_TOKEN_LEN + 1];
    if (get_cookie(req, "mns_admin", token, sizeof(token))) auth_logout(token);
    if (get_cookie(req, "mns_player", token, sizeof(token))) {
        players_leave(token);
        auth_logout(token);
    }
    httpd_resp_set_hdr(req, "Set-Cookie", "mns_admin=; Path=/; Max-Age=0");
    return auth_get(req);
}

static esp_err_t password_post(httpd_req_t *req)
{
    if (!read_body(req)) return ESP_OK;
    auth_role_t role;
    char password[SET_PASS_LEN + 1];
    if (!parse_role(body, &role) || !json_get(body, "password", password, sizeof(password))) {
        return send_error(req, "400 Bad Request", "expected {\"role\": ..., \"password\": ...}");
    }
    bool first_admin = role == AUTH_ADMIN && !settings_password_set(AUTH_ADMIN);
    if (!first_admin && !require_admin(req)) return ESP_OK;
    if (role == AUTH_ADMIN && strlen(password) < 4) {
        return send_error(req, "400 Bad Request", "the settings password needs at least 4 characters");
    }
    if (role == AUTH_PLAYER && password[0] && strlen(password) < 2) {
        return send_error(req, "400 Bad Request", "the player password needs at least 2 characters");
    }
    if (!settings_password_store(role, password)) return send_error(req, "500 Internal Server Error", "could not write flash");
    auth_revoke_all(role);
    if (role == AUTH_ADMIN) {
        char token[AUTH_TOKEN_LEN + 1];
        if (auth_login(AUTH_ADMIN, password, token) == AUTH_LOGIN_OK) set_cookie(req, AUTH_ADMIN, token);
    }
    return auth_get(req);
}

/* ---- Players and the queue ---------------------------------------------- */

static esp_err_t queue_json(httpd_req_t *req, int32_t my_position)
{
    player_info_t current, list[PLAYERS_MAX_QUEUE];
    int32_t n = players_list(&current, list, PLAYERS_MAX_QUEUE);
    mns_settings_t s;
    settings_get(&s);
    jw_t w;
    jw_init(&w, json, sizeof(json));
    jw_printf(&w, "{\"current\":");
    if (current.games < 0) {
        jw_printf(&w, "null");
    } else {
        jw_printf(&w, "{\"name\":");
        jw_str(&w, current.name);
        jw_printf(&w, ",\"games\":%d}", (int)current.games);
    }
    jw_printf(&w, ",\"queue\":[");
    for (int32_t i = 0; i < n; i++) {
        jw_printf(&w, "%s{\"name\":", i ? "," : "");
        jw_str(&w, list[i].name);
        jw_printf(&w, "}");
    }
    jw_printf(&w, "],\"me\":%d,\"pending\":%d,\"games_per_turn\":%d,\"running\":%s}",
              (int)my_position, (int)players_pending(), (int)s.games_per_turn, bstr(app_game_running()));
    return send_json(req, json);
}

static esp_err_t queue_get(httpd_req_t *req)
{
    char token[AUTH_TOKEN_LEN + 1];
    if (has_role(req, AUTH_PLAYER, token)) return queue_json(req, players_seen(token));
    if (has_role(req, AUTH_ADMIN, NULL)) return queue_json(req, -1);
    return send_error(req, "401 Unauthorized", "player password needed");
}

static esp_err_t queue_post(httpd_req_t *req)
{
    char token[AUTH_TOKEN_LEN + 1];
    if (!require_player(req, token)) return ESP_OK;
    if (!read_body(req)) return ESP_OK;
    char name[64];
    if (!json_get(body, "name", name, sizeof(name))) return send_error(req, "400 Bad Request", "expected {\"name\": ...}");
    bool accept = false;
    json_true(body, "accept_suffix", &accept);
    char suggestion[HS_NAME_LEN + 1];
    int32_t position = -1;
    switch (players_join(token, name, accept, suggestion, &position)) {
    case PLAYERS_OK:
        return queue_json(req, position);
    case PLAYERS_NAME_TAKEN: {
        char buf[160];
        jw_t w;
        jw_init(&w, buf, sizeof(buf));
        jw_printf(&w, "{\"error\":\"name in use\",\"suggestion\":");
        jw_str(&w, suggestion);
        jw_printf(&w, "}");
        httpd_resp_set_status(req, "409 Conflict");
        return send_json(req, buf);
    }
    case PLAYERS_FULL:
        return send_error(req, "409 Conflict", "the queue is full");
    default:
        return send_error(req, "400 Bad Request", "give a name with letters or digits");
    }
}

static esp_err_t queue_delete(httpd_req_t *req)
{
    char token[AUTH_TOKEN_LEN + 1];
    if (!require_player(req, token)) return ESP_OK;
    players_leave(token);
    return queue_json(req, -1);
}

static esp_err_t queue_admin_post(httpd_req_t *req)
{
    if (!require_admin(req)) return ESP_OK;
    if (!read_body(req)) return ESP_OK;
    char action[16];
    int32_t index = -1;
    json_get(body, "action", action, sizeof(action));
    json_int(body, "index", &index);
    bool ok;
    if (strcmp(action, "activate") == 0) ok = players_admin_activate(index, app_game_running());
    else if (strcmp(action, "remove") == 0) ok = players_admin_remove(index);
    else return send_error(req, "400 Bad Request", "action must be activate or remove");
    if (!ok) return send_error(req, "400 Bad Request", "no such entry");
    return queue_json(req, -1);
}

static esp_err_t press_post(httpd_req_t *req)
{
    char token[AUTH_TOKEN_LEN + 1];
    if (!require_player(req, token)) return ESP_OK;
    if (!players_is_current(token)) return send_error(req, "403 Forbidden", "not your turn");
    if (!read_body(req)) return ESP_OK;
    char color[16];
    uint8_t mask = 0;
    if (json_get(body, "color", color, sizeof(color))) {
        if (!strcmp(color, "red") || !strcmp(color, "1")) mask = GAME_RED;
        else if (!strcmp(color, "yellow") || !strcmp(color, "2")) mask = GAME_YELLOW;
        else if (!strcmp(color, "blue") || !strcmp(color, "4")) mask = GAME_BLUE;
    }
    if (mask == 0) return send_error(req, "400 Bad Request", "color must be red, yellow or blue");
    players_seen(token);
    app_remote_press(mask);
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t game_abort_post(httpd_req_t *req)
{
    if (!require_admin(req)) return ESP_OK;
    app_abort_game();
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

/* ---- Settings ----------------------------------------------------------- */

static esp_err_t settings_json(httpd_req_t *req)
{
    mns_settings_t s;
    settings_get(&s);
    char network[128], ip[20];
    net_status(network, sizeof(network), ip, sizeof(ip));
    jw_t w;
    jw_init(&w, json, sizeof(json));
    jw_printf(&w, "{\"ap_ssid\":"); jw_str(&w, s.ap_ssid);
    jw_printf(&w, ",\"ap_password_set\":%s,\"sta_ssid\":", bstr(s.ap_password[0] != '\0')); jw_str(&w, s.sta_ssid);
    jw_printf(&w, ",\"sta_password_set\":%s,\"hostname\":", bstr(s.sta_password[0] != '\0')); jw_str(&w, s.hostname);
    jw_printf(&w, ",\"led_count\":%d,\"led_gpio\":%d,\"brightness\":%d,\"max_current_ma\":%d,\"rgb_order\":%s,",
              (int)s.led_count, (int)s.led_gpio, (int)s.brightness, (int)s.max_current_ma, bstr(s.rgb_order));
    jw_printf(&w, "\"wrong_hit_keeps_dot\":%s,\"level1_dots\":%d,\"dots_per_level\":%d,\"speed_pct\":%d,\"games_per_turn\":%d,",
              bstr(s.wrong_hit_keeps_dot), (int)s.level1_dots, (int)s.dots_per_level, (int)s.speed_pct, (int)s.games_per_turn);
    jw_printf(&w, "\"btn_red_gpio\":%d,\"btn_yellow_gpio\":%d,\"btn_blue_gpio\":%d,\"btn_active_high\":%s,",
              (int)s.btn_gpio[0], (int)s.btn_gpio[1], (int)s.btn_gpio[2], bstr(s.btn_active_high));
    jw_printf(&w, "\"lights\":%s,\"light_red_gpio\":%d,\"light_yellow_gpio\":%d,\"light_blue_gpio\":%d,\"lights_active_high\":%s,",
              bstr(s.lights), (int)s.light_gpio[0], (int)s.light_gpio[1], (int)s.light_gpio[2], bstr(s.lights_active_high));
    jw_printf(&w, "\"player_password_set\":%s,\"restart_needed\":%s,", bstr(settings_password_set(AUTH_PLAYER)),
              bstr(settings_restart_needed()));
    jw_printf(&w, "\"status\":{\"version\":\"%s\",\"network\":", esp_app_get_description()->version);
    jw_str(&w, network);
    jw_printf(&w, ",\"ip\":\"%s\",\"ma\":%u,\"uptime_s\":%lld}}", ip, (unsigned)strip_last_current_ma(),
              (long long)(esp_timer_get_time() / 1000000));
    return send_json(req, json);
}

static esp_err_t settings_get_handler(httpd_req_t *req)
{
    if (!require_admin(req)) return ESP_OK;
    return settings_json(req);
}

static void get_str_field(const char *key, char *dst, size_t cap)
{
    char v[SET_PASS_LEN + 1];
    if (json_get(body, key, v, sizeof(v))) strlcpy(dst, v, cap);
}

static esp_err_t settings_post(httpd_req_t *req)
{
    if (!require_admin(req)) return ESP_OK;
    if (!read_body(req)) return ESP_OK;
    mns_settings_t s;
    settings_get(&s);
    get_str_field("ap_ssid", s.ap_ssid, sizeof(s.ap_ssid));
    get_str_field("ap_password", s.ap_password, sizeof(s.ap_password));
    get_str_field("sta_ssid", s.sta_ssid, sizeof(s.sta_ssid));
    get_str_field("sta_password", s.sta_password, sizeof(s.sta_password));
    get_str_field("hostname", s.hostname, sizeof(s.hostname));
    json_int(body, "led_count", &s.led_count);
    json_int(body, "led_gpio", &s.led_gpio);
    json_int(body, "brightness", &s.brightness);
    json_int(body, "max_current_ma", &s.max_current_ma);
    json_true(body, "rgb_order", &s.rgb_order);
    json_true(body, "wrong_hit_keeps_dot", &s.wrong_hit_keeps_dot);
    json_int(body, "level1_dots", &s.level1_dots);
    json_int(body, "dots_per_level", &s.dots_per_level);
    json_int(body, "speed_pct", &s.speed_pct);
    json_int(body, "games_per_turn", &s.games_per_turn);
    json_int(body, "btn_red_gpio", &s.btn_gpio[0]);
    json_int(body, "btn_yellow_gpio", &s.btn_gpio[1]);
    json_int(body, "btn_blue_gpio", &s.btn_gpio[2]);
    json_true(body, "btn_active_high", &s.btn_active_high);
    json_true(body, "lights", &s.lights);
    json_int(body, "light_red_gpio", &s.light_gpio[0]);
    json_int(body, "light_yellow_gpio", &s.light_gpio[1]);
    json_int(body, "light_blue_gpio", &s.light_gpio[2]);
    json_true(body, "lights_active_high", &s.lights_active_high);
    const char *err = settings_validate(&s);
    if (err) return send_error(req, "400 Bad Request", err);
    if (!settings_save(&s)) return send_error(req, "500 Internal Server Error", "could not write flash");
    app_settings_changed();
    return settings_json(req);
}

static esp_err_t settings_reset_post(httpd_req_t *req)
{
    if (!require_admin(req)) return ESP_OK;
    settings_reset();
    app_settings_changed();
    app_reset_palette();
    return settings_json(req);
}

static esp_err_t restart_post(httpd_req_t *req)
{
    if (!require_admin(req)) return ESP_OK;
    app_restart();
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

/* ---- Palette ------------------------------------------------------------ */

typedef struct {
    const char *name;
    uint8_t mask;
} color_name_t;

static const color_name_t COLOR_NAMES[GAME_PALETTE_COLORS] = {
    {"red", GAME_RED}, {"orange", GAME_ORANGE}, {"yellow", GAME_YELLOW}, {"green", GAME_GREEN},
    {"blue", GAME_BLUE}, {"purple", GAME_PURPLE}, {"white", GAME_WHITE},
};

static void jw_colors(jw_t *w, const game_rgb_t *pal)
{
    for (int i = 0; i < GAME_PALETTE_COLORS; i++) {
        game_rgb_t c = pal[COLOR_NAMES[i].mask];
        jw_printf(w, "%s\"%s\":\"%02x%02x%02x\"", i ? "," : "", COLOR_NAMES[i].name, c.r, c.g, c.b);
    }
}

static esp_err_t palette_json(httpd_req_t *req)
{
    game_rgb_t pal[8], defaults[8];
    app_get_palette(pal);
    for (int m = 0; m < 8; m++) defaults[m] = game_color_for_mask((uint8_t)m);
    app_snapshot_get(&snap);
    jw_t w;
    jw_init(&w, json, sizeof(json));
    jw_printf(&w, "{\"preview\":%s,\"colors\":{", bstr(snap.preview));
    jw_colors(&w, pal);
    jw_printf(&w, "},\"defaults\":{");
    jw_colors(&w, defaults);
    jw_printf(&w, "}}");
    return send_json(req, json);
}

static esp_err_t palette_get(httpd_req_t *req)
{
    if (!require_admin(req)) return ESP_OK;
    return palette_json(req);
}

static bool parse_hex_color(const char *text, game_rgb_t *out)
{
    if (*text == '#') text++;
    if (strlen(text) != 6) return false;
    unsigned v = 0;
    for (int i = 0; i < 6; i++) {
        char c = text[i];
        unsigned d;
        if (c >= '0' && c <= '9') d = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (unsigned)(c - 'A' + 10);
        else return false;
        v = (v << 4) | d;
    }
    out->r = (uint8_t)(v >> 16);
    out->g = (uint8_t)(v >> 8);
    out->b = (uint8_t)v;
    return true;
}

static esp_err_t palette_post(httpd_req_t *req)
{
    if (!require_admin(req)) return ESP_OK;
    if (!read_body(req)) return ESP_OK;
    int changed = 0;
    for (int i = 0; i < GAME_PALETTE_COLORS; i++) {
        char hex[16];
        game_rgb_t c;
        if (json_get(body, COLOR_NAMES[i].name, hex, sizeof(hex)) && parse_hex_color(hex, &c)) {
            app_set_color(COLOR_NAMES[i].mask, c);
            changed++;
        }
    }
    if (!changed) return send_error(req, "400 Bad Request", "expected {\"orange\": \"ff4600\", ...}");
    return palette_json(req);
}

static esp_err_t palette_save(httpd_req_t *req)
{
    if (!require_admin(req)) return ESP_OK;
    if (!app_save_palette()) return send_error(req, "500 Internal Server Error", "could not write flash");
    return palette_json(req);
}

static esp_err_t palette_reset(httpd_req_t *req)
{
    if (!require_admin(req)) return ESP_OK;
    app_reset_palette();
    return palette_json(req);
}

static esp_err_t preview_post(httpd_req_t *req)
{
    if (!require_admin(req)) return ESP_OK;
    if (!read_body(req)) return ESP_OK;
    bool on = true;
    json_true(body, "on", &on);
    app_set_preview(on);
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

/* ---- Firmware update ---------------------------------------------------- */

static esp_err_t ota_post(httpd_req_t *req)
{
    if (!require_admin(req)) return ESP_OK;
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) return send_error(req, "500 Internal Server Error", "no update partition");
    if (req->content_len < 64 * 1024 || req->content_len > part->size) {
        return send_error(req, "400 Bad Request", "that does not look like a firmware image");
    }
    ESP_LOGI(TAG, "firmware update: %u bytes into %s", (unsigned)req->content_len, part->label);

    esp_ota_handle_t ota;
    esp_err_t err = esp_ota_begin(part, OTA_SIZE_UNKNOWN, &ota);
    if (err != ESP_OK) return send_error(req, "500 Internal Server Error", esp_err_to_name(err));

    static char chunk[4096];
    size_t received = 0;
    while (received < req->content_len) {
        int r = httpd_req_recv(req, chunk, sizeof(chunk));
        if (r <= 0) {
            esp_ota_abort(ota);
            return send_error(req, "400 Bad Request", "upload interrupted");
        }
        err = esp_ota_write(ota, chunk, (size_t)r);
        if (err != ESP_OK) {
            esp_ota_abort(ota);
            return send_error(req, "400 Bad Request", esp_err_to_name(err));
        }
        received += (size_t)r;
    }
    err = esp_ota_end(ota);
    if (err == ESP_OK) err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) return send_error(req, "400 Bad Request", esp_err_to_name(err));
    ESP_LOGI(TAG, "firmware update written, restarting");
    app_restart();
    return send_json(req, "{\"ok\":true,\"restarting\":true}");
}

/* ---- Server ------------------------------------------------------------- */

void web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.lru_purge_enable = true;
    config.max_uri_handlers = 40;
    config.max_open_sockets = CONFIG_LWIP_MAX_SOCKETS - 3;
    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start: %s", esp_err_to_name(err));
        server = NULL;
        return;
    }
    const httpd_uri_t uris[] = {
        { .uri = "/",                   .method = HTTP_GET,    .handler = index_get },
        { .uri = "/play",               .method = HTTP_GET,    .handler = index_get },
        { .uri = "/settings",           .method = HTTP_GET,    .handler = index_get },
        { .uri = "/practice",           .method = HTTP_GET,    .handler = index_get },
        { .uri = "/ws",                 .method = HTTP_GET,    .handler = ws_handler, .is_websocket = true },
        { .uri = "/api/state",          .method = HTTP_GET,    .handler = state_get },
        { .uri = "/api/highscores",     .method = HTTP_GET,    .handler = highscores_get_handler },
        { .uri = "/api/highscores",     .method = HTTP_DELETE, .handler = highscores_delete },
        { .uri = "/api/auth",           .method = HTTP_GET,    .handler = auth_get },
        { .uri = "/api/login",          .method = HTTP_POST,   .handler = login_post },
        { .uri = "/api/logout",         .method = HTTP_POST,   .handler = logout_post },
        { .uri = "/api/password",       .method = HTTP_POST,   .handler = password_post },
        { .uri = "/api/queue",          .method = HTTP_GET,    .handler = queue_get },
        { .uri = "/api/queue",          .method = HTTP_POST,   .handler = queue_post },
        { .uri = "/api/queue",          .method = HTTP_DELETE, .handler = queue_delete },
        { .uri = "/api/queue/admin",    .method = HTTP_POST,   .handler = queue_admin_post },
        { .uri = "/api/press",          .method = HTTP_POST,   .handler = press_post },
        { .uri = "/api/game/abort",     .method = HTTP_POST,   .handler = game_abort_post },
        { .uri = "/api/settings",       .method = HTTP_GET,    .handler = settings_get_handler },
        { .uri = "/api/settings",       .method = HTTP_POST,   .handler = settings_post },
        { .uri = "/api/settings/reset", .method = HTTP_POST,   .handler = settings_reset_post },
        { .uri = "/api/restart",        .method = HTTP_POST,   .handler = restart_post },
        { .uri = "/api/palette",        .method = HTTP_GET,    .handler = palette_get },
        { .uri = "/api/palette",        .method = HTTP_POST,   .handler = palette_post },
        { .uri = "/api/palette/save",   .method = HTTP_POST,   .handler = palette_save },
        { .uri = "/api/palette/reset",  .method = HTTP_POST,   .handler = palette_reset },
        { .uri = "/api/preview",        .method = HTTP_POST,   .handler = preview_post },
        { .uri = "/api/ota",            .method = HTTP_POST,   .handler = ota_post },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uris[i]));
    }
    ESP_LOGI(TAG, "web server started (page %u bytes gzipped, version %s)", (unsigned)WEB_INDEX_GZ_LEN,
             esp_app_get_description()->version);
}

#else

void web_server_start(void)
{
    ESP_LOGI(TAG, "web server disabled with Wi-Fi");
}

void web_server_broadcast(void) {}

#endif
