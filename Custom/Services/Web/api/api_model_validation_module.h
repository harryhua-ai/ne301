/**
 * @file api_model_validation_module.h
 * @brief Model Validation API Module Header
 * @details API module for model inference validation and testing
 */

#ifndef API_MODEL_VALIDATION_MODULE_H
#define API_MODEL_VALIDATION_MODULE_H

#include "aicam_types.h"

#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief Register model validation module
 * @return Operation result
 */
aicam_result_t web_api_register_model_validation_module(void);

#ifdef __cplusplus
}
#endif

#endif /* API_MODEL_VALIDATION_MODULE_H */
