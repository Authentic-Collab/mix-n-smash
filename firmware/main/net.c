#include "net.h"
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "sdkconfig.h"

static const char *TAG = "net";

#ifdef CONFIG_MNS_WIFI_ENABLE

#define STA_MAX_RETRIES 6

static int retries;
static esp_timer_handle_t fallback_timer;

static void start_ap(void)
{
    wifi_config_t cfg = {0};
    strncpy((char *)cfg.ap.ssid, CONFIG_MNS_AP_SSID, sizeof(cfg.ap.ssid) - 1);
    cfg.ap.ssid_len = (uint8_t)strlen((char *)cfg.ap.ssid);
    cfg.ap.channel = 1;
    cfg.ap.max_connection = 4;
    if (strlen(CONFIG_MNS_AP_PASSWORD) >= 8) {
        strncpy((char *)cfg.ap.password, CONFIG_MNS_AP_PASSWORD, sizeof(cfg.ap.password) - 1);
        cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        cfg.ap.authmode = WIFI_AUTH_OPEN;
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "access point '%s' (%s): connect to it and open http://192.168.4.1",
             CONFIG_MNS_AP_SSID, cfg.ap.authmode == WIFI_AUTH_OPEN ? "open" : "WPA2");
}

/* Runs in the esp_timer task, outside the Wi-Fi event handler. */
static void fallback_to_ap(void *arg)
{
    (void)arg;
    ESP_LOGW(TAG, "could not join '%s': starting the access point instead", CONFIG_MNS_WIFI_SSID);
    ESP_ERROR_CHECK(esp_wifi_stop());
    start_ap();
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (retries < STA_MAX_RETRIES) {
            retries++;
            ESP_LOGI(TAG, "joining '%s' (attempt %d of %d)", CONFIG_MNS_WIFI_SSID, retries, STA_MAX_RETRIES);
            esp_wifi_connect();
        } else {
            esp_timer_start_once(fallback_timer, 100 * 1000);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        retries = 0;
        ESP_LOGI(TAG, "joined '%s': open http://" IPSTR " or http://%s.local",
                 CONFIG_MNS_WIFI_SSID, IP2STR(&ev->ip_info.ip), CONFIG_MNS_HOSTNAME);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "a device joined the access point");
    }
}

void net_init(void)
{
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

    if (strlen(CONFIG_MNS_WIFI_SSID) > 0) {
        wifi_config_t cfg = {0};
        strncpy((char *)cfg.sta.ssid, CONFIG_MNS_WIFI_SSID, sizeof(cfg.sta.ssid) - 1);
        strncpy((char *)cfg.sta.password, CONFIG_MNS_WIFI_PASSWORD, sizeof(cfg.sta.password) - 1);
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
        ESP_ERROR_CHECK(esp_wifi_start());
    } else {
        start_ap();
    }

    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(CONFIG_MNS_HOSTNAME));
    ESP_ERROR_CHECK(mdns_instance_name_set("Mix 'n Smash"));
    ESP_ERROR_CHECK(mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0));
}

#else

void net_init(void)
{
    ESP_LOGI(TAG, "Wi-Fi disabled in menuconfig");
}

#endif
