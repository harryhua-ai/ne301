#include "cfg_blob_store.h"

#include <stddef.h>
#include <string.h>

#define CFG_BLOB_MAGIC 0x43464742u
#define CFG_BLOB_CHUNK 256u

typedef struct {
    uint32_t magic;
    uint32_t generation;
    uint32_t payload_size;
    uint32_t crc;
    uint32_t rsv;
    uint32_t rsv2;
} cfg_blob_hdr_t;

static uint32_t cfg_blob_crc_update(uint32_t crc, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return crc;
}

static uint32_t cfg_blob_crc(const void *data, size_t len) {
    return cfg_blob_crc_update(0xFFFFFFFFu, data, len) ^ 0xFFFFFFFFu;
}

static uint32_t cfg_blob_slot_offset(uint32_t slot, uint32_t payload_size) {
    return slot * (CFG_BLOB_HDR_SIZE + payload_size);
}

static aicam_result_t cfg_blob_slot_scan(const cfg_blob_store_t *s, uint32_t slot, uint32_t *gen_out)
{
    uint32_t base = cfg_blob_slot_offset(slot, s->payload_size);
    cfg_blob_hdr_t h;
    if (s->io.read(s->io.user, base, &h, sizeof(h)) != AICAM_OK) return AICAM_ERROR_NOT_FOUND;
    if (h.magic != CFG_BLOB_MAGIC || h.generation == 0) return AICAM_ERROR_NOT_FOUND;
    if (h.payload_size != s->payload_size) return AICAM_ERROR_NOT_FOUND;

    uint32_t crc = cfg_blob_crc_update(0xFFFFFFFFu, &h, offsetof(cfg_blob_hdr_t, crc));
    uint8_t buf[CFG_BLOB_CHUNK];
    uint32_t remaining = s->payload_size;
    uint32_t off = base + CFG_BLOB_HDR_SIZE;
    while (remaining > 0) {
        uint32_t n = remaining < CFG_BLOB_CHUNK ? remaining : CFG_BLOB_CHUNK;
        if (s->io.read(s->io.user, off, buf, n) != AICAM_OK) return AICAM_ERROR_NOT_FOUND;
        crc = cfg_blob_crc_update(crc, buf, n);
        remaining -= n;
        off += n;
    }
    if ((crc ^ 0xFFFFFFFFu) != h.crc) return AICAM_ERROR_NOT_FOUND;
    *gen_out = h.generation;
    return AICAM_OK;
}

void cfg_blob_store_init(cfg_blob_store_t *s, const cfg_blob_io_t *io, uint32_t payload_size)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
    if (!io || !io->read || !io->write || payload_size == 0) return;
    s->io = *io;
    s->payload_size = payload_size;
    for (uint32_t slot = 0; slot < 2u; slot++) {
        uint32_t gen = 0;
        if (cfg_blob_slot_scan(s, slot, &gen) == AICAM_OK &&
            (!s->has_record || cfg_blob_store_generation_newer(gen, s->generation))) {
            s->generation = gen;
            s->has_record = 1;
        }
    }
}

aicam_bool_t cfg_blob_store_loaded(const cfg_blob_store_t *s)
{
    return (s && s->has_record) ? AICAM_TRUE : AICAM_FALSE;
}

aicam_result_t cfg_blob_store_load(const cfg_blob_store_t *s, void *out)
{
    if (!s || !out || !s->has_record) return AICAM_ERROR_NOT_FOUND;
    uint32_t slot = s->generation & 1u;
    uint32_t base = cfg_blob_slot_offset(slot, s->payload_size);
    cfg_blob_hdr_t h;
    if (s->io.read(s->io.user, base, &h, sizeof(h)) != AICAM_OK) return AICAM_ERROR_IO;
    if (h.magic != CFG_BLOB_MAGIC || h.generation != s->generation ||
        h.payload_size != s->payload_size) {
        return AICAM_ERROR_IO;
    }

    uint32_t crc = cfg_blob_crc_update(0xFFFFFFFFu, &h, offsetof(cfg_blob_hdr_t, crc));
    uint8_t *dst = (uint8_t *)out;
    uint32_t remaining = s->payload_size;
    uint32_t off = base + CFG_BLOB_HDR_SIZE;
    while (remaining > 0) {
        uint32_t n = remaining < CFG_BLOB_CHUNK ? remaining : CFG_BLOB_CHUNK;
        if (s->io.read(s->io.user, off, dst, n) != AICAM_OK) return AICAM_ERROR_IO;
        crc = cfg_blob_crc_update(crc, dst, n);
        dst += n;
        remaining -= n;
        off += n;
    }
    if ((crc ^ 0xFFFFFFFFu) != h.crc) return AICAM_ERROR_IO;
    return AICAM_OK;
}

aicam_result_t cfg_blob_store_load_slot(const cfg_blob_store_t *s, uint32_t slot, void *out)
{
    if (!s || !out || !s->payload_size || slot > 1u) return AICAM_ERROR_INVALID_PARAM;
    uint32_t base = cfg_blob_slot_offset(slot, s->payload_size);
    cfg_blob_hdr_t h;
    if (s->io.read(s->io.user, base, &h, sizeof(h)) != AICAM_OK) return AICAM_ERROR_NOT_FOUND;
    if (h.magic != CFG_BLOB_MAGIC || h.generation == 0 ||
        h.payload_size != s->payload_size) {
        return AICAM_ERROR_NOT_FOUND;
    }

    uint32_t crc = cfg_blob_crc_update(0xFFFFFFFFu, &h, offsetof(cfg_blob_hdr_t, crc));
    uint8_t *dst = (uint8_t *)out;
    uint32_t remaining = s->payload_size;
    uint32_t off = base + CFG_BLOB_HDR_SIZE;
    while (remaining > 0) {
        uint32_t n = remaining < CFG_BLOB_CHUNK ? remaining : CFG_BLOB_CHUNK;
        if (s->io.read(s->io.user, off, dst, n) != AICAM_OK) return AICAM_ERROR_IO;
        crc = cfg_blob_crc_update(crc, dst, n);
        dst += n;
        remaining -= n;
        off += n;
    }
    if ((crc ^ 0xFFFFFFFFu) != h.crc) return AICAM_ERROR_IO;
    return AICAM_OK;
}

aicam_result_t cfg_blob_store_save(cfg_blob_store_t *s, const void *payload)
{
    if (!s || !payload || !s->payload_size) return AICAM_ERROR_INVALID_PARAM;
    uint32_t generation = s->generation + 1u;
    if (generation == 0) generation = 1u;
    uint32_t slot = generation & 1u;
    uint32_t base = cfg_blob_slot_offset(slot, s->payload_size);

    cfg_blob_hdr_t h;
    h.magic = CFG_BLOB_MAGIC;
    h.generation = generation;
    h.payload_size = s->payload_size;
    h.rsv = 0;
    h.rsv2 = 0;
    uint32_t crc = cfg_blob_crc_update(0xFFFFFFFFu, &h, offsetof(cfg_blob_hdr_t, crc));
    crc = cfg_blob_crc_update(crc, payload, s->payload_size);
    h.crc = crc ^ 0xFFFFFFFFu;

    if (s->io.write(s->io.user, base + CFG_BLOB_HDR_SIZE, payload, s->payload_size) != AICAM_OK) {
        return AICAM_ERROR_IO;
    }
    if (s->io.write(s->io.user, base, &h, sizeof(h)) != AICAM_OK) {
        return AICAM_ERROR_IO;
    }

    s->generation = generation;
    s->has_record = 1;
    return AICAM_OK;
}

uint32_t cfg_blob_store_crc32(const void *data, size_t len)
{
    return cfg_blob_crc(data, len);
}

aicam_bool_t cfg_blob_store_generation_newer(uint32_t candidate, uint32_t current)
{
    if (candidate == 0u || candidate == current) return AICAM_FALSE;
    return ((int32_t)(candidate - current) > 0) ? AICAM_TRUE : AICAM_FALSE;
}

cfg_blob_recovery_t cfg_blob_store_recovery_policy(aicam_bool_t blob_loaded,
                                                   aicam_bool_t marker_present)
{
    if (blob_loaded == AICAM_TRUE) return CFG_BLOB_RECOVERY_USE_AUTHORITATIVE;
    if (marker_present != AICAM_TRUE) return CFG_BLOB_RECOVERY_MIGRATE_LEGACY;
    return CFG_BLOB_RECOVERY_SAFE_DEFAULTS;
}
