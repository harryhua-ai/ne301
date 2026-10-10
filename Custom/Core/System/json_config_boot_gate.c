#include "json_config_boot_gate.h"

json_config_key_status_t json_config_boot_gate_key_status(aicam_result_t read_result)
{
    if (read_result == AICAM_OK) {
        return JSON_CONFIG_KEY_PRESENT;
    }
    if (read_result == AICAM_ERROR_NOT_FOUND) {
        return JSON_CONFIG_KEY_MISSING;
    }
    return JSON_CONFIG_KEY_UNKNOWN;
}

int json_config_boot_gate_first_boot(storage_media_state_t user_state)
{
    return user_state == STORAGE_MEDIA_BLANK;
}

int json_config_boot_gate_backfill_allowed(json_config_key_status_t status)
{
    return status == JSON_CONFIG_KEY_MISSING;
}

int json_config_boot_gate_credential_persist_default_allowed(storage_media_state_t user_state,
                                                             json_config_key_status_t new_key)
{
    if (user_state != STORAGE_MEDIA_BLANK) {
        return 0;
    }
    return new_key == JSON_CONFIG_KEY_MISSING;
}

int json_config_boot_gate_credential_migrate_allowed(json_config_key_status_t new_key,
                                                     json_config_key_status_t old_key)
{
    return new_key == JSON_CONFIG_KEY_MISSING && old_key == JSON_CONFIG_KEY_PRESENT;
}
