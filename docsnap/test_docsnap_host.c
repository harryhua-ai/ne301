/* Host simulation tests for the REAL docsnap_service.c
 * Covers A-required behavior classes: normal, failure, duplicate ack,
 * offline/recovery.  Single-threaded cooperative execution via setjmp. */
#include <stdio.h>
#include <setjmp.h>
#include <string.h>
#include <stdbool.h>
#include "docsnap_service.h"
#include "upload_coordinator.h"
#include "system_service.h"
#include "device_service.h"
#include "json_config_mgr.h"

int test_verbose = 1;
uint32_t harness_pending = 0;
uint32_t harness_wait_calls = 0;
static jmp_buf harness_ctx;
static volatile aicam_bool_t *harness_running_ptr;
static void docsnap_task(void *arg);

void harness_yield(void) { longjmp(harness_ctx, 1); }
static void (*harness_task_fn)(void*);
static void *harness_task_arg;
osThreadId_t harness_osThreadNew_hook(void(*fn)(void*),void *arg,const void *a){
    (void)a; harness_task_fn = fn; harness_task_arg = arg; return (void*)2;
}

/* ---- fake service deps ---- */
static int  fake_capture_calls;
static char fake_next_id[80];                 /* id the fake capture returns */
static aicam_result_t fake_capture_result;
aicam_result_t system_service_capture_request(const system_capture_request_t *req,
                                              system_capture_response_t *resp)
{
    fake_capture_calls++;
    if (resp) {
        resp->result = fake_capture_result;
        resp->duration_ms = 123;
        snprintf(resp->image_id, sizeof(resp->image_id), "%s",
                 fake_capture_result == AICAM_OK ? fake_next_id : "");
    }
    return fake_capture_result;
}
static int  fake_blink_calls;
static uint32_t fake_last_duty;
static int  fake_blink_fail;
static light_config_t fake_light_cfg;
static int fake_light_restore_calls;
static int fake_set_config_calls;                 /* persistent-write detector (A fix 3) */
static int fake_set_brightness_calls;
uint32_t harness_delay_calls;
aicam_result_t device_service_light_get_config(light_config_t *c){ *c = fake_light_cfg; return AICAM_OK; }
aicam_result_t device_service_light_set_config(const light_config_t *c){ fake_light_cfg = *c; fake_light_restore_calls++; fake_set_config_calls++; return AICAM_OK; }
aicam_result_t device_service_light_set_brightness(uint32_t d){ fake_last_duty = d; fake_set_brightness_calls++; return AICAM_OK; }
aicam_result_t device_service_light_blink(uint32_t t, uint32_t ms){ (void)t;(void)ms;
    if (fake_blink_fail) return AICAM_ERROR; fake_blink_calls++; return AICAM_OK; }
static uint8_t fake_qos = 1;
aicam_result_t json_config_get_mqtt_service_config(mqtt_service_config_t *c){ c->data_receive_qos=1; c->data_report_qos=fake_qos; return AICAM_OK; }
static capture_mode_t fake_up_mode = CAPTURE_MODE_BATCH;
aicam_result_t json_config_get_capture_upload_config(capture_upload_config_t *c){ c->mode = fake_up_mode; return AICAM_OK; }
static mqtt_report_content_t fake_rc = MQTT_REPORT_CONTENT_FULL;
mqtt_report_content_t mqtt_service_get_report_content(void){ return fake_rc; }

/* coordinator seeding fake */
static record_info_t fake_pending[16]; static int fake_pending_n;
int upload_coordinator_list_records(record_state_t filter, uint32_t offset, uint32_t limit,
  uint64_t from_ts, uint64_t to_ts, aicam_bool_t sort_desc, record_info_t *out, int max)
{
    (void)filter;(void)from_ts;(void)to_ts;(void)sort_desc;
    int n = 0;
    uint32_t emitted = 0;
    for (int i = 0; i < fake_pending_n && n < max; i++) {
        if ((uint32_t)i < offset) continue;
        if (emitted >= limit) break;
        out[n++] = fake_pending[i];
        emitted++;
    }
    return n;
}

static void step(void)
{
    /* run the worker until it goes idle (fake Wait longjmps) */
    if (setjmp(harness_ctx) == 0) {
        harness_task_fn(harness_task_arg);
    }
}

/* ----------------------------- assertions ----------------------------- */
static int failures = 0;
#define CHECK(cond) do{ if(!(cond)){ printf("  FAIL %s:%d %s\n",__FILE__,__LINE__,#cond); failures++; } }while(0)

static void reset_all(void)
{
    docsnap_service_deinit();
    harness_pending = 0; harness_wait_calls = 0;
    fake_capture_calls = 0; fake_next_id[0]=0; fake_capture_result = AICAM_OK;
    fake_blink_calls = 0; fake_last_duty = 0; fake_blink_fail = 0;
    fake_light_restore_calls = 0; fake_set_config_calls = 0; fake_set_brightness_calls = 0;
    memset(&fake_light_cfg, 0, sizeof(fake_light_cfg));
    fake_light_cfg.brightness_level = 70;   /* pre-existing fill-light state */
    fake_up_mode = CAPTURE_MODE_BATCH; fake_rc = MQTT_REPORT_CONTENT_FULL;
    fake_pending_n = 0; memset(fake_pending, 0, sizeof(fake_pending));
    harness_delay_calls = 0;
    test_verbose = 0;
    docsnap_service_init(NULL);
    docsnap_service_start();
    test_verbose = 1;
}

/* docsnap_task + its statics are inside docsnap_service.c; access through the
 * feed APIs and externally observable fakes only.  state probes: */
static const char *docsnap_state_str(void);   /* provided below via debug hook? */
/* We cannot read statics from the real file; observe behavior instead. */

static void feed_present(void){ docsnap_service_notify_ai(AICAM_TRUE); step(); }
static void feed_absent(void){ docsnap_service_notify_ai(AICAM_FALSE); step(); }
static void feed_ack(const char *id){ docsnap_service_notify_ack(id); step(); }

void test_normal_flow(void)
{
    printf("T1 normal flow\n");
    reset_all();
    snprintf(fake_next_id, sizeof(fake_next_id), "cam01_1");
    feed_present();                                  /* single detection fires */
    CHECK(fake_capture_calls == 1);
    /* repeated detections while paper stays: no more captures */
    feed_present(); feed_present();
    CHECK(fake_capture_calls == 1);
    CHECK(fake_blink_calls == 0);                    /* no ack -> no blink */
    /* ack with correct id */
    feed_ack(fake_next_id);
    CHECK(fake_blink_calls == 1);
    CHECK(fake_set_config_calls == 0);               /* NO persistent write (A fix 3) */
    CHECK(fake_set_brightness_calls >= 2);           /* duty + restore */
    CHECK(fake_last_duty == 70);                     /* runtime brightness restored */
    /* paper stays: still no re-capture after confirmation */
    feed_present();
    CHECK(fake_capture_calls == 1);
    /* removal x3 -> re-arm */
    feed_absent(); feed_absent(); feed_absent();
    /* new placement -> new capture, new id */
    snprintf(fake_next_id, sizeof(fake_next_id), "cam01_2");
    feed_present();
    CHECK(fake_capture_calls == 2);
    CHECK(fake_blink_calls == 1);
}

void test_capture_failure(void)
{
    printf("T2 capture failure\n");
    reset_all();
    fake_capture_result = AICAM_ERROR_BUSY;
    feed_present();
    CHECK(fake_capture_calls == 1);
    CHECK(fake_blink_calls == 0);
    /* latch not opened: next detection retries */
    fake_capture_result = AICAM_OK;
    snprintf(fake_next_id, sizeof(fake_next_id), "cam01_10");
    feed_present();
    CHECK(fake_capture_calls == 2);
    feed_ack(fake_next_id);
    CHECK(fake_blink_calls == 1);
}

void test_ack_rejections(void)
{
    printf("T3 ack rejection rules\n");
    reset_all();
    snprintf(fake_next_id, sizeof(fake_next_id), "cam01_7");
    feed_present();
    static char idbuf[80];
    snprintf(idbuf, sizeof(idbuf), "%s", fake_next_id);
    /* id-less ack rejected */
    feed_ack(NULL);
    CHECK(fake_blink_calls == 0);
    /* unknown id rejected */
    feed_ack("cam01_unknown");
    CHECK(fake_blink_calls == 0);
    /* correct id -> exactly one blink */
    feed_ack(idbuf);
    CHECK(fake_blink_calls == 1);
    /* duplicate ack for same id -> no second blink */
    feed_ack(idbuf);
    CHECK(fake_blink_calls == 1);
    /* random id after confirm -> unknown, no blink */
    feed_ack("cam01_other");
    CHECK(fake_blink_calls == 1);
}

void test_offline_removal_recovery(void)
{
    printf("T4 offline / removal while pending / recovery\n");
    reset_all();
    /* placement 1: capture ok while "offline" (coordinator retries, no acks) */
    snprintf(fake_next_id, sizeof(fake_next_id), "cam01_1");
    feed_present();
    static char id1[80];
    snprintf(id1, sizeof(id1), "%s", fake_next_id);
    /* paper removed while pending: re-arm but photo stays tracked */
    feed_absent(); feed_absent(); feed_absent();
    CHECK(fake_blink_calls == 0);                    /* no success signal */
    /* placement 2 while ack for photo 1 still not arrived */
    snprintf(fake_next_id, sizeof(fake_next_id), "cam01_2");
    feed_present();
    CHECK(fake_capture_calls == 2);
    static char id2[80];
    snprintf(id2, sizeof(id2), "%s", fake_next_id);
    /* acks arrive out of order */
    feed_ack(id2);
    CHECK(fake_blink_calls == 1);                    /* photo 2 confirmed */
    feed_ack(id1);
    CHECK(fake_blink_calls == 2);                    /* photo 1 confirmed independently */
    /* both consumed: further acks no-op */
    feed_ack(id1); feed_ack(id2);
    CHECK(fake_blink_calls == 2);
}


void test_consecutive_acks_and_no_persist(void)
{
    printf("T5 consecutive acks + zero persistent light write\n");
    reset_all();
    /* two placements */
    snprintf(fake_next_id, sizeof(fake_next_id), "cam01_100_001");
    feed_present();
    snprintf(fake_next_id, sizeof(fake_next_id), "cam01_100_002");
    feed_absent(); feed_absent(); feed_absent();   /* re-arm between */
    feed_present();
    CHECK(fake_capture_calls == 2);
    /* push BOTH acks back-to-back before the worker drains: neither may be
     * overwritten (A fix 1) */
    docsnap_service_notify_ack("cam01_100_001");
    docsnap_service_notify_ack("cam01_100_002");
    step();   /* worker drains ack 1 (handle_ack drains one per flag, re-sets flag) */
    step();   /* drains ack 2 */
    CHECK(fake_blink_calls == 2);
    /* zero persistent config writes across the whole flow (A fix 3) */
    CHECK(fake_set_config_calls == 0);
    CHECK(fake_set_brightness_calls >= 4);   /* duty + restore per blink */
    CHECK(fake_light_cfg.brightness_level == 70);  /* runtime state restored */
    /* duplicate recognized distinctly from unknown; neither blinks */
    feed_ack("cam01_100_001");
    CHECK(fake_blink_calls == 2);
    feed_ack("cam01_999_999");
    CHECK(fake_blink_calls == 2);
}

void test_empty_id_capture_rejected(void)
{
    printf("T6 empty image_id capture -> not tracked\n");
    reset_all();
    fake_capture_calls = 0;
    /* simulate a capture that returns OK but no id: id buffer starts empty */
    fake_next_id[0] = '\0';
    feed_present();
    CHECK(fake_capture_calls == 1);
    /* ack anything -> must NOT blink (nothing tracked) */
    feed_ack("cam01_whatever");
    CHECK(fake_blink_calls == 0);
    /* latch not opened: next valid detection retries */
    snprintf(fake_next_id, sizeof(fake_next_id), "cam01_101_001");
    feed_present();
    CHECK(fake_capture_calls == 2);
    feed_ack("cam01_101_001");
    CHECK(fake_blink_calls == 1);
}

void test_degraded_upload_gate(void)
{
    printf("T7 upload-path preconditions\n");
    reset_all();
    fake_up_mode = CAPTURE_MODE_LOCAL_ONLY;
    /* re-init to re-evaluate preconditions */
    docsnap_service_stop(); docsnap_service_init(NULL); docsnap_service_start();
    feed_present();
    CHECK(fake_capture_calls == 0);       /* gated */
    fake_up_mode = CAPTURE_MODE_BATCH;
    fake_rc = MQTT_REPORT_CONTENT_METADATA_ONLY;
    docsnap_service_stop(); docsnap_service_init(NULL); docsnap_service_start();
    feed_present();
    CHECK(fake_capture_calls == 0);       /* gated */
    fake_rc = MQTT_REPORT_CONTENT_FULL;
    docsnap_service_stop(); docsnap_service_init(NULL); docsnap_service_start();
    feed_present();
    CHECK(fake_capture_calls == 1);       /* healthy path fires */
}

void test_restart_seeding(void)
{
    printf("T8 reboot recovery: seed ack tracking from coordinator\n");
    reset_all();
    /* fake pending photos that survived a reboot; seeding now happens in
     * init() (A timing fix) so: deinit clears initialized, fakes set, then
     * a fresh init seeds */
    docsnap_service_deinit();
    fake_pending_n = 2;
    snprintf(fake_pending[0].id, sizeof(fake_pending[0].id), "cap_1");
    snprintf(fake_pending[0].trigger, sizeof(fake_pending[0].trigger), "ai_doc");
    snprintf(fake_pending[0].image_id, sizeof(fake_pending[0].image_id), "cam01_200_001");
    snprintf(fake_pending[1].id, sizeof(fake_pending[1].id), "cap_2");
    snprintf(fake_pending[1].trigger, sizeof(fake_pending[1].trigger), "pir");  /* non-docsnap */
    snprintf(fake_pending[1].image_id, sizeof(fake_pending[1].image_id), "cam01_200_002");
    CHECK(docsnap_service_init(NULL) == AICAM_OK);
    CHECK(docsnap_service_start() == AICAM_OK);
    /* seeded ai_doc ack blinks; pir photo ack does not */
    feed_ack("cam01_200_001");
    CHECK(fake_blink_calls == 1);
    feed_ack("cam01_200_002");
    CHECK(fake_blink_calls == 1);
    /* new placement still works alongside seeded photo */
    snprintf(fake_next_id, sizeof(fake_next_id), "cam01_201_001");
    feed_present();
    CHECK(fake_capture_calls == 1);
    feed_ack("cam01_201_001");
    CHECK(fake_blink_calls == 2);
}


void test_ack_dedupe_and_depth(void)
{
    printf("T9 ack dedupe + queue depth (A P1)\n");
    reset_all();
    snprintf(fake_next_id, sizeof(fake_next_id), "cam01_300_001");
    feed_present();
    feed_absent(); feed_absent(); feed_absent();
    snprintf(fake_next_id, sizeof(fake_next_id), "cam01_300_002");
    feed_present();
    CHECK(fake_capture_calls == 2);
    /* burst: dup of pending id + two distinct ids, all before worker runs */
    docsnap_service_notify_ack("cam01_300_001");
    docsnap_service_notify_ack("cam01_300_001");   /* duplicate collapses */
    docsnap_service_notify_ack("cam01_300_002");
    step(); step();
    CHECK(fake_blink_calls == 2);                  /* dedupe: no loss, no double */
    /* depth: several acks (dup+unknown mix) queued while worker idle */
    docsnap_service_notify_ack("cam01_300_001");
    docsnap_service_notify_ack("cam01_300_002");
    docsnap_service_notify_ack("cam01_400_003");
    docsnap_service_notify_ack("cam01_300_002");
    step(); step(); step();
    CHECK(fake_blink_calls == 2);                  /* all rejected/ignored, none blink */
}

void test_seed_filter_before_cap(void)
{
    printf("T11 recovery filter-first, cap-second (A P1)\n");
    reset_all();
    docsnap_service_deinit();
    /* newest 5 PENDING are NON-docsnap; the 4 ai_doc photos sit BEHIND them
     * - with the old cap-4-first logic they were invisible */
    fake_pending_n = 0;
    for (int i = 0; i < 5; i++) {
        snprintf(fake_pending[fake_pending_n].id, 64, "cap_n%d", i);
        snprintf(fake_pending[fake_pending_n].trigger, 16, "pir");
        snprintf(fake_pending[fake_pending_n].image_id, 64, "cam01_pir_%d", i);
        fake_pending_n++;
    }
    for (int i = 0; i < 4; i++) {
        snprintf(fake_pending[fake_pending_n].id, 64, "cap_d%d", i);
        snprintf(fake_pending[fake_pending_n].trigger, 16, "ai_doc");
        snprintf(fake_pending[fake_pending_n].image_id, 64, "cam01_500_%03d", i + 1);
        fake_pending_n++;
    }
    CHECK(docsnap_service_init(NULL) == AICAM_OK);
    CHECK(docsnap_service_start() == AICAM_OK);
    /* all 4 docsnap photos recovered despite 5 newer pir photos */
    feed_ack("cam01_500_001"); CHECK(fake_blink_calls == 1);
    feed_ack("cam01_500_002"); CHECK(fake_blink_calls == 2);
    feed_ack("cam01_500_003"); CHECK(fake_blink_calls == 3);
    feed_ack("cam01_500_004"); CHECK(fake_blink_calls == 4);
}

void test_ack_priority_on_full_queue(void)
{
    printf("T12 full queue: tracked ack admitted over untracked (A P1)\n");
    reset_all();
    snprintf(fake_next_id, sizeof(fake_next_id), "cam01_600_001");
    feed_present();                       /* one tracked photo in flight */
    /* stuff the mailbox with 8 untracked acks (worker idle) */
    for (int i = 0; i < 8; i++) {
        char id[80];
        snprintf(id, sizeof(id), "cam01_noise_%d", i);
        docsnap_service_notify_ack(id);
    }
    /* now the VALID ack arrives - must not be dropped */
    docsnap_service_notify_ack("cam01_600_001");
    step();   /* drains all: noise rejected, valid blinks */
    CHECK(fake_blink_calls == 1);
}

int main(void)
{
    test_seed_filter_before_cap();
    test_ack_priority_on_full_queue();
    test_ack_dedupe_and_depth();

    test_consecutive_acks_and_no_persist();
    test_empty_id_capture_rejected();
    test_degraded_upload_gate();
    test_restart_seeding();

    test_normal_flow();
    test_capture_failure();
    test_ack_rejections();
    test_offline_removal_recovery();
    printf(failures ? "\nRESULT: FAIL (%d)\n" : "\nRESULT: ALL PASS\n", failures);
    return failures ? 1 : 0;
}
