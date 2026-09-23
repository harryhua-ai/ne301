#ifndef AI_SERVICE_H_STUB
#define AI_SERVICE_H_STUB

#include "aicam_types.h"
#include <stdint.h>

typedef struct {
    aicam_bool_t loaded;
    int32_t      result_type;
    char         name[64];
    char         version[32];
    char         postprocess_type[32];
    uint32_t     generation;
    uint16_t     num_classes;
} ai_model_runtime_info_t;

aicam_result_t ai_get_model_runtime_info(ai_model_runtime_info_t *info);
aicam_result_t ai_get_model_class_name(uint16_t index, char *buf, uint32_t buf_size);

uint32_t ai_get_confidence_threshold(void);
aicam_result_t ai_set_confidence_threshold(uint32_t threshold);

#endif
