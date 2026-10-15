
/*
 * ESP-32 WoL
 *
 * Minimal Wake-over-LAN implementation reachable over the internet.
 */

#include "esp32-wol.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_types.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_wifi_types_generic.h"
#include "lwip/err.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h" // IWYU pragma: keep this for the CONFIG_* macros.

const static char* TAG = "esp32-wol";

static int retried_attempt = 0;

static void
event_sta_start_handler(void* arg,
                        esp_event_base_t event_base,
                        int32_t event_id,
                        void* event_data)
{
  esp_wifi_connect();
}

static void
event_sta_disconnected_handler(void* arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void* event_data)
{
  wifi_event_sta_disconnected_t* event =
    (wifi_event_sta_disconnected_t*)event_data;
  if (event->reason == WIFI_REASON_AUTH_EXPIRE) {
    ESP_LOGI(TAG, "This device have been blacklisted by the AP!");
    retried_attempt = 5;
  }

  if (retried_attempt < CONFIG_WOL_WIFI_RETRY_ATTEMPT) {
    esp_wifi_connect();
    retried_attempt++;
    ESP_LOGI(TAG, "Attempting to reconnect to the AP (%d)", retried_attempt);
  } else {
    ESP_LOGE(TAG, "Failed to connect to the SSID: %s", CONFIG_WOL_WIFI_SSID);
  }
}

static void
event_sta_connected_handler(void* arg,
                            esp_event_base_t event_base,
                            int32_t event_id,
                            void* event_data)
{
  ESP_LOGI(TAG, "Successfully connected to the SSID: %s", CONFIG_WOL_WIFI_SSID);
}

esp_err_t
register_wifi_event_handler(void)
{
  esp_event_handler_instance_t instance_sta_start;
  esp_event_handler_instance_t instance_sta_disconnected;
  esp_event_handler_instance_t instance_sta_connected;

  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                      WIFI_EVENT_STA_START,
                                                      &event_sta_start_handler,
                                                      NULL,
                                                      &instance_sta_start));
  ESP_ERROR_CHECK(
    esp_event_handler_instance_register(WIFI_EVENT,
                                        WIFI_EVENT_STA_DISCONNECTED,
                                        &event_sta_disconnected_handler,
                                        NULL,
                                        &instance_sta_disconnected));
  ESP_ERROR_CHECK(
    esp_event_handler_instance_register(WIFI_EVENT,
                                        WIFI_EVENT_STA_CONNECTED,
                                        &event_sta_connected_handler,
                                        NULL,
                                        &instance_sta_connected));

  return ERR_OK;
}

/**
 * @brief Initialize the NVS partition.
 *
 * Attempt to initialize the NVS, if it encountered ESP_ERR_NVS_NO_FREE_PAGES
 * or ESP_ERR_NVS_NEW_VERSION_FOUND error, we try to erase it, and
 * re-initialize it.
 *
 */
esp_err_t
nvs_init()
{
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  return ret;
}

/**
 * @brief Initialize, register event handlers, and start the Wi-Fi driver.
 *
 */
esp_err_t
wifi_init()
{
  wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
  wifi_config_t config = {
        .sta = {
            .ssid = CONFIG_WOL_WIFI_SSID,
            .password = CONFIG_WOL_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

  // Initialize netif for the tcp/ip stack.
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_t* netif = esp_netif_create_default_wifi_sta();

  // Set Wi-Fi hostname.
  ESP_ERROR_CHECK(esp_netif_set_hostname(netif, CONFIG_WOL_WIFI_HOSTNAME));

  // Initialize the Wi-Fi stack.
  ESP_ERROR_CHECK(esp_wifi_init(&init_config));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));

  ESP_ERROR_CHECK(register_wifi_event_handler());

  return esp_wifi_start();
}

void
app_main(void)
{
  ESP_LOGI(TAG, "CONFIG_WOL_WIFI_SSID           : %s", CONFIG_WOL_WIFI_SSID);
  ESP_LOGI(
    TAG, "CONFIG_WOL_WIFI_PASSWORD       : %s", CONFIG_WOL_WIFI_PASSWORD);
  ESP_LOGI(
    TAG, "CONFIG_WOL_WIFI_HOSTNAME       : %s", CONFIG_WOL_WIFI_HOSTNAME);
  ESP_LOGI(
    TAG, "CONFIG_WOL_WIFI_RETRY_ATTEMPT  : %d", CONFIG_WOL_WIFI_RETRY_ATTEMPT);

  if (CONFIG_WOL_WIFI_RETRY_ATTEMPT < 0) {
    ESP_LOGE(TAG,
             "CONFIG_WOL_WIFI_RETRY_ATTEMPT cannot be less than 0",
             CONFIG_WOL_WIFI_RETRY_ATTEMPT);
    return;
  }

  ESP_ERROR_CHECK(nvs_init());
  ESP_ERROR_CHECK(wifi_init());
}
