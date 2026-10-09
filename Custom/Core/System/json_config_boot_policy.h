/**
 * @file json_config_boot_policy.h
 * @brief Pure boot-state classification/policy for the JSON config NVS layer.
 * @details Issue #37 three-state invariant, config-layer decision table. The
 *          firmware load path (json_config_nvs.c / json_config_mgr.c) gathers
 *          evidence (backend ready? partition provably blank? magic valid?)
 *          and asks this policy what is allowed:
 *            - PERSISTED: stored config recognized -> normal operation,
 *              per-key backfill of newer keys allowed (legacy behavior).
 *            - FIRST_BLANK: backend ready AND partition PROVABLY fully
 *              erased AND magic missing -> genuine factory-fresh device;
 *              the existing first-boot default initialization may run.
 *            - UNRECOGNIZED: backend ready but partition holds data that is
 *              not proven blank AND no valid magic -> refuse to persist
 *              anything; run on RAM defaults; keep old bytes for diagnosis.
 *            - BACKEND_UNAVAILABLE: NVS not ready or even the blank probe
 *              could not be read -> refuse everything; RAM defaults only.
 *          Missing magic alone is NOT a blank proof (issue #37 AC3).
 *
 *          Pure logic, no hardware includes: host-unit-testable
 *          (see tests/storage_safety).
 */

#ifndef _JSON_CONFIG_BOOT_POLICY_H_
#define _JSON_CONFIG_BOOT_POLICY_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    JSON_CONFIG_BOOT_PERSISTED = 0,      /**< stored config recognized */
    JSON_CONFIG_BOOT_FIRST_BLANK = 1,    /**< proven-blank NVS, first boot */
    JSON_CONFIG_BOOT_UNRECOGNIZED = 2,   /**< data present, not recognizable */
    JSON_CONFIG_BOOT_BACKEND_UNAVAILABLE = 3, /**< NVS not ready / unreadable */
} json_config_boot_state_t;

/**
 * @brief Classify the config boot state from gathered evidence.
 * @param[in] backend_ready NVS_USER partition initialized (storage_nvs_ready)
 * @param[in] blank_check_err 0 iff the read-only blank probe completed
 * @param[in] blank result of the blank probe (only meaningful if err==0)
 * @param[in] magic_read_err 0 iff the magic key read succeeded
 * @param[in] magic_value value read for the magic key
 * @param[in] magic_expected the expected magic constant
 * @return the classified boot state
 */
json_config_boot_state_t json_config_boot_classify(bool backend_ready,
                                                   int blank_check_err,
                                                   bool blank,
                                                   int magic_read_err,
                                                   uint32_t magic_value,
                                                   uint32_t magic_expected);

/**
 * @brief May the config layer persist ANYTHING to NVS in this state?
 *        True for PERSISTED (normal saves/backfill) and FIRST_BLANK (the
 *        first-boot initialization itself). False otherwise — fail closed.
 */
bool json_config_boot_allow_persist(json_config_boot_state_t state);

/**
 * @brief May the first-boot default initialization (write defaults to NVS)
 *        run in this state? True ONLY for FIRST_BLANK.
 */
bool json_config_boot_allow_first_boot_init(json_config_boot_state_t state);

/** @brief Stable short name of a boot state (for logs); never NULL. */
const char *json_config_boot_state_name(json_config_boot_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* _JSON_CONFIG_BOOT_POLICY_H_ */
