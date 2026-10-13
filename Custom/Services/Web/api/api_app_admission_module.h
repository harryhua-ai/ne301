#ifndef API_APP_ADMISSION_MODULE_H
#define API_APP_ADMISSION_MODULE_H

#include "web_server.h"

#include "app_admission.h"

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#define API_APP_ADMISSION_MAX_B64_CHARS 2800000u

cJSON *app_admission_verdict_json(const app_admission_verdict_t *verdict,
                                  app_admission_result_t result);

aicam_result_t app_admission_verify_handler(http_handler_context_t *ctx);

aicam_result_t app_admission_state_handler(http_handler_context_t *ctx);

aicam_result_t web_api_register_app_admission_module(void);

#ifdef __cplusplus
}
#endif

#endif
