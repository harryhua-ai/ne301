/**
 ******************************************************************************
 * @file    mqtt_image_seq.c
 * @brief   NVS-backed monotonic sequence for image ids.  See header.
 *
 *          Read-failure semantics (A P0): the generic uint32 NVS wrapper
 *          collapses "key not yet present" and "storage fault" into one
 *          error.  This unit therefore reads through the Hal API directly:
 *          -ENOENT (key absent = first ever use) bootstraps the counter at 1;
 *          ANY other read error refuses to emit an id, because writing after
 *          an unknown read state could overwrite a historical counter and
 *          reuse ids.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 CamThink.
 * All rights reserved.
 *
 ******************************************************************************
 */

#include "mqtt_image_seq.h"
#include "json_config_internal.h"   /* NVS_KEY_IMAGE_ID_SEQ + write helper */
#include "storage.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define MQTT_IMAGE_SEQ_NVS_KEY NVS_KEY_IMAGE_ID_SEQ

aicam_result_t mqtt_image_seq_next(uint32_t *out_seq)
{
    static uint32_t s_seq;      /* 0 = not yet loaded this boot */
    static aicam_bool_t s_loaded;

    if (!out_seq) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (!s_loaded) {
        char value_str[12] = {0};
        int ret = storage_nvs_read(NVS_USER, MQTT_IMAGE_SEQ_NVS_KEY,
                                   value_str, sizeof(value_str));
        if (ret >= 0) {
            s_seq = (uint32_t)strtoul(value_str, NULL, 10);
        } else if (ret == -ENOENT) {
            s_seq = 0;          /* key absent: genuine first-ever use */
        } else {
            /* storage fault: the prior counter value is unknown - a write
             * here could overwrite history and reuse ids.  Refuse. */
            return AICAM_ERROR;
        }
        s_loaded = AICAM_TRUE;
    }
    s_seq++;
    /* A P0: if persistence fails, uniqueness across reboots cannot be
     * guaranteed - report the failure instead of silently returning OK. */
    if (json_config_nvs_write_uint32(MQTT_IMAGE_SEQ_NVS_KEY, s_seq) != AICAM_OK) {
        return AICAM_ERROR;
    }
    *out_seq = s_seq;
    return AICAM_OK;
}
