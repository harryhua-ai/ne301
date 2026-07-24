#ifndef PC_MODEL_REGISTRY_H
#define PC_MODEL_REGISTRY_H
#include <stdint.h>
#include "aicam_types.h"

uintptr_t pc_model_lookup(const char* name);
void pc_model_register(const char* name, uintptr_t model_ptr);

#endif
