set(SRCS
    "esp32-wol.c"
    "esp32-wol-http-server.c"
)

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS
        "."
    PRIV_REQUIRES
        microlink
        esp_event
        esp_http_server
        esp_netif
        esp_wifi
        lwip
        nvs_flash
)
