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

static int g_resp_is_error;
static int g_resp_code;
static char g_resp_message[128];
static char g_resp_data[512];

aicam_result_t line_counting_apply_config(const line_counting_config_t *cfg) {
    g_apply_calls++;
    g_apply_cfg = *cfg;
    return g_apply_ret;
}

aicam_result_t line_counting_reset(void) {
    g_reset_calls++;
    return g_reset_ret;
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

aicam_result_t line_counting_get_delivery_stats(lc_delivery_stats_t *out) {
    memset(out, 0, sizeof(*out));
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

static void rest_reset_captures(void) {
    g_apply_calls = 0;
    g_apply_ret = AICAM_OK;
    g_reset_calls = 0;
    g_reset_ret = AICAM_OK;
    g_resp_is_error = -1;
    g_resp_code = 0;
    g_resp_message[0] = '\0';
    g_resp_data[0] = '\0';
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

    rest_reset_captures();
    rest_fill_context(&ctx, NULL);
    g_reset_ret = AICAM_ERROR_IO;
    lc_api_reset_handler(&ctx);
    CHECK(g_reset_calls == 1);
    CHECK(g_resp_is_error == 1);
    CHECK(g_resp_code == API_ERROR_INTERNAL_ERROR);
}

static void test_rest_config_invalid_target_rejected_before_apply(void) {
    http_handler_context_t ctx;
    rest_reset_captures();
    rest_fill_context(&ctx, "{\"target_class\":\"\"}");

    lc_api_config_post_handler(&ctx);
    CHECK(g_apply_calls == 0);
    CHECK(g_resp_is_error == 1);
}

int main(void) {
    test_rest_config_pre_commit_failure_reports_error();
    test_rest_config_committed_cleanup_failure_reports_success();
    test_rest_reset_committed_cleanup_failure_reports_success();
    test_rest_config_invalid_target_rejected_before_apply();

    if (g_failures) {
        printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all lc rest contract tests passed\n");
    return 0;
}
