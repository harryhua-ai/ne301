#ifndef API_APP_INSTALL_MODULE_H
#define API_APP_INSTALL_MODULE_H

#include "web_server.h"

#include "app_installer.h"

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#define API_APP_INSTALL_MAX_B64_CHARS 2800000u

cJSON *app_install_record_json(const app_install_record_t *record,
                               const char *status);

aicam_result_t app_install_package_handler(http_handler_context_t *ctx);

aicam_result_t app_install_state_handler(http_handler_context_t *ctx);

aicam_result_t app_install_remove_handler(http_handler_context_t *ctx);

aicam_result_t web_api_register_app_install_module(void);

#ifdef __cplusplus
}
#endif

#endif
