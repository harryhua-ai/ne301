#ifndef APP_REPORT_SERVICE_DEVICE_H
#define APP_REPORT_SERVICE_DEVICE_H

#include <stdint.h>

#include "aicam_types.h"

#ifdef __cplusplus
extern "C" {
#endif

aicam_result_t app_report_service_device_start(void);

aicam_result_t app_report_service_device_stop(void);

aicam_bool_t app_report_service_device_running(void);

void app_report_service_device_cmd_register(void);

#ifdef __cplusplus
}
#endif

#endif
