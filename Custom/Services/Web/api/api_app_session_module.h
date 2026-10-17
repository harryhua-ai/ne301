#ifndef API_APP_SESSION_MODULE_H
#define API_APP_SESSION_MODULE_H

#include "aicam_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t (*now_ms)(void);
    void (*sleep_ms)(uint32_t ms);
} app_bridge_backend_t;

void app_bridge_backend_set(const app_bridge_backend_t *backend);

void app_bridge_app_site_origin_set(const char *origin);

const char *app_bridge_app_site_origin(void);

void app_bridge_grants_reset(void);

aicam_result_t web_api_register_app_session_module(void);

#ifdef __cplusplus
}
#endif

#endif
