#ifndef STUB_DS_H
#define STUB_DS_H
#include "aicam_types.h"
#include <stdint.h>
typedef struct { aicam_bool_t connected; int mode; uint32_t start_hour,start_minute,end_hour,end_minute;
  uint32_t brightness_level; aicam_bool_t auto_trigger_enabled; uint32_t light_threshold;
  aicam_bool_t fill_light_while_streaming; } light_config_t;
aicam_result_t device_service_light_get_config(light_config_t *config);
aicam_result_t device_service_light_set_config(const light_config_t *config);
aicam_result_t device_service_light_set_brightness(uint32_t brightness_level);
aicam_result_t device_service_light_blink(uint32_t blink_times, uint32_t interval_ms);
#endif
