#include "net.h"
#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "sdkconfig.h"

static const char *TAG = "net";

static char status_network[128] = "Wi-Fi off";
static char status_ip[20] = "";

void net_status(char *network, size_t network_cap, char *ip, size_t ip_cap)
{
    strlcpy(network, status_network, network_cap);
    strlcpy(ip, status_ip, ip_cap);
}

#ifdef CONFIG_MNS_WIFI_ENABLE

#define STA_MAX_RETRIES 6

static mns_settings_t cfg;
static int retries;
static esp_timer_handle_t fallback_timer;

static void start_ap(void)
{
    wifi_config_t wc = {0};
    size_t n = strlen(cfg.ap_ssid);
    if (n > sizeof(wc.ap.ssid)) n = sizeof(wc.ap.ssid);
    memcpy(wc.ap.ssid, cfg.ap_ssid, n);
    wc.ap.ssid_len = (uint8_t)n;
    wc.ap.channel = 1;
    wc.ap.max_connection = 8;
    if (strlen(cfg.ap_password) >= 8) {
        size_t pn = strlen(cfg.ap_password);
        if (pn > sizeof(wc.ap.password)) pn = sizeof(wc.ap.password);
        memcpy(wc.ap.password, cfg.ap_password, pn);
        wc.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        wc.ap.authmode = WIFI_AUTH_OPEN;
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    snprintf(status_network, sizeof(status_network), "access point '%s' (%s)", cfg.ap_ssid,
             wc.ap.authmode == WIFI_AUTH_OPEN ? "open" : "WPA2");
    strcpy(status_ip, "192.168.4.1");
    ESP_LOGI(TAG, "access point '%s': connect to it and open http://192.168.4.1", cfg.ap_ssid);
}

/* Runs in the esp_timer task, outside the Wi-Fi event handler. */
static void fallback_to_ap(void *arg)
{
    (void)arg;
    ESP_LOGW(TAG, "could not join '%s': starting the access point instead", cfg.sta_ssid);
    ESP_ERROR_CHECK(esp_wifi_stop());
    start_ap();
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        status_ip[0] = '\0';
        if (retries < STA_MAX_RETRIES) {
            retries++;
            snprintf(status_network, sizeof(status_network), "joining '%s' (attempt %d)", cfg.sta_ssid, retries);
            ESP_LOGI(TAG, "joining '%s' (attempt %d of %d)", cfg.sta_ssid, retries, STA_MAX_RETRIES);
            esp_wifi_connect();
        } else {
            esp_timer_start_once(fallback_timer, 100 * 1000);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        retries = 0;
        snprintf(status_network, sizeof(status_network), "joined '%s'", cfg.sta_ssid);
        snprintf(status_ip, sizeof(status_ip), IPSTR, IP2STR(&ev->ip_info.ip));
        ESP_LOGI(TAG, "joined '%s': open http://%s or http://%s.local", cfg.sta_ssid, status_ip, cfg.hostname);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "a device joined the access point");
    }
}

void net_init(const mns_settings_t *s)
{
    cfg = *s;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));

    const esp_timer_create_args_t targs = { .callback = fallback_to_ap, .name = "ap-fallback" };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &fallback_timer));

    if (cfg.sta_ssid[0]) {
        wifi_config_t wc = {0};
        size_t sn = strlen(cfg.sta_ssid), pn = strlen(cfg.sta_password);
        if (sn > sizeof(wc.sta.ssid)) sn = sizeof(wc.sta.ssid);
        if (pn > sizeof(wc.sta.password)) pn = sizeof(wc.sta.password);
        memcpy(wc.sta.ssid, cfg.sta_ssid, sn);
        memcpy(wc.sta.password, cfg.sta_password, pn);
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
        ESP_ERROR_CHECK(esp_wifi_start());
        snprintf(status_network, sizeof(status_network), "joining '%s'", cfg.sta_ssid);
    } else {
        start_ap();
    }

    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(cfg.hostname));
    ESP_ERROR_CHECK(mdns_instance_name_set("Mix 'n Smash"));
    ESP_ERROR_CHECK(mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0));
}

#else

void net_init(const mns_settings_t *s)
{
    (void)s;
    ESP_LOGI(TAG, "Wi-Fi disabled in menuconfig");
}

#endif
