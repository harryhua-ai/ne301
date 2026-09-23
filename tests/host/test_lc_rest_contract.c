#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "line_counting.h"
#include "cJSON.h"
#include "web_server.h"
#include "web_api.h"
#include "ai_service.h"
#include "json_config_mgr.h"

static int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);            \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

static int g_apply_calls;
static aicam_result_t g_apply_ret;
static line_counting_config_t g_apply_cfg;
static int g_reset_calls;
static aicam_result_t g_reset_ret;
static int g_config_set_calls;
static line_counting_config_t g_config_set_cfg;

static int g_resp_is_error;
static int g_resp_code;
static char g_resp_message[128];
static char g_resp_data[2048];

aicam_result_t line_counting_apply_config(const line_counting_config_t *cfg) {
    g_apply_calls++;
    g_apply_cfg = *cfg;
    return g_apply_ret;
}

static uint8_t g_is_resetting;
aicam_result_t line_counting_reset(void) {
    g_reset_calls++;
    return g_reset_ret;
}
uint8_t line_counting_is_resetting(void) {
    return g_is_resetting;
}

aicam_result_t json_config_set_line_counting_config(const line_counting_config_t *cfg) {
    g_config_set_calls++;
    g_config_set_cfg = *cfg;
    return AICAM_OK;
}

aicam_result_t json_config_get_line_counting_config(line_counting_config_t *cfg) {
    line_counting_config_defaults(cfg);
    return AICAM_OK;
}

aicam_result_t ai_get_model_runtime_info(ai_model_runtime_info_t *info) {
    (void)info;
    return AICAM_ERROR_NOT_FOUND;
}

aicam_result_t ai_get_model_class_name(uint16_t index, char *buf, uint32_t buf_size) {
    (void)index;
    (void)buf;
    (void)buf_size;
    return AICAM_ERROR_NOT_FOUND;
}

aicam_result_t line_counting_get_status(line_counting_status_t *out) {
    memset(out, 0, sizeof(*out));
    out->state = LC_STATE_DISABLED;
    return AICAM_OK;
}

aicam_result_t line_counting_get_stats(line_counting_stats_t *out) {
    memset(out, 0, sizeof(*out));
    return AICAM_OK;
}

aicam_result_t line_counting_get_events(line_count_event_t *out, uint16_t capacity,
                                        uint16_t *n_out) {
    (void)out;
    (void)capacity;
    *n_out = 0;
    return AICAM_OK;
}

static uint32_t g_now_ms = 12345;
uint32_t line_counting_get_now_ms(void) {
    return g_now_ms;
}

static uint32_t g_global_conf = 50;
static int g_set_conf_calls;
static uint32_t g_set_conf_value;
uint32_t ai_get_confidence_threshold(void) {
    return g_global_conf;
}
aicam_result_t ai_set_confidence_threshold(uint32_t threshold) {
    g_set_conf_calls++;
    g_set_conf_value = threshold;
    g_global_conf = threshold;
    return AICAM_OK;
}

static cJSON *g_stub_tracks;
cJSON *line_counting_get_tracks(void) {
    if (!g_stub_tracks) return NULL;
    cJSON *out = cJSON_Duplicate(g_stub_tracks, 1);
    return out;
}

aicam_result_t line_counting_get_delivery_stats(lc_delivery_stats_t *out) {
    memset(out, 0, sizeof(*out));
    return AICAM_OK;
}

aicam_result_t line_counting_get_heat(uint32_t *out_grid) {
    memset(out_grid, 0, LC_HEAT_GRID_SIZE * sizeof(uint32_t));
    return AICAM_OK;
}

aicam_bool_t web_api_verify_method(http_handler_context_t *ctx, const char *method) {
    return strcmp(ctx->request.method, method) == 0 ? AICAM_TRUE : AICAM_FALSE;
}

aicam_bool_t web_api_verify_content_type(http_handler_context_t *ctx,
                                         const char *content_type) {
    return strcmp(ctx->request.content_type, content_type) == 0 ? AICAM_TRUE
                                                                : AICAM_FALSE;
}

cJSON *web_api_parse_body(http_handler_context_t *ctx) {
    if (!ctx->request.body) return NULL;
    return cJSON_Parse(ctx->request.body);
}

aicam_result_t api_response_success(http_handler_context_t *ctx, const char *data,
                                    const char *message) {
    (void)ctx;
    g_resp_is_error = 0;
    g_resp_code = 200;
    snprintf(g_resp_message, sizeof(g_resp_message), "%s", message);
    snprintf(g_resp_data, sizeof(g_resp_data), "%s", data ? data : "");
    return AICAM_OK;
}

aicam_result_t api_response_error(http_handler_context_t *ctx, api_error_code_t code,
                                  const char *message) {
    (void)ctx;
    g_resp_is_error = 1;
    g_resp_code = (int)code;
    snprintf(g_resp_message, sizeof(g_resp_message), "%s", message);
    g_resp_data[0] = '\0';
    return AICAM_OK;
}

aicam_result_t http_server_register_route(const api_route_t *route) {
    (void)route;
    return AICAM_OK;
}

#include "../../Custom/Services/Web/api/api_line_counting_module.c"
#include "../../Custom/Services/Web/api/api_people_counting_module.c"

static void rest_reset_captures(void) {
    g_apply_calls = 0;
    g_apply_ret = AICAM_OK;
    g_reset_calls = 0;
    g_reset_ret = AICAM_OK;
    g_config_set_calls = 0;
    g_config_set_cfg = g_apply_cfg;
    g_resp_is_error = -1;
    g_resp_code = 0;
    g_resp_message[0] = '\0';
    g_resp_data[0] = '\0';
    g_set_conf_calls = 0;
    g_set_conf_value = 0;
}

static void rest_fill_context(http_handler_context_t *ctx, const char *body) {
    memset(ctx, 0, sizeof(*ctx));
    snprintf(ctx->request.method, sizeof(ctx->request.method), "POST");
    snprintf(ctx->request.content_type, sizeof(ctx->request.content_type),
             "application/json");
    ctx->request.body = (char *)body;
}

static void test_rest_config_pre_commit_failure_reports_error(void) {
    http_handler_context_t ctx;
    rest_reset_captures();
    rest_fill_context(&ctx, "{\"target_class\":\"car\"}");
    g_apply_ret = AICAM_ERROR_IO;

    aicam_result_t r = lc_api_config_post_handler(&ctx);
    (void)r;
    CHECK(g_apply_calls == 1);
    CHECK(g_resp_is_error == 1);
    CHECK(g_resp_code == API_ERROR_INTERNAL_ERROR);
    CHECK(g_resp_data[0] == '\0');
}

static void test_rest_config_committed_cleanup_failure_reports_success(void) {
    http_handler_context_t ctx;
    rest_reset_captures();
    rest_fill_context(&ctx, "{\"target_class\":\"car\"}");
    g_apply_ret = AICAM_OK;

    aicam_result_t r = lc_api_config_post_handler(&ctx);
    (void)r;
    CHECK(g_apply_calls == 1);
    CHECK(g_resp_is_error == 0);
    CHECK(g_resp_code == 200);
    CHECK(strstr(g_resp_data, "target_class") != NULL);
    CHECK(strstr(g_resp_data, "car") != NULL);
}

static void test_rest_reset_committed_cleanup_failure_reports_success(void) {
    http_handler_context_t ctx;
    rest_reset_captures();
    rest_fill_context(&ctx, NULL);
    g_reset_ret = AICAM_OK;

    lc_api_reset_handler(&ctx);
    CHECK(g_reset_calls == 1);
    CHECK(g_resp_is_error == 0);
    CHECK(g_resp_code == 200);
    CHECK(strstr(g_resp_data, "\"resetting\":true") != NULL ||
          strstr(g_resp_data, "resetting") != NULL);

    rest_reset_captures();
    rest_fill_context(&ctx, NULL);
    g_reset_ret = AICAM_ERROR_IO;
    lc_api_reset_handler(&ctx);
    CHECK(g_reset_calls == 1);
    CHECK(g_resp_is_error == 1);
    CHECK(g_resp_code == API_ERROR_INTERNAL_ERROR);

    rest_reset_captures();
    rest_fill_context(&ctx, NULL);
    g_reset_ret = AICAM_ERROR_BUSY;
    lc_api_reset_handler(&ctx);
    CHECK(g_reset_calls == 1);
    CHECK(g_resp_is_error == 1);
    CHECK(g_resp_code == API_ERROR_TOO_MANY_REQUESTS);
}

static void test_rest_config_invalid_target_rejected_before_apply(void) {
    http_handler_context_t ctx;
    rest_reset_captures();
    rest_fill_context(&ctx, "{\"target_class\":\"\"}");

    lc_api_config_post_handler(&ctx);
    CHECK(g_apply_calls == 0);
    CHECK(g_resp_is_error == 1);
}

static void test_legacy_post_apply_failure_single_boundary(void) {
    http_handler_context_t ctx;
    rest_reset_captures();
    rest_fill_context(&ctx, "{\"target_class_name\":\"car\"}");
    g_apply_ret = AICAM_ERROR_IO;

    pc_config_set_handler(&ctx);
    CHECK(g_config_set_calls == 0);
    CHECK(g_apply_calls == 1);
    CHECK(strcmp(g_apply_cfg.target_class_name, "car") == 0);
    CHECK(g_resp_is_error == 1);
    CHECK(g_resp_code == API_ERROR_INTERNAL_ERROR);
}

static void test_legacy_post_success_updates_canonical(void) {
    http_handler_context_t ctx;
    rest_reset_captures();
    rest_fill_context(&ctx, "{\"target_class_name\":\"car\"}");
    g_apply_ret = AICAM_OK;

    pc_config_set_handler(&ctx);
    CHECK(g_config_set_calls == 0);
    CHECK(g_apply_calls == 1);
    CHECK(strcmp(g_apply_cfg.target_class_name, "car") == 0);
    CHECK(g_resp_is_error == 0);
    CHECK(g_resp_code == 200);
}

static void rest_set_get_method(http_handler_context_t *ctx) {
    snprintf(ctx->request.method, sizeof(ctx->request.method), "GET");
}

static void rest_init_stub_tracks(void) {
    cJSON_Delete(g_stub_tracks);
    cJSON *trk = cJSON_CreateObject();
    cJSON_AddNumberToObject(trk, "track_id", 7);
    cJSON *pts = cJSON_CreateArray();
    cJSON *pt = cJSON_CreateArray();
    cJSON_AddItemToArray(pt, cJSON_CreateNumber(0.5));
    cJSON_AddItemToArray(pt, cJSON_CreateNumber(0.25));
    cJSON_AddItemToArray(pt, cJSON_CreateNumber(1234));
    cJSON_AddItemToArray(pts, pt);
    cJSON_AddItemToObject(trk, "points", pts);
    g_stub_tracks = cJSON_CreateArray();
    cJSON_AddItemToArray(g_stub_tracks, trk);
}

static void test_rest_events_response_contains_server_now_ms(void) {
    http_handler_context_t ctx;
    rest_reset_captures();
    rest_fill_context(&ctx, NULL);
    rest_set_get_method(&ctx);
    g_now_ms = 54321;

    lc_api_events_handler(&ctx);
    CHECK(g_resp_is_error == 0);
    cJSON *resp = cJSON_Parse(g_resp_data);
    CHECK(resp != NULL);
    if (resp) {
        cJSON *now = cJSON_GetObjectItem(resp, "server_now_ms");
        CHECK(cJSON_IsNumber(now) && now->valuedouble == 54321.0);
        cJSON *events = cJSON_GetObjectItem(resp, "events");
        CHECK(cJSON_IsArray(events));
        CHECK(cJSON_GetArraySize(events) == 0);
        cJSON_Delete(resp);
    }
}

static void test_rest_tracks_handler_serializes_snapshot(void) {
    http_handler_context_t ctx;
    rest_reset_captures();
    rest_init_stub_tracks();
    rest_fill_context(&ctx, NULL);
    rest_set_get_method(&ctx);

    lc_api_tracks_handler(&ctx);
    CHECK(g_resp_is_error == 0);
    cJSON *resp = cJSON_Parse(g_resp_data);
    CHECK(resp != NULL);
    if (resp) {
        cJSON *tracks = cJSON_GetObjectItem(resp, "tracks");
        CHECK(cJSON_IsArray(tracks));
        cJSON *t0 = cJSON_GetArrayItem(tracks, 0);
        CHECK(t0 != NULL);
        cJSON *tid = cJSON_GetObjectItem(t0, "track_id");
        cJSON *pts = cJSON_GetObjectItem(t0, "points");
        CHECK(cJSON_IsNumber(tid) && tid->valueint == 7);
        CHECK(cJSON_IsArray(pts) && cJSON_GetArraySize(pts) == 1);
        cJSON *pt = cJSON_GetArrayItem(pts, 0);
        CHECK(cJSON_IsArray(pt) && cJSON_GetArraySize(pt) == 3);
        cJSON *px = cJSON_GetArrayItem(pt, 0);
        CHECK(cJSON_IsNumber(px) && px->valuedouble == 0.5);
        cJSON *pts_ts = cJSON_GetArrayItem(pt, 2);
        CHECK(cJSON_IsNumber(pts_ts) && pts_ts->valuedouble == 1234.0);
        cJSON_Delete(resp);
    }

    rest_reset_captures();
    cJSON_Delete(g_stub_tracks);
    g_stub_tracks = NULL;
    rest_fill_context(&ctx, NULL);
    rest_set_get_method(&ctx);
    lc_api_tracks_handler(&ctx);
    CHECK(g_resp_is_error == 1);
    CHECK(g_resp_code == API_ERROR_INTERNAL_ERROR);
}

static void test_rest_config_get_mirrors_global_confidence(void) {
    http_handler_context_t ctx;
    rest_reset_captures();
    rest_fill_context(&ctx, NULL);
    rest_set_get_method(&ctx);
    g_global_conf = 37;

    lc_api_config_get_handler(&ctx);
    CHECK(g_resp_is_error == 0);
    cJSON *resp = cJSON_Parse(g_resp_data);
    CHECK(resp != NULL);
    if (resp) {
        cJSON *c = cJSON_GetObjectItem(resp, "confidence_threshold");
        CHECK(cJSON_IsNumber(c));
        CHECK(c->valuedouble > 0.369 && c->valuedouble < 0.371);
        cJSON_Delete(resp);
    }
}

static void test_rest_config_post_updates_global_confidence(void) {
    http_handler_context_t ctx;
    rest_reset_captures();
    rest_init_stub_tracks();
    rest_fill_context(&ctx, "{\"confidence_threshold\":0.55}");
    g_apply_ret = AICAM_OK;

    lc_api_config_post_handler(&ctx);
    CHECK(g_apply_calls == 1);
    CHECK(g_set_conf_calls == 1);
    CHECK(g_set_conf_value == 55);
    CHECK(g_resp_is_error == 0);
    cJSON *resp = cJSON_Parse(g_resp_data);
    CHECK(resp != NULL);
    if (resp) {
        cJSON *c = cJSON_GetObjectItem(resp, "confidence_threshold");
        CHECK(cJSON_IsNumber(c));
        CHECK(c->valuedouble > 0.549 && c->valuedouble < 0.551);
        cJSON_Delete(resp);
    }

    rest_reset_captures();
    rest_fill_context(&ctx, "{\"target_class\":\"car\"}");
    g_apply_ret = AICAM_ERROR_IO;
    lc_api_config_post_handler(&ctx);
    CHECK(g_apply_calls == 1);
    CHECK(g_set_conf_calls == 0);
    CHECK(g_resp_is_error == 1);

    rest_reset_captures();
    rest_fill_context(&ctx, "{\"target_class\":\"car\"}");
    g_apply_ret = AICAM_OK;
    lc_api_config_post_handler(&ctx);
    CHECK(g_apply_calls == 1);
    CHECK(g_set_conf_calls == 0);
    CHECK(g_resp_is_error == 0);
}

int main(void) {
    test_rest_config_pre_commit_failure_reports_error();
    test_rest_config_committed_cleanup_failure_reports_success();
    test_rest_reset_committed_cleanup_failure_reports_success();
    test_rest_config_invalid_target_rejected_before_apply();
    test_legacy_post_apply_failure_single_boundary();
    test_legacy_post_success_updates_canonical();
    test_rest_events_response_contains_server_now_ms();
    test_rest_tracks_handler_serializes_snapshot();
    test_rest_config_get_mirrors_global_confidence();
    test_rest_config_post_updates_global_confidence();

    if (g_failures) {
        printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all lc rest contract tests passed\n");
    return 0;
}
