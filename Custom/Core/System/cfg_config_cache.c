#include "cfg_config_cache.h"

#include <string.h>

#include "cfg_blob_store.h"
#include "cmsis_os2.h"
#include "storage.h"

#define CFG_CACHE_KEY_0      "cfg_view_0"
#define CFG_CACHE_KEY_1      "cfg_view_1"
#define CFG_CACHE_MARKER_KEY "cfg_auth_mk"
#define CFG_CACHE_MARKER_MAGIC 0x43464155u

typedef struct {
    uint32_t magic;
    uint32_t generation;
    uint32_t crc;
} cfg_cache_marker_t;

_Static_assert(CFG_BLOB_HDR_SIZE + sizeof(cfg_derived_view_t) <= 3800u,
               "derived view must fit one NVS sector value");

static osMutexId_t s_cache_mutex = NULL;
static cfg_blob_store_t s_cache_store;
static uint8_t s_slot_image[2][CFG_BLOB_HDR_SIZE + sizeof(cfg_derived_view_t)];
static uint8_t s_read_image[CFG_BLOB_HDR_SIZE + sizeof(cfg_derived_view_t)];

static const char *cfg_cache_slot_key(uint32_t slot)
{
    return (slot == 0u) ? CFG_CACHE_KEY_0 : CFG_CACHE_KEY_1;
}

static uint32_t cfg_cache_slot_size(void)
{
    return (uint32_t)sizeof(s_slot_image[0]);
}

static aicam_bool_t cfg_cache_lock(void)
{
    if (!s_cache_mutex) {
        osMutexId_t m = osMutexNew(NULL);
        if (!m) return AICAM_FALSE;
        if (s_cache_mutex) {
            osMutexDelete(m);
        } else {
            s_cache_mutex = m;
        }
    }
    return (osMutexAcquire(s_cache_mutex, osWaitForever) == osOK) ? AICAM_TRUE : AICAM_FALSE;
}

static void cfg_cache_unlock(void)
{
    if (s_cache_mutex) osMutexRelease(s_cache_mutex);
}

static aicam_result_t cfg_cache_io_read(void *user, uint32_t offset, void *buf, uint32_t len)
{
    (void)user;
    uint32_t slot_size = cfg_cache_slot_size();
    uint32_t slot = offset / slot_size;
    uint32_t intra = offset % slot_size;
    if (slot > 1u || intra + len > slot_size) return AICAM_ERROR_INVALID_PARAM;

    int n = storage_nvs_read(NVS_USER, cfg_cache_slot_key(slot),
                             s_read_image, slot_size);
    if (n < 0 || (uint32_t)n != slot_size) return AICAM_ERROR_NOT_FOUND;
    memcpy(buf, s_read_image + intra, len);
    return AICAM_OK;
}

static aicam_result_t cfg_cache_io_write(void *user, uint32_t offset, const void *buf, uint32_t len)
{
    (void)user;
    uint32_t slot_size = cfg_cache_slot_size();
    uint32_t slot = offset / slot_size;
    uint32_t intra = offset % slot_size;
    if (slot > 1u || intra + len > slot_size) return AICAM_ERROR_INVALID_PARAM;

    memcpy(s_slot_image[slot] + intra, buf, len);
    if (intra + len == slot_size) {
        int n = storage_nvs_write(NVS_USER, cfg_cache_slot_key(slot),
                                  s_slot_image[slot], slot_size);
        if (n < 0 || (uint32_t)n != slot_size) return AICAM_ERROR_IO;
    }
    return AICAM_OK;
}

static void cfg_cache_ensure_init(void)
{
    if (s_cache_store.payload_size) return;
    cfg_blob_io_t io = { NULL, cfg_cache_io_read, cfg_cache_io_write };
    cfg_blob_store_init(&s_cache_store, &io, (uint32_t)sizeof(cfg_derived_view_t));
}

void cfg_config_cache_fill_view(const aicam_global_config_t *config, cfg_derived_view_t *view)
{
    if (!config || !view) return;
    memset(view, 0, sizeof(*view));
    view->log_config = config->log_config;
    view->ai_debug = config->ai_debug;
    view->device_service = config->device_service;
}

aicam_bool_t cfg_config_cache_load(cfg_derived_view_t *out, uint32_t *generation_out)
{
    if (!out) return AICAM_FALSE;
    if (!cfg_cache_lock()) return AICAM_FALSE;
    cfg_cache_ensure_init();
    aicam_bool_t ok = (cfg_blob_store_load(&s_cache_store, out) == AICAM_OK) ? AICAM_TRUE : AICAM_FALSE;
    if (ok && generation_out) *generation_out = s_cache_store.generation;
    cfg_cache_unlock();
    return ok;
}

aicam_result_t cfg_config_cache_store(const cfg_derived_view_t *view)
{
    if (!view) return AICAM_ERROR_INVALID_PARAM;
    if (!cfg_cache_lock()) return AICAM_ERROR_BUSY;
    cfg_cache_ensure_init();
    aicam_result_t r = cfg_blob_store_save(&s_cache_store, view);
    cfg_cache_unlock();
    return r;
}

aicam_bool_t cfg_config_cache_marker_valid(void)
{
    cfg_cache_marker_t m;
    int n = storage_nvs_read(NVS_USER, CFG_CACHE_MARKER_KEY, &m, sizeof(m));
    if (n != (int)sizeof(m)) return AICAM_FALSE;
    if (m.magic != CFG_CACHE_MARKER_MAGIC) return AICAM_FALSE;
    return (cfg_blob_store_crc32(&m, sizeof(m) - sizeof(uint32_t)) == m.crc) ? AICAM_TRUE : AICAM_FALSE;
}

aicam_result_t cfg_config_cache_marker_write(uint32_t generation)
{
    cfg_cache_marker_t m;
    m.magic = CFG_CACHE_MARKER_MAGIC;
    m.generation = generation;
    m.crc = cfg_blob_store_crc32(&m, sizeof(m) - sizeof(uint32_t));
    int n = storage_nvs_write(NVS_USER, CFG_CACHE_MARKER_KEY, &m, sizeof(m));
    if (n < 0 || (uint32_t)n != sizeof(m)) return AICAM_ERROR_IO;
    return AICAM_OK;
}
