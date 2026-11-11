#include "web_server.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "highscores.h"
#include "sdkconfig.h"
#include "web_page.h"

static const char *TAG = "web";

#ifdef CONFIG_MNS_WIFI_ENABLE

/* Handlers run one at a time in the server task, so static buffers are fine. */
static app_snapshot_t snap;
static char body[256];
static char json[GAME_MAX_LEDS * 6 + 512];

static esp_err_t send_json(httpd_req_t *req, const char *text)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, text);
}

static esp_err_t index_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, (const char *)WEB_INDEX_GZ, WEB_INDEX_GZ_LEN);
}

static esp_err_t state_get(httpd_req_t *req)
{
    app_snapshot_get(&snap);
    char player[HS_NAME_LEN + 1];
    highscores_get_player(player, sizeof(player));

    int n = snprintf(json, sizeof(json),
                     "{\"state\":\"%s\",\"level\":%d,\"lives\":%d,\"score\":%u,"
                     "\"player\":\"%s\",\"best\":%u,\"n\":%d,\"keeps\":%s,"
                     "\"preview\":%s,\"frame\":%u,\"ma\":%u,\"leds\":\"",
                     game_state_name(snap.state), (int)snap.level, (int)snap.lives,
                     (unsigned)snap.score, player, (unsigned)highscores_best(), (int)snap.n_leds,
#ifdef CONFIG_MNS_WRONG_HIT_KEEPS_DOT
                     "true",
#else
                     "false",
#endif
                     snap.preview ? "true" : "false", (unsigned)snap.frame, (unsigned)snap.current_ma);
    static const char hex[] = "0123456789abcdef";
    for (int32_t i = 0; i < snap.n_leds && n + 8 < (int)sizeof(json); i++) {
        const game_rgb_t c = snap.leds[i];
        json[n++] = hex[c.r >> 4]; json[n++] = hex[c.r & 15];
        json[n++] = hex[c.g >> 4]; json[n++] = hex[c.g & 15];
        json[n++] = hex[c.b >> 4]; json[n++] = hex[c.b & 15];
    }
    json[n++] = '"';
    json[n++] = '}';
    json[n] = '\0';
    return send_json(req, json);
}

static esp_err_t highscores_get_handler(httpd_req_t *req)
{
    hs_entry_t entries[HS_MAX];
    int count = highscores_get(entries, HS_MAX);
    char player[HS_NAME_LEN + 1];
    highscores_get_player(player, sizeof(player));

    int n = snprintf(json, sizeof(json), "{\"player\":\"%s\",\"best\":%u,\"scores\":[",
                     player, (unsigned)highscores_best());
    for (int i = 0; i < count && n < (int)sizeof(json) - 64; i++) {
        n += snprintf(json + n, sizeof(json) - (size_t)n, "%s{\"name\":\"%s\",\"score\":%u,\"level\":%d}",
                      i ? "," : "", entries[i].name, (unsigned)entries[i].score, entries[i].level);
    }
    snprintf(json + n, sizeof(json) - (size_t)n, "]}");
    return send_json(req, json);
}

/*
 * Pull one value out of a tiny JSON object such as {"name": "Bart"}.
 * Strings and numbers are copied as text; it is all the API needs, and it
 * keeps the firmware free of a JSON library. Returns false when absent.
 */
static bool json_get(const char *json, const char *key, char *out, size_t cap)
{
    char pat[40];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
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
                if (e == 'u') {                 /* \uXXXX: not a name character anyway */
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

/* Read a small request body into the static buffer. Answers the request on error. */
static bool read_body(httpd_req_t *req)
{
    if (req->content_len >= sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body too large");
        return false;
    }
    int got = 0;
    while (got < (int)req->content_len) {
        int r = httpd_req_recv(req, body + got, req->content_len - (size_t)got);
        if (r <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body not received");
            return false;
        }
        got += r;
    }
    body[got] = '\0';
    return true;
}

static esp_err_t player_post(httpd_req_t *req)
{
    if (!read_body(req)) return ESP_OK;
    char name[64];
    if (!json_get(body, "name", name, sizeof(name))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "expected {\"name\": \"...\"}");
    }
    highscores_set_player(name);
    return highscores_get_handler(req);
}

static esp_err_t press_post(httpd_req_t *req)
{
    if (!read_body(req)) return ESP_OK;
    char color[16];
    uint8_t mask = 0;
    if (json_get(body, "color", color, sizeof(color))) {
        if (!strcmp(color, "red") || !strcmp(color, "1")) mask = GAME_RED;
        else if (!strcmp(color, "yellow") || !strcmp(color, "2")) mask = GAME_YELLOW;
        else if (!strcmp(color, "blue") || !strcmp(color, "4")) mask = GAME_BLUE;
    }
    if (mask == 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "color must be red, yellow or blue");
    }
    app_remote_press(mask);
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

static int append_colors(char *buf, int n, size_t cap, const game_rgb_t *pal)
{
    for (int i = 0; i < GAME_PALETTE_COLORS && n < (int)cap - 32; i++) {
        game_rgb_t c = pal[COLOR_NAMES[i].mask];
        n += snprintf(buf + n, cap - (size_t)n, "%s\"%s\":\"%02x%02x%02x\"",
                      i ? "," : "", COLOR_NAMES[i].name, c.r, c.g, c.b);
    }
    return n;
}

static esp_err_t palette_get(httpd_req_t *req)
{
    game_rgb_t pal[8], defaults[8];
    app_get_palette(pal);
    for (int m = 0; m < 8; m++) defaults[m] = game_color_for_mask((uint8_t)m);
    app_snapshot_get(&snap);

    int n = snprintf(json, sizeof(json), "{\"preview\":%s,\"colors\":{", snap.preview ? "true" : "false");
    n = append_colors(json, n, sizeof(json), pal);
    n += snprintf(json + n, sizeof(json) - (size_t)n, "},\"defaults\":{");
    n = append_colors(json, n, sizeof(json), defaults);
    snprintf(json + n, sizeof(json) - (size_t)n, "}}");
    return send_json(req, json);
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
    if (!changed) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "expected {\"orange\": \"ff4600\", ...}");
    }
    return palette_get(req);
}

static esp_err_t palette_save(httpd_req_t *req)
{
    if (!app_save_palette()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not write flash");
    }
    return palette_get(req);
}

static esp_err_t palette_reset(httpd_req_t *req)
{
    app_reset_palette();
    return palette_get(req);
}

static esp_err_t preview_post(httpd_req_t *req)
{
    if (!read_body(req)) return ESP_OK;
    char on[8] = "true";
    json_get(body, "on", on, sizeof(on));
    app_set_preview(strcmp(on, "false") != 0 && strcmp(on, "0") != 0);
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t highscores_delete(httpd_req_t *req)
{
    highscores_clear();
    return highscores_get_handler(req);
}

void web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6144;
    config.lru_purge_enable = true;
    config.max_uri_handlers = 16;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start: %s", esp_err_to_name(err));
        return;
    }
    const httpd_uri_t uris[] = {
        { .uri = "/",               .method = HTTP_GET,    .handler = index_get },
        { .uri = "/api/state",      .method = HTTP_GET,    .handler = state_get },
        { .uri = "/api/highscores", .method = HTTP_GET,    .handler = highscores_get_handler },
        { .uri = "/api/highscores", .method = HTTP_DELETE, .handler = highscores_delete },
        { .uri = "/api/player",     .method = HTTP_POST,   .handler = player_post },
        { .uri = "/api/press",      .method = HTTP_POST,   .handler = press_post },
        { .uri = "/api/palette",    .method = HTTP_GET,    .handler = palette_get },
        { .uri = "/api/palette",    .method = HTTP_POST,   .handler = palette_post },
        { .uri = "/api/palette/save",  .method = HTTP_POST, .handler = palette_save },
        { .uri = "/api/palette/reset", .method = HTTP_POST, .handler = palette_reset },
        { .uri = "/api/preview",    .method = HTTP_POST,   .handler = preview_post },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uris[i]));
    }
    ESP_LOGI(TAG, "web server started (page %u bytes gzipped)", (unsigned)WEB_INDEX_GZ_LEN);
}

#else

void web_server_start(void)
{
    ESP_LOGI(TAG, "web server disabled with Wi-Fi");
}

#endif
