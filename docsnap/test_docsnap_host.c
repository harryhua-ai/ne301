/* Host simulation tests for the REAL docsnap_service.c
 * Covers A-required behavior classes: normal, failure, duplicate ack,
 * offline/recovery.  Single-threaded cooperative execution via setjmp. */
#include <stdio.h>
#include <setjmp.h>
#include <string.h>
#include <stdbool.h>
#include "docsnap_service.h"
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
aicam_result_t device_service_light_get_config(light_config_t *c){ *c = fake_light_cfg; return AICAM_OK; }
aicam_result_t device_service_light_set_config(const light_config_t *c){ fake_light_cfg = *c; fake_light_restore_calls++; return AICAM_OK; }
aicam_result_t device_service_light_set_brightness(uint32_t d){ fake_last_duty = d; return AICAM_OK; }
aicam_result_t device_service_light_blink(uint32_t t, uint32_t ms){ (void)t;(void)ms;
    if (fake_blink_fail) return AICAM_ERROR; fake_blink_calls++; return AICAM_OK; }
static uint8_t fake_qos = 1;
aicam_result_t json_config_get_mqtt_service_config(mqtt_service_config_t *c){ c->data_receive_qos=1; c->data_report_qos=fake_qos; return AICAM_OK; }

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
    fake_light_restore_calls = 0; memset(&fake_light_cfg, 0, sizeof(fake_light_cfg));
    fake_light_cfg.brightness_level = 70;   /* pre-existing fill-light state */
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
    CHECK(fake_last_duty == DOCSNAP_LED_DUTY_PERCENT);
    CHECK(fake_light_restore_calls >= 1);            /* light state restored */
    CHECK(fake_light_cfg.brightness_level == 70);    /* original brightness back */
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

int main(void)
{
    test_normal_flow();
    test_capture_failure();
    test_ack_rejections();
    test_offline_removal_recovery();
    printf(failures ? "\nRESULT: FAIL (%d)\n" : "\nRESULT: ALL PASS\n", failures);
    return failures ? 1 : 0;
}
