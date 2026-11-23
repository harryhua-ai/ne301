#ifndef STUB_JCM_H
#define STUB_JCM_H
#include "aicam_types.h"
#include <stdint.h>
typedef struct { uint8_t data_receive_qos; uint8_t data_report_qos; } mqtt_service_config_t;
aicam_result_t json_config_get_mqtt_service_config(mqtt_service_config_t *cfg);
typedef enum { CAPTURE_MODE_INSTANT=0, CAPTURE_MODE_BATCH=1, CAPTURE_MODE_SCHEDULED=2,
  CAPTURE_MODE_LOCAL_ONLY=3 } capture_mode_t;
typedef struct { capture_mode_t mode; } capture_upload_config_t;
aicam_result_t json_config_get_capture_upload_config(capture_upload_config_t *config);
/* mqtt report content (docsnap calls mqtt_service_get_report_content) */
typedef enum { MQTT_REPORT_CONTENT_FULL=0, MQTT_REPORT_CONTENT_METADATA_ONLY=1 } mqtt_report_content_t;
mqtt_report_content_t mqtt_service_get_report_content(void);
#endif
