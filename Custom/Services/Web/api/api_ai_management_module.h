/**
 * @file api_ai_management_module.h
 * @brief AI Management API Module Header
 * @details API module for AI management using ai_service
 */

#ifndef API_AI_MANAGEMENT_MODULE_H
#define API_AI_MANAGEMENT_MODULE_H

#include "aicam_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register AI management module
 * @return aicam_result_t Operation result
 */
aicam_result_t web_api_register_ai_management_module(void);

#ifdef __cplusplus
}
#endif

#endif /* API_AI_MANAGEMENT_MODULE_H */
