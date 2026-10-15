idf_component_register(SRCS "esp32-wol.c"
                    PRIV_REQUIRES nvs_flash
                    PRIV_REQUIRES esp_wifi
                    INCLUDE_DIRS ".")
