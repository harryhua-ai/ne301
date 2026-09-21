#ifndef CFG_CONFIG_CACHE_H
#define CFG_CONFIG_CACHE_H

#include <stdint.h>

#include "aicam_types.h"
#include "json_config_mgr.h"

typedef struct {
    log_config_t log_config;
    ai_debug_config_t ai_debug;
    device_service_config_t device_service;
} cfg_derived_view_t;

void cfg_config_cache_fill_view(const aicam_global_config_t *config, cfg_derived_view_t *view);

aicam_bool_t cfg_config_cache_load(cfg_derived_view_t *out, uint32_t *generation_out);

aicam_result_t cfg_config_cache_store(const cfg_derived_view_t *view);

aicam_bool_t cfg_config_cache_marker_valid(void);

aicam_result_t cfg_config_cache_marker_write(uint32_t generation);

#endif
