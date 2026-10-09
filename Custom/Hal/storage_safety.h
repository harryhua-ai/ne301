/**
 * @file storage_safety.h
 * @brief Non-destructive storage boot/format decision helpers (hardware-free).
 * @details Pure, portable decision logic implementing the #37 three-state
 *          invariant for the LittleFS volume and raw flash regions:
 *           1) a volume that mounts is used as-is (no rewriting);
 *           2) a provably blank volume is reported as NEEDS_INIT and is only
 *              ever written through the EXPLICIT format entry point;
 *           3) any other mount/read failure keeps the media untouched and
 *              reports UNAVAILABLE. No function in this module ever erases
 *              or programs anything implicitly.
 *          Because the module depends only on littlefs and the C library it
 *          is host-unit-testable with injected/faulty block devices
 *          (see tests/storage_safety).
 */

#ifndef _STORAGE_SAFETY_H_
#define _STORAGE_SAFETY_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "lfs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Mount/classification outcome for a LittleFS volume (issue #37 states). */
typedef enum {
    STORAGE_LFS_STATE_NOT_INITIALIZED = 0, /**< probing not run yet */
    STORAGE_LFS_STATE_OK = 1,              /**< mounted; normal operation */
    STORAGE_LFS_STATE_NEEDS_INIT = 2,      /**< mount failed; volume PROVABLY
                                                blank; preserved untouched;
                                                needs explicit format */
    STORAGE_LFS_STATE_UNAVAILABLE = 3,     /**< mount failed; content NOT proven
                                                blank; preserved untouched */
} storage_lfs_state_t;

/**
 * @brief Inject-able raw read: target passes storage_flash_read(), host tests
 *        pass a fake. Must return 0 on success, non-zero on failure.
 */
typedef int (*storage_raw_read_t)(uint32_t offset, void *data, size_t len);

/**
 * @brief Mount a LittleFS volume and classify the outcome. NEVER formats,
 *        erases or programs; a failed mount leaves the medium untouched.
 * @param[in,out] lfs littlefs instance (uninitialized state on entry)
 * @param[in] cfg littlefs config with read/prog/erase callbacks
 * @return STORAGE_LFS_STATE_OK / NEEDS_INIT (proven blank) / UNAVAILABLE
 */
storage_lfs_state_t storage_lfs_probe_and_mount(lfs_t *lfs, struct lfs_config *cfg);

/**
 * @brief Read-only blank probe of a whole LittleFS volume via its lfs_config.
 * @param[in] cfg littlefs config (only cfg->read is used)
 * @param[out] out_blank true iff every byte of every block reads 0xFF
 * @return 0 on a completed probe (see *out_blank), negative on read failure
 *         (in which case *out_blank is false — an unreadable medium is never
 *         reported blank).
 */
int storage_lfs_volume_blank(const struct lfs_config *cfg, bool *out_blank);

/**
 * @brief Explicitly format a LittleFS volume and remount it. This is the ONLY
 *        authorized destructive path (issue #37 invariant 2); it never runs
 *        automatically and reports the real result.
 * @param[in,out] lfs littlefs instance
 * @param[in] cfg littlefs config
 * @param[in,out] mounted in: whether the volume is currently mounted (it is
 *                unmounted first, best effort); out: whether the volume is
 *                mounted on return
 * @return 0 only if format AND remount both succeeded; negative lfs error
 *         otherwise. On failure the volume is left unmounted and untouched
 *         as far as this function is concerned.
 */
int storage_lfs_format_volume(lfs_t *lfs, struct lfs_config *cfg, bool *mounted);

/**
 * @brief Read-only erase-check of a raw flash range (e.g. an NVS partition).
 * @param[in] read_fn raw read implementation
 * @param[in] offset start offset (in raw-read address space)
 * @param[in] len length in bytes
 * @param[out] out_blank true iff every byte in the range reads 0xFF
 * @return 0 on completed probe, negative on read failure or bad parameters
 *         (*out_blank is false in every non-blank/failure case).
 */
int storage_blank_check_range(storage_raw_read_t read_fn, uint32_t offset,
                              size_t len, bool *out_blank);

/** @brief Stable short name of a state (for logs); never NULL. */
const char *storage_lfs_state_name(storage_lfs_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* _STORAGE_SAFETY_H_ */
