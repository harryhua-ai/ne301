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
#include "upload_coordinator.h"
#include "common_utils.h"
#include "debug.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define DOCSNAP_FLAG_AI         (1UL << 0)
#define DOCSNAP_FLAG_ACK        (1UL << 1)
#define DOCSNAP_FLAG_STOP       (1UL << 2)

/* 16KB: the worker runs the full capture+meta+persist chain synchronously
 * (system_service_capture_request on the caller stack).  4KB overflowed and
 * HardFaulted on the first AI-doc capture (A, 2026-11-23); the same chain is
 * proven on the 32KB web-server thread.  If the watermark log below ever
 * shows the remaining space shrinking towards 0, raise it based on real
 * usage instead of treating this as an unbounded safety margin. */
#define DOCSNAP_THREAD_STACK    (4096u * 4)
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

    /* bounded ack mailbox (MQTT thread -> worker): FIFO, entries are never
     * overwritten while unprocessed (A fix 1).  Full queue drops the NEW
     * ack with a WARN - with the 4-photo cap this means the worker is
     * wedged, which the drop log makes visible. */
    char ack_queue[DOCSNAP_MAX_ACK_QUEUE][80];
    uint8_t ack_head;                 /* next to process */
    uint8_t ack_count;
    volatile aicam_bool_t degraded;   /* upload path cannot deliver JPEG */
} docsnap_ctx_t;

static docsnap_ctx_t g_docsnap = {0};

/* Explicit static stack, like every other service thread: osThreadNew
 * without stack_mem is the only path in the app that allocates from the
 * CMSIS StackBytePool, which hides the address from the MAP and made the
 * overflow diagnosis much harder.  ThreadX fills fresh stacks with 0xEF
 * (TX_STACK_FILL) - docsnap_log_stack_watermark() relies on that to report
 * the true low-water mark after each capture. */
static uint8_t docsnap_worker_stack[DOCSNAP_THREAD_STACK] __attribute__((aligned(32))) IN_PSRAM;

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
    /* gate on initialized (not running): the mailbox exists from init on,
     * so an ack arriving in the init-to-start window is queued, not lost */
    if (!g_docsnap.initialized) {
        return;
    }
    if (osMutexAcquire(g_docsnap.mtx, 50) == osOK) {
        /* duplicate id already queued: collapse (idempotent, zero loss) */
        for (uint8_t i = 0; i < g_docsnap.ack_count; i++) {
            const char *e = g_docsnap.ack_queue[(g_docsnap.ack_head + i) % DOCSNAP_MAX_ACK_QUEUE];
            const char *nid = (image_id && image_id[0]) ? image_id : "";
            if (strcmp(e, nid) == 0) {
                osMutexRelease(g_docsnap.mtx);
                return;
            }
        }
        if (g_docsnap.ack_count >= DOCSNAP_MAX_ACK_QUEUE) {
            /* A P1: an ack for a TRACKED photo must not be dropped in favor
             * of untracked noise.  If the incoming id matches a tracked
             * photo, evict one untracked entry to make room; only drop the
             * new ack when it is itself untracked (or no untracked entry
             * exists - unreachable: tracked acks <= 4 photos < 8 slots). */
            const char *nid = (image_id && image_id[0]) ? image_id : "";
            aicam_bool_t incoming_tracked = AICAM_FALSE;
            for (int i = 0; i < DOCSNAP_MAX_INFLIGHT; i++) {
                if (g_docsnap.photos[i].image_id[0] != '\0' &&
                    strcmp(g_docsnap.photos[i].image_id, nid) == 0) {
                    incoming_tracked = AICAM_TRUE;
                    break;
                }
            }
            int8_t evict = -1;
            if (incoming_tracked) {
                for (uint8_t i = 0; i < g_docsnap.ack_count; i++) {
                    uint8_t pos = (uint8_t)((g_docsnap.ack_head + i) % DOCSNAP_MAX_ACK_QUEUE);
                    aicam_bool_t known = AICAM_FALSE;
                    for (int j = 0; j < DOCSNAP_MAX_INFLIGHT; j++) {
                        if (g_docsnap.photos[j].image_id[0] != '\0' &&
                            strcmp(g_docsnap.photos[j].image_id, g_docsnap.ack_queue[pos]) == 0) {
                            known = AICAM_TRUE;
                            break;
                        }
                    }
                    if (!known) {
                        evict = (int8_t)pos;
                        break;
                    }
                }
            }
            if (evict < 0) {
                osMutexRelease(g_docsnap.mtx);
                LOG_SVC_WARN("docsnap: ack queue full, dropped ack id=%s (worker stalled?)",
                             nid[0] ? nid : "<none>");
                return;
            }
            LOG_SVC_WARN("docsnap: queue full - evicting untracked ack to admit tracked id=%s", nid);
            strncpy(g_docsnap.ack_queue[evict], nid, 79);
            g_docsnap.ack_queue[evict][79] = '\0';
            osMutexRelease(g_docsnap.mtx);
            osEventFlagsSet(g_docsnap.ev, DOCSNAP_FLAG_ACK);
            return;
        }
        char *slot = g_docsnap.ack_queue[(g_docsnap.ack_head + g_docsnap.ack_count) % DOCSNAP_MAX_ACK_QUEUE];
        slot[0] = '\0';
        if (image_id && image_id[0]) {
            strncpy(slot, image_id, 79);
            slot[79] = '\0';
        }
        g_docsnap.ack_count++;
        osMutexRelease(g_docsnap.mtx);
        osEventFlagsSet(g_docsnap.ev, DOCSNAP_FLAG_ACK);
    }
    /* mutex-acquire failure drops the ack: safe under-signal direction
     * (photo is still retried by the coordinator; retry path re-acks). */
}

/* ------------------------------ actions --------------------------------- */

static void docsnap_soft_blink(void)
{
    /* save → override → blink → settle → restore.  Restore uses
     * set_brightness ONLY: set_config persists to NVS (A fix 3 - no
     * persistent write per success hint), and brightness is the only field
     * the hint touches.  The settle wait lets the async blink engine finish
     * its 250 ms pulse before the duty is put back. */
    light_config_t saved;
    aicam_bool_t have_saved = AICAM_FALSE;
    if (device_service_light_get_config(&saved) == AICAM_OK) {
        have_saved = AICAM_TRUE;
    }
    aicam_result_t r = device_service_light_set_brightness(DOCSNAP_LED_DUTY_PERCENT);
    if (r != AICAM_OK) {
        LOG_SVC_WARN("docsnap: led brightness set failed %d", r);
        return;
    }
    r = device_service_light_blink(1, DOCSNAP_LED_BLINK_MS);
    if (r == AICAM_OK) {
        LOG_SVC_INFO("docsnap: soft blink sent (duty=%u%% on=%ums)",
                     DOCSNAP_LED_DUTY_PERCENT, DOCSNAP_LED_BLINK_MS);
    } else {
        LOG_SVC_WARN("docsnap: led blink failed %d (upload state unaffected)", r);
    }
    osDelay(DOCSNAP_LED_SETTLE_MS);
    if (have_saved) {
        r = device_service_light_set_brightness(saved.brightness_level);
        if (r != AICAM_OK) {
            LOG_SVC_WARN("docsnap: light brightness restore failed %d", r);
        }
    }
}

static docsnap_photo_t *docsnap_photo_slot(void)
{
    for (int i = 0; i < DOCSNAP_MAX_INFLIGHT; i++) {
        /* free = consumed (ack done, id kept for duplicate detection) or
         * never used; a new capture overwrites the remembered id */
        if (g_docsnap.photos[i].consumed ||
            g_docsnap.photos[i].image_id[0] == '\0') {
            return &g_docsnap.photos[i];
        }
    }
    return NULL;
}

/* A (2026-11-23): walk up from the stack base until the TX 0xEF fill ends -
 * that offset is the deepest byte this thread has ever touched, i.e. the
 * minimum remaining stack across all runs so far.  Called on the worker
 * itself right after the capture chain returns. */
static void docsnap_log_stack_watermark(void)
{
    const uint8_t *base = docsnap_worker_stack;
    const uint8_t *top  = base + sizeof(docsnap_worker_stack);
    const uint8_t *p    = base;
    while (p < top && *p == 0xEFU) {
        p++;
    }
    LOG_SVC_INFO("docsnap: stack watermark: min remaining %lu of %lu bytes",
                 (unsigned long)(p - base), (unsigned long)sizeof(docsnap_worker_stack));
}

static void docsnap_fire_capture(void)
{
    /* Upload precondition (A deployment constraint): a photo that can never
     * reach NeoMind must not consume a placement round. */
    if (g_docsnap.degraded) {
        LOG_SVC_WARN("docsnap: upload path cannot deliver JPEG "
                     "(LOCAL_ONLY or METADATA_ONLY) - capture skipped");
        return;
    }
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
    docsnap_log_stack_watermark();
    if (r != AICAM_OK) {
        /* busy / OTA / camera error: placement latch NOT opened - the next
         * valid detection retries.  One capture per placement is preserved
         * by opening the latch only on success. */
        LOG_SVC_WARN("docsnap: capture failed %d - latch not opened, next detection retries", r);
        return;
    }

    /* A fix 2: a successful capture transaction MUST carry a non-empty id -
     * without it the photo can never be correlated with a photo_ack. */
    if (resp.image_id[0] == '\0') {
        LOG_SVC_ERROR("docsnap: capture ok but empty image_id - cannot track, "
                      "treat as failure (latch not opened)");
        return;
    }
    strncpy(slot->image_id, resp.image_id, sizeof(slot->image_id) - 1);
    slot->image_id[sizeof(slot->image_id) - 1] = '\0';
    slot->consumed = AICAM_FALSE;
    g_docsnap.state = DOCSNAP_ST_PLACED;   /* latch: one capture per placement */
    g_docsnap.abs_count = 0;
    LOG_SVC_INFO("docsnap: captured id=%s -> PLACED (capture+upload %llu ms)",
                 slot->image_id, (unsigned long long)resp.duration_ms);
}

static void docsnap_handle_ack(void)
{
    char ack_id[80] = {0};
    docsnap_photo_t *match = NULL;
    aicam_bool_t dup = AICAM_FALSE;

    /* drain one entry per flag; the worker loop re-enters while the queue
     * is non-empty (ACK flag stays set below) */
    if (osMutexAcquire(g_docsnap.mtx, 50) == osOK) {
        if (g_docsnap.ack_count > 0) {
            strncpy(ack_id, g_docsnap.ack_queue[g_docsnap.ack_head], sizeof(ack_id) - 1);
            ack_id[sizeof(ack_id) - 1] = '\0';
            g_docsnap.ack_queue[g_docsnap.ack_head][0] = '\0';
            g_docsnap.ack_head = (uint8_t)((g_docsnap.ack_head + 1) % DOCSNAP_MAX_ACK_QUEUE);
            g_docsnap.ack_count--;
        }
        if (g_docsnap.ack_count > 0) {
            osEventFlagsSet(g_docsnap.ev, DOCSNAP_FLAG_ACK);  /* more pending */
        }
        osMutexRelease(g_docsnap.mtx);
    }
    else {
        return;
    }

    /* A fix 1: id-less acks are never accepted as success */
    if (ack_id[0] == '\0') {
        LOG_SVC_WARN("docsnap: photo_ack without image_id - rejected");
        return;
    }

    for (int i = 0; i < DOCSNAP_MAX_INFLIGHT; i++) {
        if (g_docsnap.photos[i].image_id[0] != '\0' &&
            strcmp(g_docsnap.photos[i].image_id, ack_id) == 0) {
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
    /* keep image_id so duplicate acks are recognisable; consumed frees the
     * slot for the next capture */
    match->consumed = AICAM_TRUE;
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

/* --------------------- restart recovery & preconditions ------------------ */

/* A fix (restart semantics): unconfirmed photos survive a reboot in the
 * upload_coordinator (meta JSON keeps image_id).  Seed the ack-tracking
 * table from PENDING ai_doc records so their photo_ack still blinks after
 * the reboot - no second persistence queue. */
static void docsnap_seed_from_coordinator(void)
{
    /* A P1: filter FIRST, cap second - other trigger types must not consume
     * docsnap recovery slots.  Bounded scan: newest-first pages of 8 until 4
     * ai_doc records are found or 32 pending records scanned. */
    int seeded = 0;
    for (uint32_t offset = 0; offset < 32 && seeded < DOCSNAP_MAX_INFLIGHT; offset += 8) {
        record_info_t recs[8];
        int n = upload_coordinator_list_records(RECORD_STATE_PENDING,
                                                offset, 8,
                                                0, UINT64_MAX,
                                                AICAM_TRUE /* newest first */,
                                                recs, 8);
        if (n <= 0) {
            break;
        }
        for (int i = 0; i < n && seeded < DOCSNAP_MAX_INFLIGHT; i++) {
            if (strcmp(recs[i].trigger, "ai_doc") != 0 || recs[i].image_id[0] == '\0') {
                continue;
            }
            docsnap_photo_t *slot = docsnap_photo_slot();
            if (!slot) {
                break;
            }
            strncpy(slot->image_id, recs[i].image_id, sizeof(slot->image_id) - 1);
            slot->image_id[sizeof(slot->image_id) - 1] = '\0';
            slot->consumed = AICAM_FALSE;
            seeded++;
        }
    }
    if (seeded > 0) {
        LOG_SVC_INFO("docsnap: seeded %d unconfirmed photo(s) from upload "
                     "coordinator (acks will still blink)", seeded);
    }
}

/* A deployment constraint: docsnap only makes sense when the full JPEG
 * reaches the receiver.  LOCAL_ONLY never uploads; METADATA_ONLY strips
 * the image from the MQTT report.  Both are flagged degraded at start and
 * gate the capture (photos would still persist locally). */
static void docsnap_check_upload_path(void)
{
    g_docsnap.degraded = AICAM_FALSE;
    capture_upload_config_t up;
    if (json_config_get_capture_upload_config(&up) == AICAM_OK &&
        up.mode == CAPTURE_MODE_LOCAL_ONLY) {
        LOG_SVC_ERROR("docsnap: capture mode LOCAL_ONLY - JPEG will not upload; "
                      "set an upload mode before expecting ack/LED flow");
        g_docsnap.degraded = AICAM_TRUE;
    }
    if (mqtt_service_get_report_content() == MQTT_REPORT_CONTENT_METADATA_ONLY) {
        LOG_SVC_ERROR("docsnap: MQTT report content METADATA_ONLY - image "
                      "stripped from reports; set FULL before expecting ack/LED flow");
        g_docsnap.degraded = AICAM_TRUE;
    }
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
    /* A timing fix: seed HERE, in the init phase.  Priority order gives us
     * upload_coordinator init (11) -> docsnap init (12), and the coordinator
     * only starts kicking old-photo uploads in its START - so recovery
     * registration is always in place before any upload (and therefore any
     * ack) can happen.  start() no longer seeds. */
    docsnap_seed_from_coordinator();
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
    /* a previous stop() may have left STOP (or stale AI/ACK) flags set;
     * the new worker must not consume them (host test caught this) */
    osEventFlagsClear(g_docsnap.ev, DOCSNAP_FLAG_AI | DOCSNAP_FLAG_ACK | DOCSNAP_FLAG_STOP);
    /* A P1: acks queued before start (init->ack->start window, or a
     * stop->start with unprocessed acks) would sit in the mailbox with the
     * flag cleared - re-signal so the worker drains them */
    if (osMutexAcquire(g_docsnap.mtx, 50) == osOK) {
        aicam_bool_t pending = (g_docsnap.ack_count > 0) ? AICAM_TRUE : AICAM_FALSE;
        osMutexRelease(g_docsnap.mtx);
        if (pending) {
            osEventFlagsSet(g_docsnap.ev, DOCSNAP_FLAG_ACK);
        }
    }
    osThreadAttr_t attr = {0};
    attr.name = "docsnap";
    attr.stack_mem = docsnap_worker_stack;
    attr.stack_size = DOCSNAP_THREAD_STACK;
    attr.priority = osPriorityBelowNormal;
    g_docsnap.thread = osThreadNew(docsnap_task, NULL, &attr);
    if (!g_docsnap.thread) {
        g_docsnap.running = AICAM_FALSE;
        LOG_SVC_ERROR("docsnap: thread create failed");
        return AICAM_ERROR;
    }
    g_docsnap.svc_state = SERVICE_STATE_RUNNING;
    /* re-evaluated on every start: config may have changed across a stop */
    docsnap_check_upload_path();
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
