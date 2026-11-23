/**
 ******************************************************************************
 * @file    mqtt_image_seq.c
 * @brief   NVS-backed monotonic sequence for image ids.  See header.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 CamThink.
 * All rights reserved.
 *
 ******************************************************************************
 */

#include "mqtt_image_seq.h"
#include "json_config_internal.h"

#define MQTT_IMAGE_SEQ_NVS_KEY NVS_KEY_IMAGE_ID_SEQ

aicam_result_t mqtt_image_seq_next(uint32_t *out_seq)
{
    static uint32_t s_seq;      /* 0 = not yet loaded this boot */
    static aicam_bool_t s_loaded;

    if (!out_seq) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (!s_loaded) {
        uint32_t persisted = 0;
        if (json_config_nvs_read_uint32(MQTT_IMAGE_SEQ_NVS_KEY, &persisted) != AICAM_OK) {
            persisted = 0;      /* fresh key / first boot: start from 0 */
        }
        s_seq = persisted;
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
