#ifndef CFG_BLOB_STORE_H
#define CFG_BLOB_STORE_H

#include <stddef.h>
#include <stdint.h>

#include "aicam_types.h"

#define CFG_BLOB_HDR_SIZE 24u

typedef struct {
    void *user;
    aicam_result_t (*read)(void *user, uint32_t offset, void *buf, uint32_t len);
    aicam_result_t (*write)(void *user, uint32_t offset, const void *buf, uint32_t len);
} cfg_blob_io_t;

typedef struct {
    cfg_blob_io_t io;
    uint32_t payload_size;
    uint32_t generation;
    uint8_t  has_record;
} cfg_blob_store_t;

void cfg_blob_store_init(cfg_blob_store_t *s, const cfg_blob_io_t *io, uint32_t payload_size);

aicam_bool_t cfg_blob_store_loaded(const cfg_blob_store_t *s);

aicam_result_t cfg_blob_store_load(const cfg_blob_store_t *s, void *out);

aicam_result_t cfg_blob_store_load_slot(const cfg_blob_store_t *s, uint32_t slot, void *out);

aicam_result_t cfg_blob_store_save(cfg_blob_store_t *s, const void *payload);

typedef enum {
    CFG_BLOB_RECOVERY_USE_AUTHORITATIVE = 0,
    CFG_BLOB_RECOVERY_MIGRATE_LEGACY,
    CFG_BLOB_RECOVERY_SAFE_DEFAULTS
} cfg_blob_recovery_t;

uint32_t cfg_blob_store_crc32(const void *data, size_t len);

cfg_blob_recovery_t cfg_blob_store_recovery_policy(aicam_bool_t blob_loaded,
                                                   aicam_bool_t marker_present);

#endif
