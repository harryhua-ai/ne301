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

aicam_result_t cfg_blob_store_save(cfg_blob_store_t *s, const void *payload);

#endif
