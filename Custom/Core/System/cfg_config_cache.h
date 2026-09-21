#ifndef CFG_CONFIG_CACHE_H
#define CFG_CONFIG_CACHE_H

#include <stddef.h>
#include <stdint.h>

#include "aicam_types.h"
#include "cfg_blob_store.h"

#define CFG_CONFIG_CACHE_PAYLOAD_MAX 2048u

#define CFG_CACHE_MARKER_MAGIC 0x43464155u

typedef struct {
    uint32_t magic;
    uint32_t generation;
    uint32_t crc;
} cfg_cache_marker_t;

typedef enum {
    CFG_CACHE_BOOT_COMMITTED = 0,
    CFG_CACHE_BOOT_PRE_MIGRATION,
    CFG_CACHE_BOOT_SAFE_DEFAULTS
} cfg_cache_boot_source_t;

typedef struct {
    void *ctx;
    aicam_result_t (*read_slot)(void *ctx, uint32_t slot, void *buf, uint32_t len);
    aicam_result_t (*write_slot)(void *ctx, uint32_t slot, const void *buf, uint32_t len);
    aicam_result_t (*marker_read)(void *ctx, cfg_cache_marker_t *m);
    aicam_result_t (*marker_write)(void *ctx, const cfg_cache_marker_t *m);
} cfg_cache_io_t;

typedef struct {
    cfg_blob_store_t store;
    cfg_cache_io_t io;
    uint8_t staging[CFG_BLOB_HDR_SIZE + CFG_CONFIG_CACHE_PAYLOAD_MAX];
} cfg_config_cache_core_t;

void cfg_config_cache_core_init(cfg_config_cache_core_t *c, const cfg_cache_io_t *io,
                                uint32_t payload_size);

aicam_bool_t cfg_config_cache_marker_check(const cfg_cache_marker_t *m);

aicam_result_t cfg_config_cache_marker_load(const cfg_config_cache_core_t *c,
                                            uint32_t *generation);

aicam_result_t cfg_config_cache_marker_store(cfg_config_cache_core_t *c, uint32_t generation);

aicam_bool_t cfg_config_cache_load_for_generation(const cfg_config_cache_core_t *c,
                                                  void *out, uint32_t authoritative_generation);

cfg_cache_boot_source_t cfg_config_cache_boot_source(const cfg_config_cache_core_t *c, void *out);

aicam_result_t cfg_config_cache_store(cfg_config_cache_core_t *c, const void *payload);

#endif
