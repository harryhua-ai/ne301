#ifndef NN_MODEL_META_H
#define NN_MODEL_META_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NN_MAX_CLASS_COUNT 96
#define NN_CLASS_NAME_MAX  32

typedef struct {
    uint16_t count;
    char names[NN_MAX_CLASS_COUNT][NN_CLASS_NAME_MAX];
} nn_class_list_t;

typedef struct {
    char model_type[32];
    uint16_t num_classes;
    nn_class_list_t classes;
} nn_config_meta_t;

int nn_parse_model_config(const char *config_json, nn_config_meta_t *meta);

int nn_class_list_get(const nn_class_list_t *list, uint16_t index, char *buf, size_t buf_size);

uint32_t nn_generation_next(uint32_t current);

#ifdef __cplusplus
}
#endif

#endif
