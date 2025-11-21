/**
 * @file api_mqtt_module.h
 * @brief MQTT API Module Header
 * @details MQTT service API module for configuration and testing
 */

#ifndef API_MQTT_MODULE_H
#define API_MQTT_MODULE_H

#include "aicam_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register MQTT API module
 * @return aicam_result_t Operation result
 */
aicam_result_t web_api_register_mqtt_module(void);

#ifdef __cplusplus
}
#endif

#endif /* API_MQTT_MODULE_H */
