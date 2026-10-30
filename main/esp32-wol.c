
/*
 * ESP-32 WoL
 *
 * Minimal Wake-over-LAN implementation reachable over the internet through
 * Tailscale network with Wi-Fi connection.
 */

#include "esp32-wol.h"
#include "esp32-wol-http-server.h"
#include "microlink.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_wifi_types_generic.h"
#include "lwip/err.h"
#include "nvs_flash.h"
#include "sdkconfig.h" // IWYU pragma: keep this for the CONFIG_* macros.
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

const static char* TAG = "esp32-wol";

static int retried_attempt = 0;
static bool connected_once = false;
static bool wifi_gave_up = false;
static httpd_handle_t http_server = NULL;

// MicroLink handle
static microlink_t* ml = NULL;

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
  if (wifi_gave_up) {
    return;
  }

  retried_attempt++;

  if (!connected_once) {
    // Boot phase, trying to do an initial connection.
    if (retried_attempt <= CONFIG_WOL_WIFI_RETRY_ATTEMPT) {
      ESP_LOGW(TAG,
               "Initial connection failed, retrying (%d/%d)",
               retried_attempt,
               CONFIG_WOL_WIFI_RETRY_ATTEMPT);
    } else {
      // Repeated failures during boot most likely mean bad credentials.
      // Give up: no more reconnects, app_main stops the app.
      ESP_LOGE(TAG,
               "Could not connect to SSID '%s' after %d attempts, "
               "check CONFIG_WOL_WIFI_SSID and CONFIG_WOL_WIFI_PASSWORD. "
               "Giving up.",
               CONFIG_WOL_WIFI_SSID,
               CONFIG_WOL_WIFI_RETRY_ATTEMPT);
      wifi_gave_up = true;
      return;
    }
  } else {
    // Already connected at least once, retry indefinitely.
    ESP_LOGW(TAG,
             "Disconnected from SSID '%s', reconnecting (attempt %d)",
             CONFIG_WOL_WIFI_SSID,
             retried_attempt);
  }

  esp_wifi_connect();
}

static void
event_sta_connected_handler(void* arg,
                            esp_event_base_t event_base,
                            int32_t event_id,
                            void* event_data)
{
  retried_attempt = 0;
  connected_once = true;
  ESP_LOGI(TAG, "Successfully connected to the SSID: %s", CONFIG_WOL_WIFI_SSID);
  if (http_server == NULL) {
    http_server = start_http_server();
  }
}

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

static void
on_ml_state_change(microlink_t* ml_handle,
                   microlink_state_t state,
                   void* user_data)
{
  const char* state_names[] = { "IDLE",        "WIFI_WAIT", "CONNECTING",
                                "REGISTERING", "CONNECTED", "RECONNECTING",
                                "ERROR" };
  const char* name = (state < sizeof(state_names) / sizeof(state_names[0]))
                       ? state_names[state]
                       : "UNKNOWN";
  ESP_LOGI(TAG, "MicroLink state: %s", name);

  if (state == ML_STATE_CONNECTED) {
    uint32_t ip = microlink_get_vpn_ip(ml_handle);
    char ip_str[16];
    microlink_ip_to_str(ip, ip_str);
    ESP_LOGI(TAG, "MicroLink Connected! VPN IP: %s", ip_str);
  }
}

static void
on_ml_peer_update(microlink_t* ml_handle,
                  const microlink_peer_info_t* peer,
                  void* user_data)
{
  char ip_str[16];
  microlink_ip_to_str(peer->vpn_ip, ip_str);
  ESP_LOGI(TAG,
           "Peer: %s (%s) online=%d direct=%d",
           peer->hostname,
           ip_str,
           peer->online,
           peer->direct_path);
}

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

static esp_err_t
ml_init(void)
{
  microlink_config_t config = {
    .auth_key = CONFIG_TAILSCALE_AUTH_KEY,
    .device_name = CONFIG_TAILSCALE_DEVICE_NAME,
    .enable_derp = true,
    .enable_stun = true,
    .enable_disco = true,
    .wifi_tx_power_dbm = 13, // Reduced for thermal management.
  };

  ml = microlink_init(&config);
  if (!ml) {
    ESP_LOGE(TAG, "Failed to initialize MicroLink");
    return ESP_FAIL;
  }

  // Register callbacks
  microlink_set_state_callback(ml, on_ml_state_change, NULL);
  microlink_set_peer_callback(ml, on_ml_peer_update, NULL);

  return microlink_start(ml);
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
  ESP_LOGI(
    TAG, "CONFIG_TAILSCALE_AUTH_KEY           : %s", CONFIG_TAILSCALE_AUTH_KEY);
  ESP_LOGI(TAG,
           "CONFIG_TAILSCALE_DEVICE_NAME        : %s",
           CONFIG_TAILSCALE_DEVICE_NAME);

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

  if (strcmp(CONFIG_TAILSCALE_AUTH_KEY, "") == 0) {
    ESP_LOGE(TAG, "CONFIG_TAILSCALE_AUTH_KEY has not been set!");
    return;
  }

  ESP_ERROR_CHECK(nvs_init());
  ESP_ERROR_CHECK(wifi_init());
  ESP_ERROR_CHECK(ml_init());

  while (1) {
    // Stop immediately if initial Wi-Fi connection failed.
    if (wifi_gave_up) {
      ESP_LOGE(TAG, "Initial Wi-Fi connection failed. Stopping the app");
      microlink_stop(ml);
      esp_wifi_stop();
      vTaskDelete(NULL);
    }
    sleep(1);
  }
}
