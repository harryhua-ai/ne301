#ifndef CFG_CONFIG_CACHE_NVS_H
#define CFG_CONFIG_CACHE_NVS_H

#include <stdint.h>

#include "cfg_config_cache.h"
#include "json_config_mgr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t authoritative_generation;
    log_config_t log_config;
    ai_debug_config_t ai_debug;
    device_service_config_t device_service;
} cfg_derived_view_t;

_Static_assert(sizeof(cfg_derived_view_t) <= CFG_CONFIG_CACHE_PAYLOAD_MAX,
               "derived view must fit the cache payload budget");
_Static_assert(offsetof(cfg_derived_view_t, authoritative_generation) == 0u,
               "authoritative_generation must be the first payload field");

aicam_bool_t cfg_config_cache_nvs_begin(cfg_config_cache_core_t **out_core);

void cfg_config_cache_nvs_end(void);

#ifdef __cplusplus
}
#endif

#endif /* CFG_CONFIG_CACHE_NVS_H */
