/**
 * @file api_network_module.h
 * @brief Network API Module Header
 * @details Network management API interface declarations
 */

#ifndef API_NETWORK_MODULE_H
#define API_NETWORK_MODULE_H

#include "web_api.h"
#include "communication_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== API Function Declarations ==================== */

/**
 * @brief Register network API module
 * @return Operation result
 */
aicam_result_t web_api_register_network_module(void);

/**
 * @brief Network status handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_status_handler(http_handler_context_t *ctx);

/**
 * @brief WiFi configuration handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_wifi_config_handler(http_handler_context_t *ctx);

/**
 * @brief Network scan refresh handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_scan_refresh_handler(http_handler_context_t *ctx);

/**
 * @brief Network disconnect handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_disconnect_handler(http_handler_context_t *ctx);

/**
 * @brief Delete known network handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_delete_known_handler(http_handler_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* API_NETWORK_MODULE_H */
