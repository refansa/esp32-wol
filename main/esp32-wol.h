#include "esp_err.h"
#include "esp_event_base.h"

static esp_err_t
register_wifi_event_handler();

static void
event_sta_start_handler(void* arg,
                        esp_event_base_t event_base,
                        int32_t event_id,
                        void* event_data);

static void
event_sta_disconnected_handler(void* arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void* event_data);

static void
event_sta_connected_handler(void* arg,
                            esp_event_base_t event_base,
                            int32_t event_id,
                            void* event_data);