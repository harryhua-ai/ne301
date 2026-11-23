#ifndef STUB_SS_H
#define STUB_SS_H
#include "aicam_types.h"
#include <stdint.h>
typedef struct { aicam_bool_t enable_ai; uint32_t chunk_size; aicam_bool_t store_to_sd;
  aicam_bool_t fast_fail_mqtt; aicam_capture_trigger_t trigger_type; } system_capture_request_t;
typedef struct { aicam_result_t result; uint64_t duration_ms; char image_id[80]; } system_capture_response_t;
aicam_result_t system_service_capture_request(const system_capture_request_t *request, system_capture_response_t *response);
#endif
