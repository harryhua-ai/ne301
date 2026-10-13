#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "app_report_service.h"

static int g_total;
static int g_fail;

#define CHECK(cond) \
    do { \
        g_total++; \
        if (!(cond)) { \
            g_fail++; \
            printf("FAIL app_report:%d %s\n", g_total, #cond); \
        } \
    } while (0)

#define BOOT_A 0x5A5A0001u
#define BOOT_B 0x17170002u
#define DEV_ID "aa:bb:cc:dd:ee:66"
#define PLACEHOLDER "ne301-app:line-crossing"

static const uint8_t *span_find(const uint8_t *hay, uint32_t hay_len,
                                const char *needle)
{
    size_t n = strlen(needle);

    if (n == 0u || hay_len < (uint32_t)n) {
        return NULL;
    }
    for (uint32_t i = 0u; i <= hay_len - (uint32_t)n; i++) {
        if (memcmp(hay + i, needle, n) == 0) {
            return hay + i;
        }
    }
    return NULL;
}

typedef struct {
    aicam_bool_t mqtt_ready;
    aicam_bool_t webhook_ready;
    int mqtt_script[24];
    int mqtt_script_len;
    int mqtt_script_pos;
    int webhook_script[24];
    int webhook_script_len;
    int webhook_script_pos;
    uint8_t mqtt_last[APP_REPORT_SLOT_CAP];
    uint32_t mqtt_last_len;
    uint32_t mqtt_calls;
    uint8_t webhook_last[APP_REPORT_SLOT_CAP];
    uint32_t webhook_last_len;
    uint32_t webhook_calls;
} fake_transport_t;

static fake_transport_t g_ft;

static int ft_mqtt_publish(void *user, const uint8_t *json, uint32_t len)
{
    (void)user;
    g_ft.mqtt_calls++;
    if ((size_t)len <= sizeof(g_ft.mqtt_last)) {
        memcpy(g_ft.mqtt_last, json, len);
        g_ft.mqtt_last_len = len;
    }
    if (g_ft.mqtt_script_pos < g_ft.mqtt_script_len) {
        return g_ft.mqtt_script[g_ft.mqtt_script_pos++];
    }
    return APP_REPORT_TRANSPORT_OK;
}

static int ft_webhook_post(void *user, const uint8_t *json, uint32_t len)
{
    (void)user;
    g_ft.webhook_calls++;
    if ((size_t)len <= sizeof(g_ft.webhook_last)) {
        memcpy(g_ft.webhook_last, json, len);
        g_ft.webhook_last_len = len;
    }
    if (g_ft.webhook_script_pos < g_ft.webhook_script_len) {
        return g_ft.webhook_script[g_ft.webhook_script_pos++];
    }
    return APP_REPORT_TRANSPORT_OK;
}

static aicam_bool_t ft_mqtt_ready(void *user)
{
    (void)user;
    return g_ft.mqtt_ready;
}

static aicam_bool_t ft_webhook_ready(void *user)
{
    (void)user;
    return g_ft.webhook_ready;
}

static void ft_reset(void)
{
    memset(&g_ft, 0, sizeof(g_ft));
    g_ft.mqtt_ready = AICAM_TRUE;
    g_ft.webhook_ready = AICAM_TRUE;
}

static const app_report_transport_t g_transport = {
    NULL,
    ft_mqtt_ready,
    ft_mqtt_publish,
    ft_webhook_ready,
    ft_webhook_post
};

static aicam_bool_t ident_device_id(void *user, char *out, uint32_t cap)
{
    (void)user;
    if (out == NULL || cap < (uint32_t)sizeof(DEV_ID)) {
        return AICAM_FALSE;
    }
    memcpy(out, DEV_ID, sizeof(DEV_ID));
    return AICAM_TRUE;
}

static aicam_bool_t ident_device_id_fail(void *user, char *out, uint32_t cap)
{
    (void)user;
    (void)out;
    (void)cap;
    return AICAM_FALSE;
}

static uint32_t g_boot_source = BOOT_A;

static uint32_t ident_boot_id(void *user)
{
    (void)user;
    return g_boot_source;
}

static const app_report_identity_t g_identity = {
    NULL,
    ident_device_id,
    ident_boot_id
};

static app_report_session_t g_session;

static void session_fill(app_report_session_t *s, const char *app,
                         const uint8_t key, uint32_t report_max,
                         aicam_bool_t active)
{
    memset(s, 0, sizeof(*s));
    strncpy(s->publisher_id, "ne301-exp-pub", APP_REPORT_PUBLISHER_ID_SLOT);
    strncpy(s->app_id, app, APP_REPORT_APP_ID_SLOT);
    memset(s->publisher_key_sha256, key, APP_REPORT_SHA256_LEN);
    s->report_max = report_max;
    s->active = active;
}

static void reset_all(void)
{
    ft_reset();
    g_boot_source = BOOT_A;
    app_report_service_init(&g_identity, &g_transport);
    session_fill(&g_session, "line-crossing", 0xA5, APP_REPORT_INPUT_MAX,
                 AICAM_TRUE);
    if (app_report_service_set_session(&g_session) != APP_REPORT_ABI_OK) {
        printf("FATAL: session bind failed\n");
        exit(1);
    }
}

static size_t build_report(char *buf, size_t cap, uint32_t seq,
                           const char *device_id, const char *boot_id_text,
                           const char *pad, int with_device)
{
    size_t n = 0;

    n += (size_t)snprintf(buf + n, cap - n,
                          "{\"schema_version\":1,\"type\":\"line_counting\",");
    if (with_device != 0) {
        n += (size_t)snprintf(buf + n, cap - n, "\"device_id\":\"%s\",",
                              device_id);
    }
    n += (size_t)snprintf(
        buf + n, cap - n,
        "\"app\":{\"app_id\":\"line-crossing\",\"sw_version\":\"1.0.0\"},"
        "\"boot_id\":%s,\"report_seq\":%u,"
        "\"clock_valid\":false,\"reported_at\":null,"
        "\"window\":{\"start_ms\":0,\"end_ms\":1000,\"in\":1,\"out\":0},"
        "\"total\":{\"in\":5,\"out\":3},"
        "\"counter\":{\"counter_name\":\"%s\"},"
        "\"target\":{\"class_name\":\"person\"},"
        "\"model\":{\"name\":\"yolo26n\",\"version\":\"1.0\"}}",
        boot_id_text, (unsigned int)seq, pad);
    return n;
}

static const char *g_report_tail =
    ",\"app\":{\"app_id\":\"line-crossing\",\"sw_version\":\"1.0.0\"},"
    "\"boot_id\":%s,\"report_seq\":%u,"
    "\"clock_valid\":false,\"reported_at\":null,"
    "\"window\":{\"start_ms\":0,\"end_ms\":1000,\"in\":1,\"out\":0},"
    "\"total\":{\"in\":5,\"out\":3},"
    "\"counter\":{\"counter_name\":\"d\"},"
    "\"target\":{\"class_name\":\"person\"},"
    "\"model\":{\"name\":\"yolo26n\",\"version\":\"1.0\"}}";

static void test_session_gates(void)
{
    app_report_session_t inactive;
    app_report_session_t other;
    uint32_t boot = 0;
    char rbuf[512];
    size_t rlen;

    reset_all();
    CHECK(app_report_service_boot_id() == BOOT_A);
    CHECK(app_report_service_queue_count() == 0u);

    rlen = build_report(rbuf, sizeof(rbuf), 1u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(NULL, rbuf, (uint32_t)rlen, 1u, &boot) ==
          APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(boot == 0u);

    CHECK(app_report_service_set_session(NULL) == APP_REPORT_ABI_OK);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 1u, &boot) ==
          APP_REPORT_ABI_UNAUTHORIZED);

    session_fill(&inactive, "line-crossing", 0xA5, APP_REPORT_INPUT_MAX,
                 AICAM_FALSE);
    CHECK(app_report_service_set_session(&inactive) ==
          APP_REPORT_ABI_INVALID_ARGUMENT);
    CHECK(app_report_submit(&inactive, rbuf, (uint32_t)rlen, 1u, &boot) ==
          APP_REPORT_ABI_UNAUTHORIZED);

    session_fill(&other, "other-app", 0xA5, APP_REPORT_INPUT_MAX, AICAM_TRUE);
    CHECK(app_report_service_set_session(&other) == APP_REPORT_ABI_OK);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 1u, &boot) ==
          APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(app_report_service_set_session(&g_session) == APP_REPORT_ABI_OK);
    CHECK(app_report_submit(&other, rbuf, (uint32_t)rlen, 1u, &boot) ==
          APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(app_report_service_queue_count() == 0u);

    {
        app_report_session_t zero_quota;
        session_fill(&zero_quota, "line-crossing", 0xA5, 0u, AICAM_TRUE);
        CHECK(app_report_service_set_session(&zero_quota) ==
              APP_REPORT_ABI_INVALID_ARGUMENT);
    }
}

static void test_param_rejections(void)
{
    uint32_t boot = 0;
    char rbuf[512];
    size_t rlen;
    uint32_t mstate;
    uint32_t wstate;
    char nul_payload[256];

    reset_all();
    rlen = build_report(rbuf, sizeof(rbuf), 1u, PLACEHOLDER, "0", "d", 1);

    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 1u, NULL) ==
          APP_REPORT_ABI_INVALID_ARGUMENT);
    CHECK(app_report_submit(&g_session, NULL, (uint32_t)rlen, 1u, &boot) ==
          APP_REPORT_ABI_INVALID_ARGUMENT);
    CHECK(app_report_submit(&g_session, rbuf, 0u, 1u, &boot) ==
          APP_REPORT_ABI_INVALID_ARGUMENT);

    memset(nul_payload, 'x', sizeof(nul_payload));
    nul_payload[64] = '\0';
    CHECK(app_report_submit(&g_session, nul_payload, sizeof(nul_payload), 1u,
                            &boot) == APP_REPORT_ABI_INVALID_ARGUMENT);

    CHECK(app_report_status(BOOT_A, 1u, NULL, &wstate) ==
          APP_REPORT_ABI_INVALID_ARGUMENT);
    CHECK(app_report_status(BOOT_A, 1u, &mstate, NULL) ==
          APP_REPORT_ABI_INVALID_ARGUMENT);
    CHECK(app_report_service_queue_count() == 0u);
}

static void test_quota(void)
{
    app_report_session_t small;
    uint32_t boot = 0;
    char rbuf[512];
    size_t rlen;

    reset_all();
    rlen = build_report(rbuf, sizeof(rbuf), 1u, PLACEHOLDER, "0", "d", 1);

    session_fill(&small, "line-crossing", 0xA5, 100u, AICAM_TRUE);
    CHECK(app_report_service_set_session(&small) == APP_REPORT_ABI_OK);
    CHECK(app_report_submit(&small, rbuf, (uint32_t)rlen, 1u, &boot) ==
          APP_REPORT_ABI_QUOTA_EXCEEDED);
    CHECK(boot == 0u);
    CHECK(app_report_service_queue_count() == 0u);

    reset_all();
    {
        static char over[APP_REPORT_INPUT_MAX + 8u];
        size_t head;
        size_t target = (size_t)APP_REPORT_INPUT_MAX + 1u;

        head = (size_t)snprintf(
            over, sizeof(over),
            "{\"schema_version\":1,\"type\":\"line_counting\","
            "\"device_id\":\"%s\",\"app\":{\"app_id\":\"line-crossing\"},"
            "\"boot_id\":0,\"report_seq\":9,\"window\":{},\"total\":{},"
            "\"counter\":{\"counter_name\":\"", PLACEHOLDER);
        for (size_t i = head; i < target - 2u; i++) {
            over[i] = 'p';
        }
        over[target - 2u] = '"';
        over[target - 1u] = '}';
        over[target] = '\0';
        CHECK(strlen(over) == target);
        CHECK(app_report_submit(&g_session, over, (uint32_t)strlen(over), 9u,
                                &boot) == APP_REPORT_ABI_QUOTA_EXCEEDED);
        CHECK(app_report_service_queue_count() == 0u);
    }
}

static void test_json_validity(void)
{
    uint32_t boot = 0;

    reset_all();
    {
        const char *bad[] = {
            "not json",
            "{",
            "[]",
            "{\"a\":1}}",
            "{\"schema_version\":1,\"type\":\"line_counting\",\"report_seq\":1} x",
            "{\"schema_version\":1,\"type\":\"line_count\ting\",\"report_seq\":1}",
            "{\"schema_version\":1,\"type\":\"line_counting\\x\",\"report_seq\":1}",
            "{\"schema_version\":01,\"type\":\"line_counting\",\"report_seq\":1}",
            "{\"schema_version\":1,,\"type\":\"line_counting\",\"report_seq\":1}",
            "{\"schema_version\":1,\"type\":\"line_counting\",\"report_seq\":1,}",
            "{\"schema_version\":1,'type':'line_counting',\"report_seq\":1}",
            "{\"schema_version\":1,\"type\":\"line_counting\",\"report_seq\":+1}",
            "{\"schema_version\":1,\"type\":\"line_counting\",\"report_seq\":1 \"x\":2}",
            "{\"schema_version\":1,\"type\":\"line_counting\",\"report_seq\":1e2}",
            ("{\"schema_version\":1,\"type\":\"line_counting\",\"report_seq\":1,"
             "\"window\":{}}extra")};
        for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
            CHECK(app_report_submit(&g_session, bad[i], (uint32_t)strlen(bad[i]),
                                    1u, &boot) == APP_REPORT_ABI_INVALID_ARGUMENT);
        }        CHECK(app_report_service_queue_count() == 0u);
    }
}

static void test_schema_rejections(void)
{
    uint32_t boot = 0;

    reset_all();
    {
        const char *schema2 =
            "{\"schema_version\":2,\"type\":\"line_counting\",\"device_id\":\"x\","
            "\"boot_id\":0,\"report_seq\":1,\"window\":{},\"total\":{},"
            "\"counter\":{},\"target\":{},\"model\":{}}";
        CHECK(app_report_submit(&g_session, schema2, (uint32_t)strlen(schema2),
                                1u, &boot) == APP_REPORT_ABI_INCOMPATIBLE);
    }
    {
        const char *wrong_type =
            "{\"schema_version\":1,\"type\":\"face_count\",\"device_id\":\"x\","
            "\"boot_id\":0,\"report_seq\":1,\"window\":{},\"total\":{},"
            "\"counter\":{},\"target\":{},\"model\":{}}";
        CHECK(app_report_submit(&g_session, wrong_type, (uint32_t)strlen(wrong_type),
                                1u, &boot) == APP_REPORT_ABI_INCOMPATIBLE);
    }
    {
        const char *missing[] = {
            "{\"type\":\"line_counting\",\"device_id\":\"x\",\"boot_id\":0,"
            "\"report_seq\":1,\"window\":{},\"total\":{},\"counter\":{},"
            "\"target\":{},\"model\":{}}",
            "{\"schema_version\":1,\"device_id\":\"x\",\"boot_id\":0,"
            "\"report_seq\":1,\"window\":{},\"total\":{},\"counter\":{},"
            "\"target\":{},\"model\":{}}",
            "{\"schema_version\":1,\"type\":\"line_counting\",\"device_id\":\"x\","
            "\"boot_id\":0,\"window\":{},\"total\":{},\"counter\":{},"
            "\"target\":{},\"model\":{}}",
            "{\"schema_version\":1,\"type\":\"line_counting\",\"device_id\":\"x\","
            "\"boot_id\":0,\"report_seq\":1,\"total\":{},\"counter\":{},"
            "\"target\":{},\"model\":{}}",
            "{\"schema_version\":1,\"type\":\"line_counting\",\"device_id\":\"x\","
            "\"boot_id\":0,\"report_seq\":1,\"window\":[],\"total\":{},"
            "\"counter\":{},\"target\":{},\"model\":{}}",
            "{\"schema_version\":true,\"type\":\"line_counting\",\"device_id\":\"x\","
            "\"boot_id\":0,\"report_seq\":1,\"window\":{},\"total\":{},"
            "\"counter\":{},\"target\":{},\"model\":{}}"
        };
        for (size_t i = 0; i < sizeof(missing) / sizeof(missing[0]); i++) {
            CHECK(app_report_submit(&g_session, missing[i],
                                    (uint32_t)strlen(missing[i]), 1u, &boot) ==
                  APP_REPORT_ABI_INVALID_ARGUMENT);
        }
    }
    {
        const char *bad_fields[] = {
            "{\"schema_version\":1,\"type\":\"line_counting\",\"device_id\":\"x\","
            "\"boot_id\":0,\"report_seq\":5,\"window\":{},\"total\":{},"
            "\"counter\":{},\"target\":{},\"model\":{}}",
            "{\"schema_version\":1,\"type\":\"line_counting\",\"device_id\":7,"
            "\"boot_id\":0,\"report_seq\":1,\"window\":{},\"total\":{},"
            "\"counter\":{},\"target\":{},\"model\":{}}",
            "{\"schema_version\":1,\"type\":\"line_counting\",\"device_id\":\"x\","
            "\"boot_id\":\"zero\",\"report_seq\":1,\"window\":{},\"total\":{},"
            "\"counter\":{},\"target\":{},\"model\":{}}",
            "{\"schema_version\":1,\"type\":\"line_counting\",\"device_id\":\"x\","
            "\"boot_id\":-4,\"report_seq\":1,\"window\":{},\"total\":{},"
            "\"counter\":{},\"target\":{},\"model\":{}}",
            "{\"schema_version\":1,\"type\":\"line_counting\",\"device_id\":\"x\","
            "\"boot_id\":0,\"report_seq\":1.5,\"window\":{},\"total\":{},"
            "\"counter\":{},\"target\":{},\"model\":{}}",
            "{\"schema_version\":1,\"type\":5,\"device_id\":\"x\","
            "\"boot_id\":0,\"report_seq\":1,\"window\":{},\"total\":{},"
            "\"counter\":{},\"target\":{},\"model\":{}}"
        };
        for (size_t i = 0; i < sizeof(bad_fields) / sizeof(bad_fields[0]); i++) {
            CHECK(app_report_submit(&g_session, bad_fields[i],
                                    (uint32_t)strlen(bad_fields[i]), 1u, &boot) ==
                  APP_REPORT_ABI_INVALID_ARGUMENT);
        }
    }
    {
        const char *dup[] = {
            "{\"schema_version\":1,\"type\":\"line_counting\",\"device_id\":\"a\","
            "\"device_id\":\"b\",\"boot_id\":0,\"report_seq\":1,\"window\":{},"
            "\"total\":{},\"counter\":{},\"target\":{},\"model\":{}}",
            "{\"schema_version\":1,\"type\":\"line_counting\",\"device_id\":\"a\","
            "\"boot_id\":0,\"boot_id\":0,\"report_seq\":1,\"window\":{},"
            "\"total\":{},\"counter\":{},\"target\":{},\"model\":{}}",
            "{\"schema_version\":1,\"type\":\"line_counting\",\"device_id\":\"a\","
            "\"boot_id\":0,\"report_seq\":1,\"report_seq\":1,\"window\":{},"
            "\"total\":{},\"counter\":{},\"target\":{},\"model\":{}}",
            "{\"schema_version\":1,\"type\":\"line_counting\",\"zz\":1,\"zz\":2,"
            "\"device_id\":\"a\",\"boot_id\":0,\"report_seq\":1,\"window\":{},"
            "\"total\":{},\"counter\":{},\"target\":{},\"model\":{}}"
        };
        for (size_t i = 0; i < sizeof(dup) / sizeof(dup[0]); i++) {
            CHECK(app_report_submit(&g_session, dup[i], (uint32_t)strlen(dup[i]),
                                    1u, &boot) ==
                  APP_REPORT_ABI_INVALID_ARGUMENT);
        }
    }
    CHECK(app_report_service_queue_count() == 0u);
}

static void test_normalization_gaps_dedup(void)
{
    uint32_t boot = 0;
    char rbuf[1024];
    char tail[512];
    size_t rlen;
    app_report_entry_info_t info;
    const uint8_t *found;
    const uint8_t *found_tail;

    reset_all();
    rlen = build_report(rbuf, sizeof(rbuf), 1u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 1u, &boot) ==
          APP_REPORT_ABI_OK);
    CHECK(boot == BOOT_A);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(app_report_service_entry(0u, &info) == AICAM_TRUE);
    CHECK(info.report_seq == 1u);
    CHECK(info.host_boot_id == BOOT_A);
    CHECK(info.mqtt_state == APP_REPORT_MQTT_PENDING);
    CHECK(info.webhook_state == APP_REPORT_WEBHOOK_PENDING);
    CHECK(app_report_service_entry(APP_REPORT_QUEUE_SLOTS, &info) == AICAM_FALSE);

    app_report_service_pump();
    CHECK(g_ft.mqtt_calls == 1u);
    CHECK(g_ft.webhook_calls == 1u);
    CHECK(g_ft.mqtt_last_len > 0u);
    CHECK(g_ft.mqtt_last_len == g_ft.webhook_last_len);
    CHECK(memcmp(g_ft.mqtt_last, g_ft.webhook_last, g_ft.mqtt_last_len) == 0);
    CHECK(span_find(g_ft.mqtt_last, g_ft.mqtt_last_len, DEV_ID) != NULL);
    CHECK(span_find(g_ft.mqtt_last, g_ft.mqtt_last_len, PLACEHOLDER) == NULL);
    {
        char boot_text[24];
        (void)snprintf(boot_text, sizeof(boot_text), "\"boot_id\":%u",
                       (unsigned int)BOOT_A);
        CHECK(span_find(g_ft.mqtt_last, g_ft.mqtt_last_len, boot_text) != NULL);
    }
    {
        char seq_text[24];
        (void)snprintf(seq_text, sizeof(seq_text), "\"report_seq\":1,");
        CHECK(span_find(g_ft.mqtt_last, g_ft.mqtt_last_len, seq_text) != NULL);
    }
    {
        snprintf(tail, sizeof(tail), g_report_tail, "0", 1u);
        found_tail = (const uint8_t *)strstr(tail, ",\"report_seq\"");
        CHECK(found_tail != NULL);
        found = (const uint8_t *)span_find(g_ft.mqtt_last, g_ft.mqtt_last_len,
                                           (const char *)found_tail);
        CHECK(found != NULL);
        CHECK((uint32_t)((uintptr_t)found - (uintptr_t)g_ft.mqtt_last) +
                  (uint32_t)strlen((const char *)found_tail) ==
              g_ft.mqtt_last_len);
    }

    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 1u, &boot) ==
          APP_REPORT_ABI_OK);
    CHECK(boot == BOOT_A);
    CHECK(app_report_service_queue_count() == 1u);

    rlen = build_report(rbuf, sizeof(rbuf), 3u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 3u, &boot) ==
          APP_REPORT_ABI_OK);
    CHECK(app_report_service_queue_count() == 2u);
    app_report_service_pump();
    CHECK(g_ft.mqtt_calls == 2u);
    CHECK(g_ft.webhook_calls == 2u);

    {
        uint32_t ms;
        uint32_t ws;
        CHECK(app_report_status(BOOT_A, 1u, &ms, &ws) == APP_REPORT_ABI_OK);
        CHECK(ms == APP_REPORT_MQTT_TRANSPORT_SUCCEEDED);
        CHECK(ws == APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED);
        CHECK(app_report_status(BOOT_A, 3u, &ms, &ws) == APP_REPORT_ABI_OK);
        CHECK(ms == APP_REPORT_MQTT_TRANSPORT_SUCCEEDED);
        CHECK(ws == APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED);
        CHECK(app_report_status(BOOT_A, 2u, &ms, &ws) == APP_REPORT_ABI_NOT_FOUND);
        CHECK(app_report_status(BOOT_A, 999u, &ms, &ws) == APP_REPORT_ABI_NOT_FOUND);
    }
    app_report_service_get_stats(NULL);
}

static void test_insertion_when_identity_absent(void)
{
    uint32_t boot = 0;
    char rbuf[1024];
    size_t rlen;
    const char *needle;

    reset_all();
    rlen = build_report(rbuf, sizeof(rbuf), 4u, "x", "0", "d", 0);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 4u, &boot) ==
          APP_REPORT_ABI_OK);
    app_report_service_pump();
    CHECK(g_ft.mqtt_calls == 1u);
    needle = "\"type\":\"line_counting\",\"device_id\":\"" DEV_ID "\",";
    CHECK(span_find(g_ft.mqtt_last, g_ft.mqtt_last_len, needle) != NULL);
    needle = ",\"boot_id\":";
    CHECK(span_find(g_ft.mqtt_last, g_ft.mqtt_last_len, needle) != NULL);
}

static void test_forged_device_id(void)
{
    uint32_t boot = 0;
    char rbuf[1024];
    size_t rlen;

    reset_all();
    rlen = build_report(rbuf, sizeof(rbuf), 5u, "de:ad:be:ef:00:11", "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 5u, &boot) ==
          APP_REPORT_ABI_OK);
    app_report_service_pump();
    CHECK(span_find(g_ft.mqtt_last, g_ft.mqtt_last_len, DEV_ID) != NULL);
    CHECK(span_find(g_ft.mqtt_last, g_ft.mqtt_last_len, "de:ad:be:ef:00:11") ==
          NULL);
}

static void test_identity_fail_closed(void)
{
    app_report_session_t s;
    uint32_t boot = 0;
    char rbuf[512];
    size_t rlen;
    const app_report_identity_t broken = { NULL, ident_device_id_fail,
                                           ident_boot_id };

    session_fill(&s, "line-crossing", 0xA5, APP_REPORT_INPUT_MAX, AICAM_TRUE);
    ft_reset();
    app_report_service_init(&broken, &g_transport);
    CHECK(app_report_service_boot_id() == BOOT_A);
    CHECK(app_report_service_set_session(&s) == APP_REPORT_ABI_OK);
    rlen = build_report(rbuf, sizeof(rbuf), 1u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&s, rbuf, (uint32_t)rlen, 1u, &boot) ==
          APP_REPORT_ABI_STORAGE_UNKNOWN);
    CHECK(app_report_service_queue_count() == 0u);

    g_boot_source = 0u;
    app_report_service_init(&g_identity, &g_transport);
    CHECK(app_report_service_boot_id() == 0u);
    CHECK(app_report_service_set_session(&s) == APP_REPORT_ABI_OK);
    CHECK(app_report_submit(&s, rbuf, (uint32_t)rlen, 1u, &boot) ==
          APP_REPORT_ABI_STORAGE_UNKNOWN);
    CHECK(app_report_service_queue_count() == 0u);
}

static void test_channel_independence(void)
{
    uint32_t boot = 0;
    char rbuf[512];
    size_t rlen;
    uint32_t ms;
    uint32_t ws;

    reset_all();
    for (int i = 0; i < 24; i++) {
        g_ft.mqtt_script[i] = APP_REPORT_TRANSPORT_FAIL;
    }
    g_ft.mqtt_script_len = 24;
    rlen = build_report(rbuf, sizeof(rbuf), 10u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 10u, &boot) ==
          APP_REPORT_ABI_OK);
    app_report_service_pump();
    CHECK(app_report_status(BOOT_A, 10u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_PENDING);
    CHECK(ws == APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED);
    CHECK(g_ft.mqtt_calls == 1u);
    CHECK(g_ft.webhook_calls == 1u);
    app_report_service_pump();
    app_report_service_pump();
    CHECK(app_report_status(BOOT_A, 10u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_TRANSPORT_FAILED);
    CHECK(g_ft.mqtt_calls == 3u);
    CHECK(g_ft.webhook_calls == 1u);
    CHECK(app_report_service_queue_count() == 1u);

    reset_all();
    g_ft.mqtt_script[0] = APP_REPORT_TRANSPORT_FAIL;
    g_ft.mqtt_script[1] = APP_REPORT_TRANSPORT_FAIL;
    g_ft.mqtt_script[2] = APP_REPORT_TRANSPORT_OK;
    g_ft.mqtt_script_len = 3;
    rlen = build_report(rbuf, sizeof(rbuf), 11u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 11u, &boot) ==
          APP_REPORT_ABI_OK);
    app_report_service_pump();
    app_report_service_pump();
    app_report_service_pump();
    CHECK(app_report_status(BOOT_A, 11u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_TRANSPORT_SUCCEEDED);
    CHECK(g_ft.mqtt_calls == 3u);

    reset_all();
    g_ft.mqtt_script[0] = APP_REPORT_TRANSPORT_UNKNOWN;
    g_ft.mqtt_script_len = 1;
    rlen = build_report(rbuf, sizeof(rbuf), 12u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 12u, &boot) ==
          APP_REPORT_ABI_OK);
    app_report_service_pump();
    CHECK(app_report_status(BOOT_A, 12u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_UNKNOWN);
    CHECK(ws == APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED);
    app_report_service_pump();
    CHECK(g_ft.mqtt_calls == 1u);
    CHECK(app_report_status(BOOT_A, 12u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_UNKNOWN);

    reset_all();
    for (int i = 0; i < 24; i++) {
        g_ft.webhook_script[i] = APP_REPORT_TRANSPORT_FAIL;
    }
    g_ft.webhook_script_len = 24;
    rlen = build_report(rbuf, sizeof(rbuf), 13u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 13u, &boot) ==
          APP_REPORT_ABI_OK);
    app_report_service_pump();
    app_report_service_pump();
    app_report_service_pump();
    CHECK(app_report_status(BOOT_A, 13u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_TRANSPORT_SUCCEEDED);
    CHECK(ws == APP_REPORT_WEBHOOK_TRANSPORT_FAILED);
}

static void test_channel_disabled(void)
{
    uint32_t boot = 0;
    char rbuf[512];
    size_t rlen;
    uint32_t ms;
    uint32_t ws;

    reset_all();
    g_ft.mqtt_ready = AICAM_FALSE;
    g_ft.webhook_ready = AICAM_FALSE;
    rlen = build_report(rbuf, sizeof(rbuf), 20u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 20u, &boot) ==
          APP_REPORT_ABI_OK);
    CHECK(app_report_status(BOOT_A, 20u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_PENDING);
    CHECK(ws == APP_REPORT_WEBHOOK_PENDING);
    app_report_service_pump();
    CHECK(app_report_status(BOOT_A, 20u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_NOT_CONFIGURED);
    CHECK(ws == APP_REPORT_WEBHOOK_NOT_CONFIGURED);
    CHECK(g_ft.mqtt_calls == 0u);
    CHECK(g_ft.webhook_calls == 0u);
    CHECK(app_report_service_queue_count() == 1u);

    reset_all();
    g_ft.webhook_ready = AICAM_FALSE;
    rlen = build_report(rbuf, sizeof(rbuf), 21u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 21u, &boot) ==
          APP_REPORT_ABI_OK);
    app_report_service_pump();
    CHECK(app_report_status(BOOT_A, 21u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_TRANSPORT_SUCCEEDED);
    CHECK(ws == APP_REPORT_WEBHOOK_NOT_CONFIGURED);

    reset_all();
    g_ft.mqtt_ready = AICAM_FALSE;
    rlen = build_report(rbuf, sizeof(rbuf), 22u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 22u, &boot) ==
          APP_REPORT_ABI_OK);
    app_report_service_pump();
    CHECK(app_report_status(BOOT_A, 22u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_NOT_CONFIGURED);
    CHECK(ws == APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED);
}

static void test_queue_bounds(void)
{
    uint32_t boot = 0;
    char rbuf[512];
    size_t rlen;

    reset_all();
    for (int i = 0; i < 24; i++) {
        g_ft.mqtt_script[i] = APP_REPORT_TRANSPORT_FAIL;
        g_ft.webhook_script[i] = APP_REPORT_TRANSPORT_FAIL;
    }
    g_ft.mqtt_script_len = 24;
    g_ft.webhook_script_len = 24;
    for (uint32_t i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
        rlen = build_report(rbuf, sizeof(rbuf), 30u + i, PLACEHOLDER, "0", "d", 1);
        CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 30u + i,
                                &boot) == APP_REPORT_ABI_OK);
    }
    rlen = build_report(rbuf, sizeof(rbuf), 99u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_service_queue_count() == APP_REPORT_QUEUE_SLOTS);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 99u, &boot) ==
          APP_REPORT_ABI_BUSY);
    CHECK(app_report_service_queue_count() == APP_REPORT_QUEUE_SLOTS);
    app_report_service_pump();
    app_report_service_pump();
    app_report_service_pump();
    for (uint32_t i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
        uint32_t ms;
        uint32_t ws;
        CHECK(app_report_status(BOOT_A, 30u + i, &ms, &ws) == APP_REPORT_ABI_OK);
        CHECK(ms == APP_REPORT_MQTT_TRANSPORT_FAILED);
        CHECK(ws == APP_REPORT_WEBHOOK_TRANSPORT_FAILED);
    }
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 99u, &boot) ==
          APP_REPORT_ABI_OK);
    CHECK(app_report_service_queue_count() == APP_REPORT_QUEUE_SLOTS);
    {
        uint32_t ms;
        uint32_t ws;
        CHECK(app_report_status(BOOT_A, 30u, &ms, &ws) == APP_REPORT_ABI_NOT_FOUND);
        CHECK(app_report_status(BOOT_A, 31u, &ms, &ws) == APP_REPORT_ABI_OK);
    }
}

static void test_restart_and_key_change(void)
{
    app_report_session_t rotated;
    uint32_t boot = 0;
    char rbuf[512];
    size_t rlen;
    uint32_t ms;
    uint32_t ws;

    reset_all();
    rlen = build_report(rbuf, sizeof(rbuf), 40u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 40u, &boot) ==
          APP_REPORT_ABI_OK);
    CHECK(boot == BOOT_A);

    g_boot_source = BOOT_B;
    app_report_service_init(&g_identity, &g_transport);
    session_fill(&g_session, "line-crossing", 0xA5, APP_REPORT_INPUT_MAX,
                 AICAM_TRUE);
    CHECK(app_report_service_set_session(&g_session) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_boot_id() == BOOT_B);
    CHECK(app_report_status(BOOT_A, 40u, &ms, &ws) == APP_REPORT_ABI_NOT_FOUND);
    CHECK(app_report_status(BOOT_B, 40u, &ms, &ws) == APP_REPORT_ABI_NOT_FOUND);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 40u, &boot) ==
          APP_REPORT_ABI_OK);
    CHECK(boot == BOOT_B);
    CHECK(app_report_service_queue_count() == 1u);

    session_fill(&rotated, "line-crossing", 0x5B, APP_REPORT_INPUT_MAX,
                 AICAM_TRUE);
    CHECK(app_report_service_set_session(&rotated) == APP_REPORT_ABI_OK);
    CHECK(app_report_status(BOOT_B, 40u, &ms, &ws) == APP_REPORT_ABI_NOT_FOUND);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 41u, &boot) ==
          APP_REPORT_ABI_UNAUTHORIZED);
    rlen = build_report(rbuf, sizeof(rbuf), 41u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&rotated, rbuf, (uint32_t)rlen, 41u, &boot) ==
          APP_REPORT_ABI_OK);
    CHECK(boot == BOOT_B);
    CHECK(app_report_status(BOOT_B, 41u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(app_report_status(BOOT_B, 40u, &ms, &ws) == APP_REPORT_ABI_NOT_FOUND);
}

static void test_input_budget_boundary(void)
{
    uint32_t boot = 0;
    static char exact[APP_REPORT_INPUT_MAX + 1u];
    size_t head;
    size_t target = (size_t)APP_REPORT_INPUT_MAX;

    reset_all();
    const char *tail = "\"},\"target\":{},\"model\":{}}";
    size_t tail_len = strlen(tail);
    head = (size_t)snprintf(
        exact, sizeof(exact),
        "{\"schema_version\":1,\"type\":\"line_counting\","
        "\"device_id\":\"%s\",\"app\":{\"app_id\":\"line-crossing\"},"
        "\"boot_id\":0,\"report_seq\":50,\"window\":{},\"total\":{},"
        "\"counter\":{\"counter_name\":\"", PLACEHOLDER);
    CHECK(head < target - 40u);
    for (size_t i = head; i < target - tail_len; i++) {
        exact[i] = 'n';
    }
    snprintf(exact + (target - tail_len), sizeof(exact) - (target - tail_len),
             "%s", tail);
    CHECK(strlen(exact) == target);
    CHECK(app_report_submit(&g_session, exact, (uint32_t)strlen(exact), 50u,
                            &boot) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_queue_count() == 1u);
    app_report_service_pump();
    CHECK(g_ft.mqtt_last_len > (uint32_t)strlen(exact));
    CHECK(g_ft.mqtt_last_len <= APP_REPORT_SLOT_CAP);
    CHECK(span_find(g_ft.mqtt_last, g_ft.mqtt_last_len, "\"report_seq\":50") !=
          NULL);
    CHECK(span_find(g_ft.mqtt_last, g_ft.mqtt_last_len, "counter_name\":\"nnnn") !=
          NULL);

    reset_all();
    {
        static char over[APP_REPORT_INPUT_MAX + 8u];
        size_t ohead;
        size_t otarget = (size_t)APP_REPORT_INPUT_MAX + 1u;

        ohead = (size_t)snprintf(
            over, sizeof(over),
            "{\"schema_version\":1,\"type\":\"line_counting\","
            "\"device_id\":\"%s\",\"app\":{\"app_id\":\"line-crossing\"},"
            "\"boot_id\":0,\"report_seq\":51,\"window\":{},\"total\":{},"
            "\"counter\":{\"counter_name\":\"", PLACEHOLDER);
        for (size_t i = ohead; i < otarget - 2u; i++) {
            over[i] = 'p';
        }
        over[otarget - 2u] = '"';
        over[otarget - 1u] = '}';
        over[otarget] = '\0';
        CHECK(strlen(over) == otarget);
        CHECK(app_report_submit(&g_session, over, (uint32_t)strlen(over), 51u,
                                &boot) == APP_REPORT_ABI_QUOTA_EXCEEDED);
        CHECK(app_report_service_queue_count() == 0u);
    }
}

static void test_stats_and_misc(void)
{
    app_report_stats_t st;
    uint32_t boot = 0;
    char rbuf[512];
    size_t rlen;
    app_report_entry_info_t info;

    reset_all();
    app_report_service_pump();
    CHECK(app_report_service_queue_count() == 0u);

    rlen = build_report(rbuf, sizeof(rbuf), 60u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 60u, &boot) ==
          APP_REPORT_ABI_OK);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 60u, &boot) ==
          APP_REPORT_ABI_OK);
    CHECK(app_report_submit(&g_session, "{bad", 4u, 61u, &boot) ==
          APP_REPORT_ABI_INVALID_ARGUMENT);
    app_report_service_get_stats(&st);
    CHECK(st.accepted == 1u);
    CHECK(st.dedup_hits == 1u);
    CHECK(st.rejected_schema == 1u);
    CHECK(st.rejected_unauthorized == 0u);
    CHECK(app_report_service_entry(APP_REPORT_QUEUE_SLOTS, &info) == AICAM_FALSE);
    CHECK(strcmp(app_report_result_str(APP_REPORT_ABI_OK), "OK") == 0);
    CHECK(strcmp(app_report_result_str(APP_REPORT_ABI_BUSY), "BUSY") == 0);
    CHECK(strcmp(app_report_result_str(APP_REPORT_ABI_NOT_FOUND), "NOT_FOUND") ==
          0);
    CHECK(strcmp(app_report_result_str(APP_REPORT_ABI_QUOTA_EXCEEDED),
                 "QUOTA_EXCEEDED") == 0);
    CHECK(strcmp(app_report_result_str(1234), "UNKNOWN") == 0);
}

int main(void)
{
    test_session_gates();
    test_param_rejections();
    test_quota();
    test_json_validity();
    test_schema_rejections();
    test_normalization_gaps_dedup();
    test_insertion_when_identity_absent();
    test_forged_device_id();
    test_identity_fail_closed();
    test_channel_independence();
    test_channel_disabled();
    test_queue_bounds();
    test_restart_and_key_change();
    test_input_budget_boundary();
    test_stats_and_misc();
    printf("app_report: %d checks, %d failures\n", g_total, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
