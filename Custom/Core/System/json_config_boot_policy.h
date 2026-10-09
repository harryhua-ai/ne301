/**
 * @file json_config_boot_policy.h
 * @brief Pure boot-state classification/policy for the JSON config NVS layer.
 * @details Issue #37 three-state invariant, config-layer decision table (rev 2
 *          per review of PR #38). The firmware load path (json_config_nvs.c /
 *          json_config_mgr.c) gathers evidence (backend ready? partition
 *          provably blank? magic valid?) and asks this policy what is allowed:
 *
 *            - PERSISTED: stored config recognized -> normal operation,
 *              per-key backfill of newer keys allowed (legacy behavior),
 *              stored (real) admin credentials are authoritative.
 *            - PENDING_INIT: backend ready AND partition PROVABLY fully
 *              erased AND magic missing -> blank device waiting for an
 *              AUTHORIZED first initialization. Automatic persistent writes
 *              are REFUSED at boot (issue #37 review Blocker 2: proven blank
 *              alone is not a write authorization - there is no independent
 *              factory-init authority in this path). The explicit authorized
 *              entry (factory reset, require_auth route) may initialize a
 *              PENDING_INIT device; see json_config_reset_to_default().
 *              Because the medium is PROVEN empty, the factory default
 *              credential is the device's true credential and admin
 *              authentication may use it (bootstrap).
 *            - UNRECOGNIZED: backend ready but partition holds data that is
 *              not proven blank AND no valid magic -> refuse to persist
 *              anything; run on RAM defaults; keep old bytes for diagnosis.
 *            - BACKEND_UNAVAILABLE: NVS not ready or even the blank probe
 *              could not be read -> refuse everything; RAM defaults only.
 *
 *          For UNRECOGNIZED / BACKEND_UNAVAILABLE the RAM default credential
 *          is NOT a valid admin credential: the real password may exist but
 *          be unverifiable, so admin authentication must be refused
 *          (issue #37 review Blocker 1: a corrupted credential source must
 *          never promote the compile-time default password to a working
 *          admin credential).
 *
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
    JSON_CONFIG_BOOT_PENDING_INIT = 1,   /**< proven-blank NVS; awaiting an
                                              AUTHORIZED first initialization;
                                              no automatic persistent writes */
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
 *        True ONLY for PERSISTED (normal saves/backfill). A proven-blank
 *        partition (PENDING_INIT) is NOT a write authorization: first
 *        initialization must go through the explicit authorized entry
 *        (factory reset), never an automatic boot-time write.
 */
bool json_config_boot_allow_persist(json_config_boot_state_t state);

/**
 * @brief May admin authentication proceed against the credential this boot
 *        session would use?
 *        True for PERSISTED (stored credential) and PENDING_INIT (proven
 *        empty medium: the factory default credential IS the device's real
 *        credential). False for UNRECOGNIZED and BACKEND_UNAVAILABLE: the
 *        credential source is corrupted/unknown, so the compile-time default
 *        must never be promoted to a working admin credential
 *        (issue #37 review Blocker 1).
 */
bool json_config_boot_allow_admin_auth(json_config_boot_state_t state);

/** @brief Stable short name of a boot state (for logs); never NULL. */
const char *json_config_boot_state_name(json_config_boot_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* _JSON_CONFIG_BOOT_POLICY_H_ */
