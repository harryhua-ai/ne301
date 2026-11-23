/**
 ******************************************************************************
 * @file    docsnap_service.h
 * @author  CamThink
 *
 * @brief   Docsnap trigger service: NE302 document-presence -> one HD capture
 *          -> NeoMind upload -> photo_ack -> soft LED blink -> re-arm.
 *
 *          Product semantics (A, 2026-11-23): the LED blink means "NeoMind
 *          confirmed receipt of the photo", NOT "shutter fired".  One capture
 *          per placement; paper must be removed before a new round arms.
 *
 *          Feed points:
 *          - ai_service result callback -> docsnap_service_notify_ai()
 *          - mqtt_service down/control {"cmd":"photo_ack"} ->
 *            docsnap_service_notify_ack()
 *          Upload retry/persistence is owned by upload_coordinator; this
 *          service never re-queues or re-captures.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 CamThink.
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef DOCSNAP_SERVICE_H
#define DOCSNAP_SERVICE_H

#include "aicam_types.h"
#include "service_interfaces.h"
#include "cmsis_os2.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Compile-time defaults (NVS config deferred; see docsnap/DESIGN.md). */
#define DOCSNAP_ABSENT_CONSECUTIVE   3     /**< invalid frames to re-open placement */
#define DOCSNAP_LED_DUTY_PERCENT     20    /**< soft blink PWM duty (>=15 kick)     */
#define DOCSNAP_LED_BLINK_MS         250   /**< soft blink on-time                  */
#define DOCSNAP_MAX_INFLIGHT         4     /**< per-photo ack tracking cap          */

aicam_result_t docsnap_service_init(void *config);
aicam_result_t docsnap_service_start(void);
aicam_result_t docsnap_service_stop(void);
aicam_result_t docsnap_service_deinit(void);
service_state_t docsnap_service_get_state(void);

/**
 * @brief Feed one AI presence sample (non-blocking, any thread).
 *        Called from the AI result callback path; only validity is used.
 * @param present AICAM_TRUE when the postprocess emitted a valid detection
 */
void docsnap_service_notify_ai(aicam_bool_t present);

/**
 * @brief Feed a photo_ack from MQTT (non-blocking, MQTT thread).
 * @param image_id confirmed image id, or NULL when the ack carries no id
 */
void docsnap_service_notify_ack(const char *image_id);

#ifdef __cplusplus
}
#endif

#endif /* DOCSNAP_SERVICE_H */
