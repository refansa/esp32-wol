
/*
 * ESP-32 WoL
 *
 * Minimal Wake-over-LAN implementation reachable over the internet through
 * Wi-Fi connection.
 */

#include "esp32-wol.h"
#include "esp32-wol-http-server.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_server.h"
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
#include <string.h>

const static char* TAG = "esp32-wol";

static int retried_attempt = 0;
static httpd_handle_t http_server = NULL;

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
  if (retried_attempt < CONFIG_WOL_WIFI_RETRY_ATTEMPT) {
    esp_wifi_connect();
    retried_attempt++;
    ESP_LOGI(TAG, "Attempting to reconnect to the AP (%d)", retried_attempt);
  } else {
    ESP_LOGE(TAG, "Failed to connect to the SSID: %s", CONFIG_WOL_WIFI_SSID);
    ESP_ERROR_CHECK(stop_http_server(http_server));
  }
}

static void
event_sta_connected_handler(void* arg,
                            esp_event_base_t event_base,
                            int32_t event_id,
                            void* event_data)
{
  retried_attempt = 0;
  ESP_LOGI(TAG, "Successfully connected to the SSID: %s", CONFIG_WOL_WIFI_SSID);
  if (http_server == NULL) {
    http_server = start_http_server();
  }
}

/**
 * @brief Register wifi event handlers.
 *
 */
static esp_err_t
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
static esp_err_t
nvs_init(void)
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
static esp_err_t
wifi_init(void)
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
  ESP_LOGI(
    TAG, "CONFIG_WOL_WIFI_SSID                : %s", CONFIG_WOL_WIFI_SSID);
  ESP_LOGI(
    TAG, "CONFIG_WOL_WIFI_PASSWORD            : %s", CONFIG_WOL_WIFI_PASSWORD);
  ESP_LOGI(
    TAG, "CONFIG_WOL_WIFI_HOSTNAME            : %s", CONFIG_WOL_WIFI_HOSTNAME);
  ESP_LOGI(TAG,
           "CONFIG_WOL_WIFI_RETRY_ATTEMPT       : %d",
           CONFIG_WOL_WIFI_RETRY_ATTEMPT);
  ESP_LOGI(TAG,
           "CONFIG_WOL_HTTP_SERVER_PORT         : %d",
           CONFIG_WOL_HTTP_SERVER_PORT);
  ESP_LOGI(TAG,
           "CONFIG_WOL_TARGET_MAC_ADDRESS       : %s",
           CONFIG_WOL_TARGET_MAC_ADDRESS);
  ESP_LOGI(TAG,
           "CONFIG_WOL_TARGET_BROADCAST_IP      : %s",
           CONFIG_WOL_TARGET_BROADCAST_IP);

  if (CONFIG_WOL_WIFI_RETRY_ATTEMPT < 0) {
    ESP_LOGE(TAG, "CONFIG_WOL_WIFI_RETRY_ATTEMPT cannot be less than 0!");
    return;
  }

  if (strcmp(CONFIG_WOL_TARGET_MAC_ADDRESS, "XX:XX:XX:XX:XX:XX") == 0) {
    ESP_LOGE(TAG, "CONFIG_WOL_TARGET_MAC_ADDRESS has not been set!");
    return;
  }

  unsigned char t_mac_bytes[MAC_BYTES_LENGTH];
  if (parse_mac(CONFIG_WOL_TARGET_MAC_ADDRESS, t_mac_bytes) != 0) {
    ESP_LOGE(TAG, "CONFIG_WOL_TARGET_MAC_ADDRESS format is invalid!");
    return;
  }

  ESP_ERROR_CHECK(nvs_init());
  ESP_ERROR_CHECK(wifi_init());

  while (http_server) {
    sleep(5);
  }
}
