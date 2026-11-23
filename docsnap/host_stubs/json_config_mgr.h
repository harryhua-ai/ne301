#ifndef STUB_JCM_H
#define STUB_JCM_H
#include "aicam_types.h"
#include <stdint.h>
typedef struct { uint8_t data_receive_qos; uint8_t data_report_qos; } mqtt_service_config_t;
aicam_result_t json_config_get_mqtt_service_config(mqtt_service_config_t *cfg);
#endif
