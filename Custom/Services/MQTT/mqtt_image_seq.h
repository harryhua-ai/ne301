/**
 ******************************************************************************
 * @file    mqtt_image_seq.h
 * @brief   Persistent monotonic counter for image-id uniqueness (A fix:
 *          seconds-only or per-boot counters can collide after clock
 *          reset/revert; this counter survives reboots via NVS and is
 *          wall-clock independent).  Extracted as a tiny unit so the
 *          failure semantics are host-testable.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 CamThink.
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef MQTT_IMAGE_SEQ_H
#define MQTT_IMAGE_SEQ_H

#include "aicam_types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Next persistent sequence value.
 *        First call in a boot reads the NVS counter; every call increments
 *        and writes back.
 * @param out_seq receives the new value
 * @return AICAM_OK, or AICAM_ERROR when the counter could not be persisted
 *         (caller MUST NOT treat the id as unique in that case)
 */
aicam_result_t mqtt_image_seq_next(uint32_t *out_seq);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_IMAGE_SEQ_H */
