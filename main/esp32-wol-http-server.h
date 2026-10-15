#include "esp_http_server.h"

#define MAC_BYTES_LENGTH 6
#define PACKET_SIZE 102
#define DEFAULT_PORT 9

int
parse_mac(const char* mac_str, unsigned char* mac_bytes);

httpd_handle_t*
start_http_server(void);

esp_err_t
stop_http_server(httpd_handle_t* server);