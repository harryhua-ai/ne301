/**
 ******************************************************************************
 * @file    docsnap_service.c
 * @brief   Docsnap trigger state machine (see docsnap/DESIGN.md §2, rev B).
 *
 *   Trigger:  ONE valid positive detection fires one capture (dedupe via the
 *             placement latch + capture-in-progress guard).  No consecutive-
 *             frame requirement (A fix 2).
 *   Photos:   each capture is independently tracked by image_id until its
 *             photo_ack arrives; paper removal only re-opens capture
 *             eligibility and NEVER drops an in-flight photo (A fix 2).
 *   Ack:      requires an exact image_id match (A fix 1).  Duplicates,
 *             unknown ids and id-less acks are rejected and logged.  NeoMind
 *             side fires the ack from the post-storage image event, so the
 *             ack proves full receipt (protocol evidence: docsnap/DESIGN.md).
 *   LED:      soft PWM blink once per confirmed photo; previous brightness
 *             and configuration are saved and restored afterwards (A §2).
 *   Upload:   retry/persistence owned by upload_coordinator; this service
 *             never re-queues or re-captures.
 *
 *   Known limitation: reboot returns to ARMED and loses ack matching for
 *   photos already in flight (their upload keeps retrying, DESIGN §7).
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 CamThink.
 * All rights reserved.
 *
 ******************************************************************************
 */

#include "docsnap_service.h"
#include "system_service.h"
#include "device_service.h"
#include "json_config_mgr.h"
#include "debug.h"
#include <string.h>
#include <stdio.h>

#define DOCSNAP_FLAG_AI         (1UL << 0)
#define DOCSNAP_FLAG_ACK        (1UL << 1)
#define DOCSNAP_FLAG_STOP       (1UL << 2)

#define DOCSNAP_THREAD_STACK    4096u
#define DOCSNAP_TICK_MS         1000u

typedef enum {
    DOCSNAP_ST_ARMED = 0,     /* paper absent / placement closed: may capture */
    DOCSNAP_ST_PLACED,        /* paper present, this placement already shot   */
} docsnap_state_t;

typedef struct {
    char    image_id[80];
    aicam_bool_t consumed;           /* ack consumed; slot reusable        */
} docsnap_photo_t;

static const char *state_str(docsnap_state_t s)
{
    return (s == DOCSNAP_ST_ARMED) ? "ARMED" : "PLACED";
}

typedef struct {
    service_state_t svc_state;
    osThreadId_t thread;
    osEventFlagsId_t ev;
    osMutexId_t mtx;                  /* guards photos[] + ack mailbox   */
    volatile aicam_bool_t running;
    volatile aicam_bool_t initialized;

    docsnap_state_t state;            /* placement latch                 */
    volatile aicam_bool_t ai_present; /* last sample (AI thread writer)  */
    uint8_t abs_count;                /* consecutive invalid samples     */

    docsnap_photo_t photos[DOCSNAP_MAX_INFLIGHT];

    /* ack mailbox (MQTT thread -> worker) */
    volatile aicam_bool_t ack_pending;
    char ack_image_id[80];
} docsnap_ctx_t;

static docsnap_ctx_t g_docsnap = {0};

/* ------------------------- feed points (non-blocking) ------------------- */

void docsnap_service_notify_ai(aicam_bool_t present)
{
    if (!g_docsnap.running) {
        return;
    }
    g_docsnap.ai_present = present;   /* single bool write, worker reads next tick */
    osEventFlagsSet(g_docsnap.ev, DOCSNAP_FLAG_AI);
}

void docsnap_service_notify_ack(const char *image_id)
{
    if (!g_docsnap.running) {
        return;
    }
    if (osMutexAcquire(g_docsnap.mtx, 50) == osOK) {
        g_docsnap.ack_image_id[0] = '\0';
        if (image_id && image_id[0]) {
            strncpy(g_docsnap.ack_image_id, image_id, sizeof(g_docsnap.ack_image_id) - 1);
            g_docsnap.ack_image_id[sizeof(g_docsnap.ack_image_id) - 1] = '\0';
        }
        g_docsnap.ack_pending = AICAM_TRUE;
        osMutexRelease(g_docsnap.mtx);
        osEventFlagsSet(g_docsnap.ev, DOCSNAP_FLAG_ACK);
    }
    /* mutex-acquire failure drops the ack: safe under-signal direction
     * (photo is still retried by the coordinator; retry path re-acks). */
}

/* ------------------------------ actions --------------------------------- */

static void docsnap_soft_blink(void)
{
    light_config_t saved;
    aicam_bool_t have_saved = AICAM_FALSE;

    /* save → override → blink → restore (A: must not break fill-light state) */
    if (device_service_light_get_config(&saved) == AICAM_OK) {
        have_saved = AICAM_TRUE;
    }
    aicam_result_t r = device_service_light_set_brightness(DOCSNAP_LED_DUTY_PERCENT);
    if (r != AICAM_OK) {
        LOG_SVC_WARN("docsnap: led brightness set failed %d", r);
        if (have_saved) {
            (void)device_service_light_set_config(&saved);
        }
        return;
    }
    r = device_service_light_blink(1, DOCSNAP_LED_BLINK_MS);
    if (r == AICAM_OK) {
        LOG_SVC_INFO("docsnap: soft blink sent (duty=%u%% on=%ums)",
                     DOCSNAP_LED_DUTY_PERCENT, DOCSNAP_LED_BLINK_MS);
    } else {
        LOG_SVC_WARN("docsnap: led blink failed %d (upload state unaffected)", r);
    }

    if (have_saved) {
        if (device_service_light_set_config(&saved) != AICAM_OK) {
            LOG_SVC_WARN("docsnap: light config restore failed");
        }
    }
}

static docsnap_photo_t *docsnap_photo_slot(void)
{
    for (int i = 0; i < DOCSNAP_MAX_INFLIGHT; i++) {
        if (g_docsnap.photos[i].image_id[0] == '\0') {
            return &g_docsnap.photos[i];
        }
    }
    return NULL;
}

static void docsnap_fire_capture(void)
{
    docsnap_photo_t *slot = docsnap_photo_slot();
    if (!slot) {
        /* A fix 2: removal must not drop in-flight photos; with the tracking
         * table full the sample is dropped and the next placement round
         * reuses slots as acks land.  Unreachable in practice (cap 4). */
        LOG_SVC_WARN("docsnap: inflight table full, capture deferred");
        return;
    }

    system_capture_request_t req = {
        .enable_ai      = AICAM_FALSE,   /* presence already validated upstream */
        .chunk_size     = 0,             /* auto */
        .store_to_sd    = AICAM_FALSE,
        .fast_fail_mqtt = AICAM_TRUE,    /* never stall the worker on a dead link;
                                            the coordinator keeps the photo queued */
        .trigger_type   = AICAM_CAPTURE_TRIGGER_AI_DOC,
    };
    system_capture_response_t resp = {0};

    LOG_SVC_INFO("docsnap: firing capture (state=%s)", state_str(g_docsnap.state));
    aicam_result_t r = system_service_capture_request(&req, &resp);
    if (r != AICAM_OK) {
        /* busy / OTA / camera error: placement latch NOT opened - the next
         * valid detection retries.  One capture per placement is preserved
         * by opening the latch only on success. */
        LOG_SVC_WARN("docsnap: capture failed %d - latch not opened, next detection retries", r);
        return;
    }

    strncpy(slot->image_id, resp.image_id, sizeof(slot->image_id) - 1);
    slot->image_id[sizeof(slot->image_id) - 1] = '\0';
    slot->consumed = AICAM_FALSE;
    g_docsnap.state = DOCSNAP_ST_PLACED;   /* latch: one capture per placement */
    g_docsnap.abs_count = 0;
    LOG_SVC_INFO("docsnap: captured id=%s -> PLACED (capture+upload %llu ms)",
                 slot->image_id[0] ? slot->image_id : "<n/a>",
                 (unsigned long long)resp.duration_ms);
}

static void docsnap_handle_ack(void)
{
    char ack_id[80] = {0};
    docsnap_photo_t *match = NULL;
    aicam_bool_t dup = AICAM_FALSE;

    if (osMutexAcquire(g_docsnap.mtx, 50) == osOK) {
        g_docsnap.ack_pending = AICAM_FALSE;
        strncpy(ack_id, g_docsnap.ack_image_id, sizeof(ack_id) - 1);
        osMutexRelease(g_docsnap.mtx);
    }

    /* A fix 1: id-less acks are never accepted as success */
    if (ack_id[0] == '\0') {
        LOG_SVC_WARN("docsnap: photo_ack without image_id - rejected");
        return;
    }

    for (int i = 0; i < DOCSNAP_MAX_INFLIGHT; i++) {
        if (strcmp(g_docsnap.photos[i].image_id, ack_id) == 0) {
            if (g_docsnap.photos[i].consumed) {
                dup = AICAM_TRUE;        /* duplicate ack for an already-confirmed photo */
            } else {
                match = &g_docsnap.photos[i];
            }
            break;
        }
    }
    if (!match) {
        LOG_SVC_WARN("docsnap: photo_ack %s - %s", ack_id,
                     dup ? "duplicate (already confirmed), ignored" : "unknown id, rejected");
        return;
    }

    LOG_SVC_INFO("docsnap: photo_ack confirmed (id=%s)", ack_id);
    docsnap_soft_blink();
    match->consumed = AICAM_TRUE;      /* further acks for this id hit the dup branch */
    match->image_id[0] = '\0';
}

static void docsnap_handle_ai_sample(void)
{
    aicam_bool_t present = g_docsnap.ai_present;

    switch (g_docsnap.state) {
        case DOCSNAP_ST_ARMED:
            /* A fix 2: single valid detection fires; the placement latch +
             * capture guard handle repeated detections while paper stays */
            if (present) {
                docsnap_fire_capture();
            } else {
                g_docsnap.abs_count = 0;
            }
            break;

        case DOCSNAP_ST_PLACED:
            g_docsnap.abs_count = (uint8_t)(!present ? g_docsnap.abs_count + 1 : 0);
            if (g_docsnap.abs_count >= DOCSNAP_ABSENT_CONSECUTIVE) {
                /* re-open capture eligibility only; in-flight photos stay
                 * tracked and their acks still blink (A fix 2) */
                LOG_SVC_INFO("docsnap: paper removed -> ARMED (inflight photos keep "
                             "uploading, acks still blink)");
                g_docsnap.state = DOCSNAP_ST_ARMED;
                g_docsnap.abs_count = 0;
            }
            break;

        default:
            g_docsnap.state = DOCSNAP_ST_ARMED;
            break;
    }
}

/* ------------------------------- worker --------------------------------- */

static void docsnap_task(void *arg)
{
    (void)arg;
    LOG_SVC_INFO("docsnap: worker started");
    while (g_docsnap.running) {
        uint32_t flags = osEventFlagsWait(g_docsnap.ev,
                                          DOCSNAP_FLAG_AI | DOCSNAP_FLAG_ACK | DOCSNAP_FLAG_STOP,
                                          osFlagsWaitAny, DOCSNAP_TICK_MS);
        /* osEventFlagsWait returns osFlagsError* (0x8xxxxxxx) on timeout -
         * those values alias our low-bit flags, so gate on the error bit */
        if (flags & 0x80000000UL) {
            continue;
        }
        if (flags & DOCSNAP_FLAG_STOP) {
            break;
        }
        if (flags & DOCSNAP_FLAG_ACK) {
            docsnap_handle_ack();
        }
        if (flags & DOCSNAP_FLAG_AI) {
            docsnap_handle_ai_sample();
        }
    }
    g_docsnap.svc_state = SERVICE_STATE_SHUTDOWN;
    LOG_SVC_INFO("docsnap: worker exited");
}

/* --------------------------- service plumbing --------------------------- */

aicam_result_t docsnap_service_init(void *config)
{
    (void)config;
    if (g_docsnap.initialized) {
        return AICAM_OK;
    }
    memset(&g_docsnap, 0, sizeof(g_docsnap));
    g_docsnap.svc_state = SERVICE_STATE_UNINITIALIZED;
    g_docsnap.state = DOCSNAP_ST_ARMED;

    g_docsnap.ev = osEventFlagsNew(NULL);
    g_docsnap.mtx = osMutexNew(NULL);
    if (!g_docsnap.ev || !g_docsnap.mtx) {
        LOG_SVC_ERROR("docsnap: os objects failed");
        return AICAM_ERROR;
    }

    /* Deployment requirement: photo-level PUBACK semantics need QoS >= 1
     * (QoS0 never raises the PUBLISHED event the coordinator waits on). */
    mqtt_service_config_t cfg;
    if (json_config_get_mqtt_service_config(&cfg) == AICAM_OK && cfg.data_report_qos == 0) {
        LOG_SVC_WARN("docsnap: data_report_qos=0 - set it to 1 for reliable "
                     "upload + ack flow (see docsnap/DESIGN.md §3)");
    }

    g_docsnap.initialized = AICAM_TRUE;
    g_docsnap.svc_state = SERVICE_STATE_INITIALIZED;
    LOG_SVC_INFO("docsnap: init ok (absent_m=%d duty=%u%% blink=%ums inflight_cap=%d)",
                 DOCSNAP_ABSENT_CONSECUTIVE, DOCSNAP_LED_DUTY_PERCENT,
                 DOCSNAP_LED_BLINK_MS, DOCSNAP_MAX_INFLIGHT);
    return AICAM_OK;
}

aicam_result_t docsnap_service_start(void)
{
    if (!g_docsnap.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    if (g_docsnap.running) {
        return AICAM_OK;
    }
    g_docsnap.running = AICAM_TRUE;
    g_docsnap.state = DOCSNAP_ST_ARMED;
    osThreadAttr_t attr = {0};
    attr.name = "docsnap";
    attr.stack_size = DOCSNAP_THREAD_STACK;
    attr.priority = osPriorityBelowNormal;
    g_docsnap.thread = osThreadNew(docsnap_task, NULL, &attr);
    if (!g_docsnap.thread) {
        g_docsnap.running = AICAM_FALSE;
        LOG_SVC_ERROR("docsnap: thread create failed");
        return AICAM_ERROR;
    }
    g_docsnap.svc_state = SERVICE_STATE_RUNNING;
    return AICAM_OK;
}

aicam_result_t docsnap_service_stop(void)
{
    if (!g_docsnap.initialized || !g_docsnap.running) {
        return AICAM_OK;
    }
    g_docsnap.running = AICAM_FALSE;
    osEventFlagsSet(g_docsnap.ev, DOCSNAP_FLAG_STOP);
    if (g_docsnap.thread) {
        osThreadJoin(g_docsnap.thread);
        g_docsnap.thread = NULL;
    }
    g_docsnap.svc_state = SERVICE_STATE_SHUTDOWN;
    return AICAM_OK;
}

aicam_result_t docsnap_service_deinit(void)
{
    docsnap_service_stop();
    if (g_docsnap.ev) {
        osEventFlagsDelete(g_docsnap.ev);
        g_docsnap.ev = NULL;
    }
    if (g_docsnap.mtx) {
        osMutexDelete(g_docsnap.mtx);
        g_docsnap.mtx = NULL;
    }
    g_docsnap.initialized = AICAM_FALSE;
    g_docsnap.svc_state = SERVICE_STATE_UNINITIALIZED;
    return AICAM_OK;
}

service_state_t docsnap_service_get_state(void)
{
    return g_docsnap.svc_state;
}
