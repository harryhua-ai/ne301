#ifndef STUB_JCI_H
#define STUB_JCI_H
#include "aicam_types.h"
#include <stdint.h>
#define NVS_KEY_IMAGE_ID_SEQ "img_seq"
aicam_result_t json_config_nvs_read_uint32(const char *key, uint32_t *value);
aicam_result_t json_config_nvs_write_uint32(const char *key, uint32_t value);
#endif
