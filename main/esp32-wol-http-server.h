#include "esp_http_server.h"

#define MAC_BYTES_LENGTH 6

// 6 bytes of initial 0xFF + 16 * MAC_BYTES_LENGTH (96 bytes)
#define MAGIC_PACKET_SIZE 102

#define DEFAULT_PORT 9

int
parse_mac(const char* mac_str, unsigned char* mac_bytes);

/**
 * @brief Start the http server to the set port number.
 */
httpd_handle_t
start_http_server(void);

/**
 * @brief Stop the http server.
 */
esp_err_t
stop_http_server(httpd_handle_t server);