/**
 * @file storage_safety.c
 * @brief Non-destructive storage boot/format decision helpers (hardware-free).
 * @details Implements the issue #37 three-state invariant for LittleFS and raw
 *          flash regions. Everything here is deliberately side-effect-free
 *          with respect to the medium: the only write path is the explicit
 *          storage_lfs_format_volume() entry point.
 */

#include "storage_safety.h"

/* Scratch buffer for blank probes. Static (not stack): on target this runs in
 * the storage task (8 KB stack) and the probe may also be used from other
 * constrained contexts. Only used under the caller's storage lock. */
#define STORAGE_BLANK_SCRATCH_SIZE 256U

int storage_lfs_volume_blank(const struct lfs_config *cfg, bool *out_blank)
{
    static uint8_t scratch[STORAGE_BLANK_SCRATCH_SIZE];

    if (!cfg || !cfg->read || !out_blank || cfg->block_count == 0U ||
        cfg->block_size == 0U) {
        if (out_blank) {
            *out_blank = false;
        }
        return -1;
    }

    *out_blank = false;

    size_t chunk = cfg->read_size;
    if (chunk == 0U || chunk > sizeof(scratch)) {
        chunk = sizeof(scratch);
    }

    const lfs_size_t total = (lfs_size_t)cfg->block_count * cfg->block_size;
    lfs_size_t pos = 0U;

    while (pos < total) {
        lfs_size_t remain = total - pos;
        lfs_size_t in_block = cfg->block_size - (pos % cfg->block_size);
        lfs_size_t want = (remain < in_block) ? remain : in_block;
        if (want > chunk) {
            want = chunk;
        }

        int rc = cfg->read(cfg, (lfs_block_t)(pos / cfg->block_size),
                           (lfs_off_t)(pos % cfg->block_size), scratch,
                           (lfs_size_t)want);
        if (rc != LFS_ERR_OK) {
            /* Unreadable medium must never be classified as blank. */
            return -2;
        }

        for (lfs_size_t i = 0U; i < want; i++) {
            if (scratch[i] != 0xFFU) {
                return 0; /* any non-erased byte: not blank */
            }
        }
        pos += want;
    }

    *out_blank = true;
    return 0;
}

storage_lfs_state_t storage_lfs_probe_and_mount(lfs_t *lfs, struct lfs_config *cfg)
{
    if (!lfs || !cfg) {
        return STORAGE_LFS_STATE_UNAVAILABLE;
    }

    int err = lfs_mount(lfs, cfg);
    if (err == LFS_ERR_OK) {
        return STORAGE_LFS_STATE_OK;
    }

    /* Mount failed. State ③: never format here. Classify read-only so the
     * caller can report "needs safe init" vs "unavailable, media preserved".
     * A blank volume yields LFS_ERR_CORRUPT like any other invalid
     * superblock, so an explicit blank proof is required before claiming
     * NEEDS_INIT (issue #37: a generic mount error is NOT a blank proof). */
    bool blank = false;
    if (storage_lfs_volume_blank(cfg, &blank) == 0 && blank) {
        return STORAGE_LFS_STATE_NEEDS_INIT;
    }
    return STORAGE_LFS_STATE_UNAVAILABLE;
}

int storage_lfs_format_volume(lfs_t *lfs, struct lfs_config *cfg, bool *mounted)
{
    if (!lfs || !cfg || !mounted) {
        return LFS_ERR_INVAL;
    }

    if (*mounted) {
        /* Best-effort unmount so the format does not race a live filesystem
         * instance. Ignore the error: the fs may be corrupt, formatting is
         * still the authorized intent. */
        (void)lfs_unmount(lfs);
        *mounted = false;
    }

    int err = lfs_format(lfs, cfg);
    if (err != LFS_ERR_OK) {
        return err;
    }

    err = lfs_mount(lfs, cfg);
    *mounted = (err == LFS_ERR_OK);
    return err;
}

int storage_blank_check_range(storage_raw_read_t read_fn, uint32_t offset,
                              size_t len, bool *out_blank)
{
    static uint8_t scratch[STORAGE_BLANK_SCRATCH_SIZE];

    if (!read_fn || !out_blank || len == 0U) {
        if (out_blank) {
            *out_blank = false;
        }
        return -1;
    }

    *out_blank = false;

    size_t pos = 0U;
    while (pos < len) {
        size_t want = len - pos;
        if (want > sizeof(scratch)) {
            want = sizeof(scratch);
        }
        if (read_fn(offset + (uint32_t)pos, scratch, want) != 0) {
            return -2;
        }
        for (size_t i = 0U; i < want; i++) {
            if (scratch[i] != 0xFFU) {
                return 0; /* not blank */
            }
        }
        pos += want;
    }

    *out_blank = true;
    return 0;
}

const char *storage_lfs_state_name(storage_lfs_state_t state)
{
    switch (state) {
    case STORAGE_LFS_STATE_OK:
        return "OK";
    case STORAGE_LFS_STATE_NEEDS_INIT:
        return "NEEDS_INIT(blank)";
    case STORAGE_LFS_STATE_UNAVAILABLE:
        return "UNAVAILABLE(media-preserved)";
    case STORAGE_LFS_STATE_NOT_INITIALIZED:
    default:
        return "NOT_INITIALIZED";
    }
}
