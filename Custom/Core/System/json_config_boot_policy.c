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
    /* Only a recognized stored configuration authorizes writes. Blank media
     * must stay PENDING_INIT until the explicit authorized first init
     * (factory reset entry); unrecognized/unavailable states preserve the
     * medium untouched. */
    return (state == JSON_CONFIG_BOOT_PERSISTED);
}

bool json_config_boot_allow_admin_auth(json_config_boot_state_t state)
{
    switch (state) {
    case JSON_CONFIG_BOOT_PERSISTED:
        /* Stored credential is readable and authoritative. */
        return true;
    case JSON_CONFIG_BOOT_PENDING_INIT:
        /* Medium PROVEN empty: the factory default credential is the device's
         * real credential (bootstrap for first use / explicit init). */
        return true;
    case JSON_CONFIG_BOOT_UNRECOGNIZED:
    case JSON_CONFIG_BOOT_BACKEND_UNAVAILABLE:
    default:
        /* Corrupted or unverifiable credential source: the RAM default must
         * never become a valid admin credential (review Blocker 1). */
        return false;
    }
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
