#include "cfg_config_cache_nvs.h"

#include <string.h>

#include "cmsis_os2.h"
#include "storage.h"

#define CFG_CACHE_KEY_0      "cfg_view_0"
#define CFG_CACHE_KEY_1      "cfg_view_1"
#define CFG_CACHE_MARKER_KEY "cfg_auth_mk"

static const char *cfg_nvs_slot_key(uint32_t slot)
{
    return (slot == 0u) ? CFG_CACHE_KEY_0 : CFG_CACHE_KEY_1;
}

static aicam_result_t cfg_nvs_read_slot(void *ctx, uint32_t slot, void *buf, uint32_t len)
{
    (void)ctx;
    int n = storage_nvs_read(NVS_USER, cfg_nvs_slot_key(slot), buf, len);
    if (n < 0 || (uint32_t)n != len) return AICAM_ERROR_NOT_FOUND;
    return AICAM_OK;
}

static aicam_result_t cfg_nvs_write_slot(void *ctx, uint32_t slot, const void *buf, uint32_t len)
{
    (void)ctx;
    int n = storage_nvs_write(NVS_USER, cfg_nvs_slot_key(slot), buf, len);
    if (n < 0 || (uint32_t)n != len) return AICAM_ERROR_IO;
    return AICAM_OK;
}

static aicam_result_t cfg_nvs_marker_read(void *ctx, cfg_cache_marker_t *m)
{
    (void)ctx;
    int n = storage_nvs_read(NVS_USER, CFG_CACHE_MARKER_KEY, m, sizeof(*m));
    if (n != (int)sizeof(*m)) return AICAM_ERROR_NOT_FOUND;
    return AICAM_OK;
}

static aicam_result_t cfg_nvs_marker_write(void *ctx, const cfg_cache_marker_t *m)
{
    (void)ctx;
    int n = storage_nvs_write(NVS_USER, CFG_CACHE_MARKER_KEY, m, sizeof(*m));
    if (n < 0 || (uint32_t)n != sizeof(*m)) return AICAM_ERROR_IO;
    return AICAM_OK;
}

static const cfg_cache_io_t s_nvs_cache_io = {
    NULL,
    cfg_nvs_read_slot,
    cfg_nvs_write_slot,
    cfg_nvs_marker_read,
    cfg_nvs_marker_write
};

static osMutexId_t s_nvs_cache_mutex = NULL;
static cfg_config_cache_core_t s_nvs_cache_core;
static uint8_t s_nvs_cache_ready = 0u;

aicam_bool_t cfg_config_cache_nvs_begin(cfg_config_cache_core_t **out_core)
{
    if (!out_core) return AICAM_FALSE;
    if (!s_nvs_cache_mutex) {
        osMutexId_t m = osMutexNew(NULL);
        if (!m) return AICAM_FALSE;
        if (s_nvs_cache_mutex) {
            osMutexDelete(m);
        } else {
            s_nvs_cache_mutex = m;
        }
    }
    if (osMutexAcquire(s_nvs_cache_mutex, osWaitForever) != osOK) return AICAM_FALSE;
    if (!s_nvs_cache_ready) {
        cfg_config_cache_core_init(&s_nvs_cache_core, &s_nvs_cache_io,
                                   (uint32_t)sizeof(cfg_derived_view_t));
        s_nvs_cache_ready = 1u;
    }
    *out_core = &s_nvs_cache_core;
    return AICAM_TRUE;
}

void cfg_config_cache_nvs_end(void)
{
    if (s_nvs_cache_mutex) osMutexRelease(s_nvs_cache_mutex);
}
