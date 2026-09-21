#ifndef VIDEO_AI_ACTIVE_MODEL_H
#define VIDEO_AI_ACTIVE_MODEL_H

#include <stddef.h>
#include <stdint.h>
#include "pp.h"
#include "nn_model_meta.h"

typedef struct {
    char name[64];
    char version[32];
    char model_type[32];
    char postprocess_type[32];
    pp_type_t result_type;
    nn_class_list_t classes;
} video_ai_active_model_install_t;

typedef struct {
    uint8_t loaded;
    uint32_t generation;
    video_ai_active_model_install_t active;
} video_ai_active_model_state_t;

void video_ai_active_model_reset(video_ai_active_model_state_t *state);

void video_ai_active_model_commit(video_ai_active_model_state_t *state,
                                  const video_ai_active_model_install_t *install);

void video_ai_active_model_uninstall(video_ai_active_model_state_t *state);

uint8_t video_ai_active_model_is_loaded(const video_ai_active_model_state_t *state);

uint32_t video_ai_active_model_generation(const video_ai_active_model_state_t *state);

uint16_t video_ai_active_model_class_count(const video_ai_active_model_state_t *state);

int video_ai_active_model_class_name(const video_ai_active_model_state_t *state,
                                     uint16_t index, char *buf, size_t buf_size);

void video_ai_active_model_view(const video_ai_active_model_state_t *state,
                                uint8_t *loaded, uint32_t *generation,
                                video_ai_active_model_install_t *out);

#endif
