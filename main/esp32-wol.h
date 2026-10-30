#include "esp_err.h"
#include "esp_event_base.h"

// Declare variable as unused, useful for temporary debugging.
#define UNUSED(x) ((void)(x))

/**
 * @brief Register wifi event handlers.
 */
static esp_err_t
register_wifi_event_handler(void);

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

/**
 * @brief Initialize the NVS partition.
 *
 * Attempt to initialize the NVS, if it encountered ESP_ERR_NVS_NO_FREE_PAGES
 * or ESP_ERR_NVS_NEW_VERSION_FOUND error, we try to erase it, and
 * re-initialize it.
 */
static esp_err_t
nvs_init(void);

/**
 * @brief Initialize, register event handlers, and start the Wi-Fi driver.
 */
static esp_err_t
wifi_init(void);