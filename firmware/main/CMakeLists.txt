idf_component_register(
    SRCS "main.c" "strip.c" "buttons.c" "btn_leds.c" "highscores.c" "settings.c" "net.c" "web_server.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES game esp_driver_gpio esp_timer nvs_flash esp_wifi esp_netif esp_event esp_http_server
)
