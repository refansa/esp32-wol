idf_component_register(SRCS "esp32-wol.c" "esp32-wol-http-server.c"
                    PRIV_REQUIRES nvs_flash esp_wifi esp_http_server
                    INCLUDE_DIRS ".")
