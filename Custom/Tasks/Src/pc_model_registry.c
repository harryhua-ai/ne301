#include "pc_model_registry.h"
#include <string.h>

#define PC_MAX_MODELS 8
typedef struct { char name[64]; uintptr_t ptr; } pc_model_entry_t;
static pc_model_entry_t g_models[PC_MAX_MODELS];
static uint8_t g_n_models = 0;

void pc_model_register(const char* name, uintptr_t ptr) {
    if (!name || g_n_models >= PC_MAX_MODELS) return;
    strncpy(g_models[g_n_models].name, name, 63);
    g_models[g_n_models].name[63] = '\0';
    g_models[g_n_models].ptr = ptr;
    g_n_models++;
}

uintptr_t pc_model_lookup(const char* name) {
    if (!name) return 0;
    for (uint8_t i = 0; i < g_n_models; ++i)
        if (strcmp(g_models[i].name, name) == 0) return g_models[i].ptr;
    return 0;
}
