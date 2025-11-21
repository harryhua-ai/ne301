/**
 * @file api_auth_module.h
 * @brief Authentication API Module Header
 */

#ifndef API_LOGIN_MODULE_H
#define API_LOGIN_MODULE_H

#include "aicam_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register login module
 * @return Operation result
 */
aicam_result_t web_api_register_auth_module(void);

#ifdef __cplusplus
}
#endif

#endif /* API_LOGIN_MODULE_H */
