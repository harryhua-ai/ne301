/**
 * @file json_config_boot_policy.c
 * @brief Pure boot-state classification/policy for the JSON config NVS layer.
 * @details See json_config_boot_policy.h. This file is intentionally free of
 *          any hardware/RTOS dependency so the decision table can be exercised
 *          by host error-injection tests (issue #37 AC4).
 */

#include "json_config_boot_policy.h"

json_config_boot_state_t json_config_boot_classify(bool backend_ready,
                                                   int blank_check_err,
                                                   bool blank,
                                                   int magic_read_err,
                                                   uint32_t magic_value,
                                                   uint32_t magic_expected)
{
    /* Evidence-gathering failed or backend down: state ③, refuse. */
    if (!backend_ready) {
        return JSON_CONFIG_BOOT_BACKEND_UNAVAILABLE;
    }
    /* An unreadable partition is never treated as blank (fail closed). */
    if (blank_check_err != 0) {
        return JSON_CONFIG_BOOT_BACKEND_UNAVAILABLE;
    }

    bool magic_ok = (magic_read_err == 0) && (magic_value == magic_expected);
    if (magic_ok) {
        return JSON_CONFIG_BOOT_PERSISTED;
    }

    /* Missing/mismatched magic alone is NOT a blank proof: only a completed
     * read-only erase-check of the whole partition may classify the device as
     * blank. A proven-blank device is PENDING_INIT: it waits for the explicit
     * authorized initialization; it is NOT auto-initialized at boot
     * (issue #37 review Blocker 2). Anything else keeps the old bytes
     * untouched. */
    if (blank) {
        return JSON_CONFIG_BOOT_PENDING_INIT;
    }
    return JSON_CONFIG_BOOT_UNRECOGNIZED;
}

bool json_config_boot_allow_persist(json_config_boot_state_t state)
{
    /* Only a recognized stored configuration authorizes writes. A blank
     * device stays PENDING_INIT: with no provable independent first-boot
     * authorization in the approved product flow it must not be written
     * automatically, and blank evidence is not permission (rev 3 BLOCKER 2).
     * Unrecognized/unavailable states preserve the medium untouched. */
    return (state == JSON_CONFIG_BOOT_PERSISTED);
}

bool json_config_boot_allow_admin_auth(json_config_boot_state_t state)
{
    /* Rev 3 (BLOCKER 1 + BLOCKER 2): admin auth requires BOTH a recognized
     * stored configuration AND a provably read credential (see
     * json_config_boot_assess_credential). A blank device has no stored
     * credential, so the public compile-time default must not be elevated to
     * a working admin credential; the earlier PENDING_INIT bootstrap design
     * is withdrawn - first-boot policy is an A/User decision. */
    return (state == JSON_CONFIG_BOOT_PERSISTED);
}

bool json_config_boot_allow_factory_reset(bool persist_blocked)
{
    /* Rev 3 (BLOCKER 2/3): the factory-reset entry is only the ordinary
     * admin action on a healthy persisted session. It is NOT an
     * initialization path for blank/unknown media: blocked sessions
     * (PENDING_INIT / UNRECOGNIZED / BACKEND_UNAVAILABLE) refuse, and no
     * session latch is pre-opened before the reset work is attempted. */
    return (persist_blocked == false);
}

json_config_cred_state_t json_config_boot_assess_credential(int auth_key_err,
                                                            int legacy_key_err)
{
    /* BLOCKER 1 (rev 3): distinguish a PROVABLE stored/legacy credential
     * from an unknown read failure. Only an actual successful read proves
     * the credential; if both keys fail (missing OR backend error) the RAM
     * copy is the compile-time default and must not be trusted, used, or
     * written back. */
    if (auth_key_err == 0 || legacy_key_err == 0) {
        return JSON_CONFIG_CRED_PROVEN;
    }
    return JSON_CONFIG_CRED_UNKNOWN;
}

const char *json_config_boot_state_name(json_config_boot_state_t state)
{
    switch (state) {
    case JSON_CONFIG_BOOT_PERSISTED:
        return "PERSISTED";
    case JSON_CONFIG_BOOT_PENDING_INIT:
        return "PENDING_INIT(blank-awaiting-authorized-init)";
    case JSON_CONFIG_BOOT_UNRECOGNIZED:
        return "UNRECOGNIZED(media-preserved)";
    case JSON_CONFIG_BOOT_BACKEND_UNAVAILABLE:
    default:
        return "BACKEND_UNAVAILABLE";
    }
}
