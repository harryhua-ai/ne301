#include "cfg_config_cache.h"

#include <string.h>

static uint32_t cfg_cache_slot_size(const cfg_config_cache_core_t *c)
{
    return CFG_BLOB_HDR_SIZE + c->store.payload_size;
}

static aicam_result_t cfg_cache_flat_read(void *user, uint32_t offset, void *buf, uint32_t len)
{
    cfg_config_cache_core_t *c = (cfg_config_cache_core_t *)user;
    uint32_t slot_size = cfg_cache_slot_size(c);
    uint32_t slot = offset / slot_size;
    uint32_t intra = offset % slot_size;
    if (slot > 1u || intra + len > slot_size) return AICAM_ERROR_INVALID_PARAM;
    aicam_result_t r = c->io.read_slot(c->io.ctx, slot, c->staging, slot_size);
    if (r != AICAM_OK) return r;
    memcpy(buf, c->staging + intra, len);
    return AICAM_OK;
}

static aicam_result_t cfg_cache_flat_write(void *user, uint32_t offset, const void *buf, uint32_t len)
{
    cfg_config_cache_core_t *c = (cfg_config_cache_core_t *)user;
    uint32_t slot_size = cfg_cache_slot_size(c);
    uint32_t slot = offset / slot_size;
    uint32_t intra = offset % slot_size;
    if (slot > 1u || intra + len > slot_size) return AICAM_ERROR_INVALID_PARAM;
    memcpy(c->staging + intra, buf, len);
    if (intra == 0u) {
        return c->io.write_slot(c->io.ctx, slot, c->staging, slot_size);
    }
    return AICAM_OK;
}

void cfg_config_cache_core_init(cfg_config_cache_core_t *c, const cfg_cache_io_t *io,
                                uint32_t payload_size)
{
    if (!c || !io) return;
    memset(c, 0, sizeof(*c));
    if (!io->read_slot || !io->write_slot || !io->marker_read || !io->marker_write) return;
    if (payload_size == 0u || payload_size > CFG_CONFIG_CACHE_PAYLOAD_MAX) return;
    c->io = *io;
    cfg_blob_io_t bio = { c, cfg_cache_flat_read, cfg_cache_flat_write };
    cfg_blob_store_init(&c->store, &bio, payload_size);
}

aicam_bool_t cfg_config_cache_marker_check(const cfg_cache_marker_t *m)
{
    if (!m) return AICAM_FALSE;
    if (m->magic != CFG_CACHE_MARKER_MAGIC) return AICAM_FALSE;
    return (cfg_blob_store_crc32(m, sizeof(*m) - sizeof(uint32_t)) == m->crc) ? AICAM_TRUE : AICAM_FALSE;
}

aicam_result_t cfg_config_cache_marker_load(const cfg_config_cache_core_t *c, uint32_t *generation)
{
    if (!c) return AICAM_ERROR_INVALID_PARAM;
    cfg_cache_marker_t m;
    aicam_result_t r = c->io.marker_read(c->io.ctx, &m);
    if (r != AICAM_OK) return r;
    if (!cfg_config_cache_marker_check(&m)) return AICAM_ERROR_IO;
    if (generation) *generation = m.generation;
    return AICAM_OK;
}

aicam_result_t cfg_config_cache_marker_store(cfg_config_cache_core_t *c, uint32_t generation)
{
    if (!c) return AICAM_ERROR_INVALID_PARAM;
    cfg_cache_marker_t m;
    m.magic = CFG_CACHE_MARKER_MAGIC;
    m.generation = generation;
    m.crc = cfg_blob_store_crc32(&m, sizeof(m) - sizeof(uint32_t));
    return c->io.marker_write(c->io.ctx, &m);
}

aicam_bool_t cfg_config_cache_load_for_generation(const cfg_config_cache_core_t *c,
                                                  void *out, uint32_t authoritative_generation)
{
    if (!c || !out || authoritative_generation == 0u) return AICAM_FALSE;
    uint32_t payload_size = c->store.payload_size;
    for (uint32_t k = 0u; k < 2u; k++) {
        uint32_t gen = c->store.generation - k;
        if (gen == 0u) break;
        if (cfg_blob_store_load_slot(&c->store, gen & 1u, out) == AICAM_OK &&
            memcmp(out, &authoritative_generation, sizeof(uint32_t)) == 0) {
            return AICAM_TRUE;
        }
        (void)payload_size;
    }
    return AICAM_FALSE;
}

cfg_cache_boot_source_t cfg_config_cache_boot_source(const cfg_config_cache_core_t *c, void *out)
{
    uint32_t marker_gen = 0;
    aicam_result_t mr = cfg_config_cache_marker_load(c, &marker_gen);
    if (mr == AICAM_ERROR_NOT_FOUND) return CFG_CACHE_BOOT_PRE_MIGRATION;
    if (mr != AICAM_OK) return CFG_CACHE_BOOT_SAFE_DEFAULTS;
    if (cfg_config_cache_load_for_generation(c, out, marker_gen)) return CFG_CACHE_BOOT_COMMITTED;
    return CFG_CACHE_BOOT_SAFE_DEFAULTS;
}

aicam_result_t cfg_config_cache_store(cfg_config_cache_core_t *c, const void *payload)
{
    if (!c || !payload) return AICAM_ERROR_INVALID_PARAM;
    return cfg_blob_store_save(&c->store, payload);
}
