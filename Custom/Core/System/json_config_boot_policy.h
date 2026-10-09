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
 *              erased AND magic missing -> blank device awaiting an
 *              AUTHORIZED first initialization that does not exist in the
 *              current approved product flow. Automatic persistent writes
 *              are REFUSED at boot, admin auth is REFUSED (rev 3, second
 *              review BLOCKER 2: blank evidence is not permission - the
 *              earlier bootstrap design is withdrawn; first-boot UX/security
 *              policy is an A/User decision), and the explicit factory-reset
 *              entry refuses as well.
 *            - UNRECOGNIZED: backend ready but partition holds data that is
 *              not proven blank AND no valid magic -> refuse to persist
 *              anything; run on RAM defaults; keep old bytes for diagnosis.
 *            - BACKEND_UNAVAILABLE: NVS not ready or even the blank probe
 *              could not be read -> refuse everything; RAM defaults only.
 *
 *          For PENDING_INIT / UNRECOGNIZED / BACKEND_UNAVAILABLE the RAM
 *          default credential is NOT a valid admin credential: the real
 *          password may exist but
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
 *        partition (PENDING_INIT) is NOT a write authorization: no
 *        executable authorized first-initialization entry exists in the
 *        approved product flow yet (A/User decision pending; the factory
 *        reset entry refuses blocked sessions), so a blank device stays
 *        write-refused and there is never an automatic boot-time write.
 */
bool json_config_boot_allow_persist(json_config_boot_state_t state);

/**
 * @brief May admin authentication proceed against the credential this boot
 *        session would use?
 *        True ONLY for PERSISTED (a stored credential was provably read).
 *        False for PENDING_INIT, UNRECOGNIZED and BACKEND_UNAVAILABLE.
 *
 *        Rev 3 (second review, BLOCKER 2): the earlier bootstrap design that
 *        kept admin auth alive on PENDING_INIT is WITHDRAWN — blank evidence
 *        is not permission. With no provable independent first-boot
 *        authorization in the approved product flow, a PENDING_INIT device
 *        gets neither automatic writes nor public-default-credential
 *        elevation; first-boot UX/security policy is an A/User decision.
 */
bool json_config_boot_allow_admin_auth(json_config_boot_state_t state);

/**
 * @brief May the explicit factory-reset entry run in this session?
 *        Only when persistence is NOT blocked (healthy PERSISTED session).
 *        Rev 3 (BLOCKER 2/3): blocked sessions — including PENDING_INIT —
 *        refuse; the reset entry is no longer an initialization path for
 *        blank media, and no session latch is pre-opened before the reset
 *        work is attempted.
 */
bool json_config_boot_allow_factory_reset(bool persist_blocked);

/* ==================== Storage read provenance & credential (rev 4) ==================== */

/**
 * Outcome of one NVS key read at the granularity the boot decisions need:
 * the raw backend distinguishes a PROVABLY absent key (-ENOENT class) from
 * any other read failure; boot-time write decisions may proceed only on the
 * provable cases and must treat every other failure as unknown.
 */
typedef enum {
    JSON_CONFIG_KEY_READ_OK = 0,      /**< value provably read from storage */
    JSON_CONFIG_KEY_READ_MISSING = 1, /**< key provably absent (-ENOENT class) */
    JSON_CONFIG_KEY_READ_UNKNOWN = 2, /**< read failed for an unknown reason */
} json_config_key_read_t;

typedef enum {
    JSON_CONFIG_CRED_PROVEN = 0,  /**< password provably read from a stored key */
    JSON_CONFIG_CRED_UNKNOWN = 1, /**< credential not provable -> fail-closed */
} json_config_cred_state_t;

/**
 * @brief Assess admin-credential provenance from the two storage read
 *        outcomes (new key, legacy key). PROVEN only when the new key was
 *        actually read, or the new key is PROVABLY absent and the legacy
 *        key was actually read (the authorized one-shot migration case).
 *        An unknown new-key read failure is UNKNOWN even when the legacy
 *        key reads fine: the unreadable new key may hold a newer credential,
 *        so the legacy value must not be promoted, written back, or allowed
 *        to authenticate; the RAM default must not be seeded either.
 * @param[in] auth_key read outcome of NVS_KEY_AUTH_PASSWORD
 * @param[in] legacy_key read outcome of NVS_KEY_DEVICE_INFO_PASSWORD
 *                     (only consulted when auth_key == MISSING)
 */
json_config_cred_state_t json_config_boot_assess_credential(json_config_key_read_t auth_key,
                                                            json_config_key_read_t legacy_key);

/**
 * @brief May a failed config-key read trigger the boot-time default
 *        backfill write for that key? True ONLY for a PROVABLY missing key
 *        (healthy old-firmware field migration keeps working). An unknown
 *        read failure must never overwrite the stored bytes with the RAM
 *        default; a successful read needs no backfill.
 */
bool json_config_boot_allow_key_backfill(json_config_key_read_t read_status);

/** @brief Stable short name of a boot state (for logs); never NULL. */
const char *json_config_boot_state_name(json_config_boot_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* _JSON_CONFIG_BOOT_POLICY_H_ */
