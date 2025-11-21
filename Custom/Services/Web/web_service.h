/**
 * @file web_service.h
 * @brief Web Service Interface Header
 * @details Web service standard interface definition
 */

#ifndef WEB_SERVICE_H
#define WEB_SERVICE_H

#include "aicam_types.h"
#include "service_interfaces.h"
#include "web_server.h"
#include "websocket_stream_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Web Service Interface Functions ==================== */

/**
 * @brief Initialize web service
 * @param config Service configuration (optional)
 * @return aicam_result_t Operation result
 */
aicam_result_t web_service_init(void *config);

/**
 * @brief Start web service
 * @return aicam_result_t Operation result
 */
aicam_result_t web_service_start(void);

/**
 * @brief Stop web service
 * @return aicam_result_t Operation result
 */
aicam_result_t web_service_stop(void);

/**
 * @brief Deinitialize web service
 * @return aicam_result_t Operation result
 */
aicam_result_t web_service_deinit(void);

/**
 * @brief Get web service state
 * @return service_state_t Service state
 */
service_state_t web_service_get_state(void);

/* ==================== Web Service Configuration API ==================== */

/**
 * @brief Set web service configuration
 * @param config Configuration structure
 * @return aicam_result_t Operation result
 */
aicam_result_t web_service_set_config(const http_server_config_t *config);

/**
 * @brief Get web service configuration
 * @param config Configuration structure (output parameter)
 * @return aicam_result_t Operation result
 */
aicam_result_t web_service_get_config(http_server_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* WEB_SERVICE_H */
