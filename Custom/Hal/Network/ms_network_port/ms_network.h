#ifndef __MS_NETWORK_H__
#define __MS_NETWORK_H__

#ifdef __cplusplus
    extern "C" {
#endif

#include <stdint.h>
#include "FreeRTOS.h"
#include "semphr.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/ssl.h"

#define MS_NETWORK_RECV_IDLE_TIMEOUT_MS  (10)
#define MS_NETWORK_DEFAULT_TIMEOUT_MS    (3000)
#define MS_NETWORK_LAST_SEND_TIMEOUT_MS  (500)
// #define MS_NETWORK_ONCE_MAX_SEND_SIZE    (4 * 1024)

/// @brief Network error code
typedef enum
{
    NET_ERR_OK = 0,
    NET_ERR_FAILED = -1,
    NET_ERR_INVALID_ARG = -2,
    NET_ERR_INVALID_STATE = -3,
    NET_ERR_TIMEOUT = -4,
    NET_ERR_DNS = -5,
    NET_ERR_SOCKET = -6,
    NET_ERR_SELECT = -7,
    NET_ERR_CONN = -8,
    NET_ERR_SEND = -9,
    NET_ERR_RECV = -10,
    NET_ERR_TLS = -11,
    NET_ERR_TLS_AUTH = -12,
    NET_ERR_TLS_HANDSHAKE = -13,
    NET_ERR_TLS_ALERT = -14,
    NET_ERR_UNKNOWN = -0xff,
} network_error_t;

/// @brief Network configuration
typedef struct
{
    uint8_t is_verify_hostname;     // Whether to verify hostname

    const char *ca_data;            // Server CA certificate data
    size_t ca_len;                  // Server CA certificate length (if 0, use strlen)

    const char *client_cert_data;   // Client certificate data
    size_t client_cert_len;         // Client certificate length (if 0, use strlen)

    const char *client_key_data;    // Client key data
    size_t client_key_len;          // Client key length (if 0, use strlen)
} network_tls_config_t;

/// @brief Network object
typedef struct
{
    int sock_fd;
    SemaphoreHandle_t lock;
    uint32_t rx_timeout_ms;
    uint32_t tx_timeout_ms;

    uint8_t tls_enable_flag;
    uint8_t is_verify_hostname;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config ssl_conf;
    mbedtls_x509_crt cacert, clicert;
    mbedtls_pk_context pkey;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
} ms_network_t;

/// @brief Network handle
typedef ms_network_t *ms_network_handle_t;

/// @brief Network initialization
/// @param tls_config TLS configuration
/// @return Network handle
ms_network_handle_t ms_network_init(const network_tls_config_t *tls_config);

/// @brief DNS resolution
/// @param host Hostname
/// @param ipaddr Output IP address
/// @return Error code
int ms_network_dns_parse(const char *host, uint8_t *ipaddr);

/// @brief Network connection
/// @param network Network handle
/// @param host Hostname
/// @param port Port
/// @return Error code
int ms_network_connect(ms_network_handle_t network, const char *host, uint16_t port, uint32_t timeout);

/// @brief Network receive data
/// @param network Network handle
/// @param buf Receive buffer
/// @param len Receive length
/// @param timeout Timeout in milliseconds
/// @return Less than 0 indicates failure, greater than or equal to 0 indicates actual received length
int ms_network_recv(ms_network_handle_t network, uint8_t *buf, uint32_t len, uint32_t timeout);

/// @brief Network send data
/// @param network Network handle
/// @param buf Send buffer
/// @param len Send length
/// @param timeout Timeout in milliseconds
/// @return Less than 0 indicates failure, greater than or equal to 0 indicates actual sent length
int ms_network_send(ms_network_handle_t network, uint8_t *buf, uint32_t len, uint32_t timeout);

/// @brief Network close
/// @param network Network handle
void ms_network_close(ms_network_handle_t network);

/// @brief Network deinitialize
/// @param network Network handle
void ms_network_deinit(ms_network_handle_t network);

#ifdef __cplusplus
}
#endif

#endif /* __MS_NETWORK_H__ */
