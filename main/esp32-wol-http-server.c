#include "esp32-wol-http-server.h"

#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "http_parser.h"
#include "sdkconfig.h" // IWYU pragma: keep this for the CONFIG_* macros.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* TAG = "esp32-wol-http-server";

int
parse_mac(const char* mac_str, unsigned char* mac_bytes)
{
  unsigned int bytes[MAC_BYTES_LENGTH];
  if (sscanf(mac_str,
             "%x:%x:%x:%x:%x:%x",
             &bytes[0],
             &bytes[1],
             &bytes[2],
             &bytes[3],
             &bytes[4],
             &bytes[5]) != MAC_BYTES_LENGTH) {
    return -1;
  }
  for (int i = 0; i < MAC_BYTES_LENGTH; i++) {
    mac_bytes[i] = (unsigned char)bytes[i];
  }
  return 0;
}

static esp_err_t
health_handler(httpd_req_t* req)
{
  return httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t
wol_handler(httpd_req_t* req)
{
  ssize_t sent_bytes = 0;

  const char* mac_str = CONFIG_WOL_TARGET_MAC_ADDRESS;
  unsigned char mac_bytes[MAC_BYTES_LENGTH];
  parse_mac(mac_str, mac_bytes);

  unsigned char packet[MAGIC_PACKET_SIZE];
  memset(packet, 0xFF, MAC_BYTES_LENGTH);

  // Append the MAC address 16 times
  for (int i = 0; i < 16; i++) {
    memcpy(&packet[MAC_BYTES_LENGTH + (i * MAC_BYTES_LENGTH)],
           mac_bytes,
           MAC_BYTES_LENGTH);
  }

  // Create UDP socket
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    char* err_msg = "Socket creation failed";
    ESP_LOGE(TAG, "%s, received socket: %d", err_msg, sock);
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, err_msg);
  }

  // Enable broadcast option
  int broadcast_permission = 1;
  if (setsockopt(sock,
                 SOL_SOCKET,
                 SO_BROADCAST,
                 &broadcast_permission,
                 sizeof(broadcast_permission)) < 0) {
    char* err_msg = "Setting broadcast permission failed";
    ESP_LOGE(TAG, "%s", err_msg);
    close(sock);
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, err_msg);
  }

  // Set target address structure
  struct sockaddr_in target_addr;
  memset(&target_addr, 0, sizeof(target_addr));
  target_addr.sin_family = AF_INET;
  target_addr.sin_port = htons(DEFAULT_PORT);

  if (inet_pton(
        AF_INET, CONFIG_WOL_TARGET_BROADCAST_IP, &target_addr.sin_addr) <= 0) {
    char* err_msg = "Invalid broadcast IP address";
    ESP_LOGE(TAG, "%s", err_msg);
    close(sock);
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, err_msg);
  }

  // Send packet
  sent_bytes = sendto(sock,
                      packet,
                      MAGIC_PACKET_SIZE,
                      0,
                      (struct sockaddr*)&target_addr,
                      sizeof(target_addr));
  if (sent_bytes < 0) {
    char* err_msg = "Failed to send magic packet";
    ESP_LOGE(TAG, "%s", err_msg);
    close(sock);
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, err_msg);
  }
  close(sock);

  char* success_msg_fmt = "WoL sent, successfully broadcasted (%zd bytes)";
  size_t success_msg_size = snprintf(NULL, 0, success_msg_fmt, sent_bytes) + 1;
  char* success_msg = malloc(success_msg_size);
  sprintf(success_msg, success_msg_fmt, sent_bytes);

  return httpd_resp_send(req, success_msg, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t
not_found_handler(httpd_req_t* req, httpd_err_code_t code)
{
  return httpd_resp_send_err(req, code, "404 Not Found");
}

static httpd_uri_t health_uri = {
  .method = HTTP_GET,
  .uri = "/health",
  .handler = health_handler,
  .user_ctx = NULL,
};

static httpd_uri_t wol_uri = {
  .method = HTTP_GET,
  .uri = "/wol",
  .handler = wol_handler,
  .user_ctx = NULL,
};

httpd_handle_t
start_http_server(void)
{
  httpd_handle_t server = NULL;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();

  config.server_port = CONFIG_WOL_HTTP_SERVER_PORT;
  config.lru_purge_enable = true;

  ESP_LOGI(TAG, "Starting http server on port: %d", config.server_port);
  ESP_ERROR_CHECK(httpd_start(&server, &config));

  // Register http uri handler
  ESP_ERROR_CHECK(httpd_register_uri_handler(server, &health_uri));
  ESP_ERROR_CHECK(httpd_register_uri_handler(server, &wol_uri));

  // Register http err handler
  ESP_ERROR_CHECK(httpd_register_err_handler(
    server, HTTPD_404_NOT_FOUND, &not_found_handler));

  return server;
}

esp_err_t
stop_http_server(httpd_handle_t server)
{
  return httpd_stop(server);
}