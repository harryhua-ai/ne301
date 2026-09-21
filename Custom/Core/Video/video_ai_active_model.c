#include "video_ai_active_model.h"
#include <string.h>

void video_ai_active_model_reset(video_ai_active_model_state_t *state)
{
    if (!state) {
        return;
    }
    memset(state, 0, sizeof(*state));
}

void video_ai_active_model_commit(video_ai_active_model_state_t *state,
                                  const video_ai_active_model_install_t *install)
{
    if (!state || !install) {
        return;
    }
    state->active = *install;
    state->loaded = 1u;
    state->generation++;
}

void video_ai_active_model_uninstall(video_ai_active_model_state_t *state)
{
    if (!state) {
        return;
    }
    memset(&state->active, 0, sizeof(state->active));
    state->active.result_type = PP_TYPE_NONE;
    state->loaded = 0u;
}

uint8_t video_ai_active_model_is_loaded(const video_ai_active_model_state_t *state)
{
    return state ? state->loaded : 0u;
}

uint32_t video_ai_active_model_generation(const video_ai_active_model_state_t *state)
{
    return state ? state->generation : 0u;
}

uint16_t video_ai_active_model_class_count(const video_ai_active_model_state_t *state)
{
    if (!state || !state->loaded) {
        return 0u;
    }
    return state->active.classes.count;
}

int video_ai_active_model_class_name(const video_ai_active_model_state_t *state,
                                     uint16_t index, char *buf, size_t buf_size)
{
    if (!state || !state->loaded) {
        return -1;
    }
    return nn_class_list_get(&state->active.classes, index, buf, buf_size);
}

void video_ai_active_model_view(const video_ai_active_model_state_t *state,
                                uint8_t *loaded, uint32_t *generation,
                                video_ai_active_model_install_t *out)
{
    if (!state) {
        return;
    }
    if (loaded) {
        *loaded = state->loaded;
    }
    if (generation) {
        *generation = state->generation;
    }
    if (out) {
        *out = state->active;
    }
}
