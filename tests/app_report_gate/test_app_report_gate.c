#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>

#include "mongoose.h"
#include "web_server.h"
#include "web_api.h"
#include "api_app_report_module.h"
#include "aicam_types.h"
#include "app_report_service.h"
#include "app_host_session.h"

static int g_total;
static int g_fail;
static char g_last_response_kind[16];
static int g_last_response_code;
static char g_last_response_message[256];
static char g_last_response_data[16384];

static void check_impl(int cond, int line)
{
    g_total++;
    if (!cond) {
        g_fail++;
        printf("FAIL report_gate:%d line=%d kind=[%s] code=%d msg=[%s] data=[%.600s]\n",
               g_total, line, g_last_response_kind, g_last_response_code,
               g_last_response_message, g_last_response_data);
    }
}

static void check_msg_impl(int cond, int line, const char *what)
{
    g_total++;
    if (!cond) {
        g_fail++;
        printf("FAIL report_gate:%d line=%d: %s\n", g_total, line, what);
    }
}

#define CHECK(cond) check_impl((cond), __LINE__)
#define CHECK_MSG(cond, what) check_msg_impl((cond), __LINE__, (what))

aicam_bool_t web_server_high_privilege_allowed(http_handler_context_t *ctx);
aicam_result_t api_response_success(http_handler_context_t *ctx, const char *data,
                                    const char *message);
aicam_result_t api_response_error(http_handler_context_t *ctx, api_error_code_t error_code,
                                  const char *message);
aicam_result_t http_server_register_route(const api_route_t *route);

aicam_bool_t web_server_high_privilege_allowed(http_handler_context_t *ctx)
{
    if (ctx == NULL) {
        return AICAM_FALSE;
    }
    return (aicam_bool_t)((ctx->authenticated == AICAM_TRUE &&
                           ctx->high_privilege == AICAM_TRUE)
                              ? AICAM_TRUE
                              : AICAM_FALSE);
}

aicam_result_t api_response_success(http_handler_context_t *ctx, const char *data,
                                    const char *message)
{
    (void)ctx;
    snprintf(g_last_response_kind, sizeof(g_last_response_kind), "success");
    snprintf(g_last_response_message, sizeof(g_last_response_message), "%s",
             message ? message : "");
    g_last_response_data[0] = '\0';
    if (data != NULL) {
        snprintf(g_last_response_data, sizeof(g_last_response_data), "%s", data);
        free((void *)data);
    }
    return AICAM_OK;
}

aicam_result_t api_response_error(http_handler_context_t *ctx, api_error_code_t error_code,
                                  const char *message)
{
    (void)ctx;
    snprintf(g_last_response_kind, sizeof(g_last_response_kind), "error");
    g_last_response_code = (int)error_code;
    snprintf(g_last_response_message, sizeof(g_last_response_message), "%s",
             message ? message : "");
    return AICAM_ERROR;
}

#define MAX_CAPTURED_ROUTES 8
static api_route_t g_captured_routes[MAX_CAPTURED_ROUTES];
static int g_captured_route_count;

aicam_result_t http_server_register_route(const api_route_t *route)
{
    if (g_captured_route_count >= MAX_CAPTURED_ROUTES) {
        return AICAM_ERROR;
    }
    g_captured_routes[g_captured_route_count] = *route;
    g_captured_route_count++;
    return AICAM_OK;
}

static app_host_session_status_t g_fake_session;
static aicam_bool_t g_fake_session_valid;

void app_host_session_status(app_host_session_status_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (g_fake_session_valid == AICAM_TRUE) {
        *out = g_fake_session;
    }
}

const char *app_session_state_str(app_session_state_t state)
{
    switch (state) {
    case APP_SESSION_STATE_IDLE:
        return "idle";
    case APP_SESSION_STATE_RUNNING:
        return "running";
    case APP_SESSION_STATE_STOPPING:
        return "stopping";
    default:
        return "unknown";
    }
}

static uint32_t fake_boot_id(void *user)
{
    (void)user;
    return 0x0057B00Fu;
}

static aicam_bool_t fake_device_id(void *user, char *out, uint32_t cap)
{
    (void)user;
    if (out == NULL || cap < 6u) {
        return AICAM_FALSE;
    }
    strcpy(out, "aa-bb-01");
    return AICAM_TRUE;
}

static uint32_t fake_mqtt_ready(void *user)
{
    (void)user;
    return APP_REPORT_CHANNEL_READY;
}

static int fake_mqtt_fail(void *user, const uint8_t *json, uint32_t len)
{
    (void)user;
    (void)json;
    (void)len;
    return APP_REPORT_TRANSPORT_FAIL;
}

static uint32_t fake_webhook_ready(void *user)
{
    (void)user;
    return APP_REPORT_CHANNEL_READY;
}

static int fake_webhook_fail(void *user, const uint8_t *json, uint32_t len)
{
    (void)user;
    (void)json;
    (void)len;
    return APP_REPORT_TRANSPORT_FAIL;
}

static app_report_identity_t g_rep_identity;
static app_report_transport_t g_rep_transport;

static void rep_init_service(void)
{
    app_report_identity_t *identity = &g_rep_identity;
    app_report_transport_t *transport = &g_rep_transport;

    memset(identity, 0, sizeof(*identity));
    memset(transport, 0, sizeof(*transport));
    identity->user = NULL;
    identity->device_id = fake_device_id;
    identity->boot_id = fake_boot_id;
    transport->user = NULL;
    transport->mqtt_ready = fake_mqtt_ready;
    transport->mqtt_publish = fake_mqtt_fail;
    transport->webhook_ready = fake_webhook_ready;
    transport->webhook_post = fake_webhook_fail;
    app_report_service_set_lock(NULL, NULL);
    app_report_service_init(identity, transport);
    (void)app_report_service_set_session(NULL);
    g_fake_session_valid = AICAM_FALSE;
    memset(&g_fake_session, 0, sizeof(g_fake_session));
}

static void rep_bind_session(const uint8_t key_byte)
{
    app_report_session_t s;

    memset(&s, 0, sizeof(s));
    s.active = AICAM_TRUE;
    strcpy(s.publisher_id, "pub-1");
    strcpy(s.app_id, "line-crossing.app");
    memset(s.publisher_key_sha256, key_byte, APP_REPORT_SHA256_LEN);
    s.report_max = APP_REPORT_INPUT_MAX;
    if (app_report_service_set_session(&s) != APP_REPORT_ABI_OK) {
        printf("FATAL: set_session refused\n");
        exit(2);
    }
}

static int32_t rep_submit(uint32_t seq)
{
    app_report_session_t s;
    const char *json;
    uint32_t boot = 0u;

    memset(&s, 0, sizeof(s));
    s.active = AICAM_TRUE;
    strcpy(s.publisher_id, "pub-1");
    strcpy(s.app_id, "line-crossing.app");
    memset(s.publisher_key_sha256, 0xAA, APP_REPORT_SHA256_LEN);
    s.report_max = APP_REPORT_INPUT_MAX;
    json = "{\"schema_version\":1,\"type\":\"line_counting\",\"report_seq\":%u,"
           "\"window\":{\"in\":1,\"out\":0},\"total\":{\"in\":2,\"out\":1},"
           "\"counter\":{\"name\":\"door\"},\"target\":{\"class\":\"person\"},"
           "\"model\":{\"valid\":true}}";
    char buf[512];
    int n = snprintf(buf, sizeof(buf), json, seq);
    if (n <= 0 || (size_t)n >= sizeof(buf)) {
        return APP_REPORT_ABI_INVALID_ARGUMENT;
    }
    return app_report_submit(&s, buf, (uint32_t)n, seq, &boot);
}

static void rep_set_fake_session(app_session_state_t state)
{
    memset(&g_fake_session, 0, sizeof(g_fake_session));
    g_fake_session.state = state;
    strcpy(g_fake_session.publisher_id, "pub-1");
    strcpy(g_fake_session.app_id, "line-crossing.app");
    g_fake_session_valid = AICAM_TRUE;
}

static const api_route_t *find_route(const char *method, const char *path)
{
    int i;

    for (i = 0; i < g_captured_route_count; i++) {
        if (strcmp(g_captured_routes[i].method, method) == 0 &&
            strcmp(g_captured_routes[i].path, path) == 0) {
            return &g_captured_routes[i];
        }
    }
    return NULL;
}

static aicam_result_t invoke_route(const char *wire_method, const char *route_method,
                                   const char *path, aicam_bool_t auth,
                                   aicam_bool_t high_priv)
{
    const api_route_t *route = find_route(route_method, path);
    http_handler_context_t ctx;

    if (route == NULL) {
        return AICAM_ERROR;
    }
    memset(&ctx, 0, sizeof(ctx));
    snprintf(ctx.request.method, sizeof(ctx.request.method), "%s", wire_method);
    snprintf(ctx.request.content_type, sizeof(ctx.request.content_type), "%s",
             "application/json");
    ctx.request.body = NULL;
    ctx.authenticated = auth;
    ctx.high_privilege = high_priv;
    return route->handler(&ctx);
}

static aicam_result_t invoke_get(aicam_bool_t auth, aicam_bool_t high_priv)
{
    return invoke_route("GET", "GET", API_PATH_PREFIX "/app/report/status", auth, high_priv);
}

static void reset_response(void)
{
    g_last_response_kind[0] = '\0';
    g_last_response_code = -1;
    g_last_response_message[0] = '\0';
    g_last_response_data[0] = '\0';
}

static cJSON *parse_last(void)
{
    return cJSON_Parse(g_last_response_data);
}

static int json_array_len(cJSON *root)
{
    cJSON *reports;
    int n = 0;
    cJSON *it;

    if (root == NULL) {
        return -1;
    }
    reports = cJSON_GetObjectItem(root, "reports");
    if (reports == NULL || !cJSON_IsArray(reports)) {
        return -1;
    }
    cJSON_ArrayForEach(it, reports) {
        n++;
    }
    return n;
}

static cJSON *json_report_at(cJSON *root, int index)
{
    cJSON *reports = cJSON_GetObjectItem(root, "reports");
    int i = 0;
    cJSON *it;

    if (reports == NULL) {
        return NULL;
    }
    cJSON_ArrayForEach(it, reports) {
        if (i == index) {
            return it;
        }
        i++;
    }
    return NULL;
}

int main(void)
{
    cJSON *root;
    cJSON *item;
    const char *s;

    reset_response();
    CHECK_MSG(web_api_register_app_report_module() == AICAM_OK, "module registers");
    reset_response();
    CHECK(g_captured_route_count == 1);
    CHECK(find_route("GET", API_PATH_PREFIX "/app/report/status") != NULL);
    CHECK(find_route("GET", API_PATH_PREFIX "/app/report/status")->require_auth == AICAM_TRUE);

    CHECK(invoke_route("POST", "GET", API_PATH_PREFIX "/app/report/status",
                       AICAM_TRUE, AICAM_TRUE) != AICAM_OK);
    CHECK(strcmp(g_last_response_kind, "error") == 0);
    CHECK(g_last_response_code == API_ERROR_METHOD_NOT_ALLOWED);

    reset_response();
    CHECK(invoke_get(AICAM_FALSE, AICAM_FALSE) != AICAM_OK);
    CHECK(strcmp(g_last_response_kind, "error") == 0);
    CHECK(g_last_response_code == API_ERROR_FORBIDDEN);

    reset_response();
    CHECK(invoke_get(AICAM_TRUE, AICAM_FALSE) != AICAM_OK);
    CHECK(g_last_response_code == API_ERROR_FORBIDDEN);

    rep_init_service();
    reset_response();
    CHECK(invoke_get(AICAM_TRUE, AICAM_TRUE) == AICAM_OK);
    CHECK(strcmp(g_last_response_kind, "success") == 0);
    root = parse_last();
    CHECK(root != NULL);
    CHECK(cJSON_IsTrue(cJSON_GetObjectItem(root, "session_bound")) == 0);
    s = cJSON_GetStringValue(cJSON_GetObjectItem(root, "session_state"));
    CHECK(s != NULL && strcmp(s, "idle") == 0);
    CHECK(cJSON_GetNumberValue(cJSON_GetObjectItem(root, "host_boot_id")) == 0.0);
    CHECK(json_array_len(root) == 0);
    cJSON_Delete(root);

    rep_bind_session(0xAA);
    CHECK(rep_submit(1) == APP_REPORT_ABI_OK);
    CHECK(rep_submit(2) == APP_REPORT_ABI_OK);
    rep_set_fake_session(APP_SESSION_STATE_RUNNING);
    reset_response();
    CHECK(invoke_get(AICAM_TRUE, AICAM_TRUE) == AICAM_OK);
    root = parse_last();
    CHECK(root != NULL);
    CHECK(cJSON_IsTrue(cJSON_GetObjectItem(root, "session_bound")) != 0);
    CHECK(cJSON_GetNumberValue(cJSON_GetObjectItem(root, "host_boot_id")) ==
          (double)fake_boot_id(NULL));
    CHECK(json_array_len(root) == 2);
    item = json_report_at(root, 0);
    CHECK(item != NULL);
    CHECK(cJSON_GetNumberValue(cJSON_GetObjectItem(item, "report_seq")) == 1.0);
    s = cJSON_GetStringValue(cJSON_GetObjectItem(item, "mqtt_state"));
    CHECK(s != NULL && strcmp(s, "pending") == 0);
    s = cJSON_GetStringValue(cJSON_GetObjectItem(item, "webhook_state"));
    CHECK(s != NULL && strcmp(s, "pending") == 0);
    CHECK(cJSON_GetNumberValue(cJSON_GetObjectItem(item, "mqtt_attempts")) == 0.0);
    CHECK(cJSON_GetNumberValue(cJSON_GetObjectItem(item, "payload_len")) > 0.0);
    CHECK(strstr(g_last_response_data, "\"payload\":") == NULL);
    CHECK(strstr(g_last_response_data, "publisher_key") == NULL);
    CHECK(strstr(g_last_response_data, "0xAa") == NULL);
    cJSON_Delete(root);

    for (int i = 0; i < 20; i++) {
        app_report_service_pump();
    }
    reset_response();
    CHECK(invoke_get(AICAM_TRUE, AICAM_TRUE) == AICAM_OK);
    root = parse_last();
    CHECK(root != NULL);
    item = json_report_at(root, 0);
    CHECK(item != NULL);
    s = cJSON_GetStringValue(cJSON_GetObjectItem(item, "mqtt_state"));
    CHECK(s != NULL && strcmp(s, "failed") == 0);
    s = cJSON_GetStringValue(cJSON_GetObjectItem(item, "webhook_state"));
    CHECK(s != NULL && strcmp(s, "failed") == 0);
    CHECK(cJSON_GetNumberValue(cJSON_GetObjectItem(item, "mqtt_attempts")) == 3.0);
    CHECK(cJSON_GetNumberValue(cJSON_GetObjectItem(item, "webhook_attempts")) == 3.0);
    cJSON_Delete(root);

    (void)app_report_service_set_session(NULL);
    CHECK(app_report_service_queue_count() == 2);
    reset_response();
    CHECK(invoke_get(AICAM_TRUE, AICAM_TRUE) == AICAM_OK);
    root = parse_last();
    CHECK(root != NULL);
    CHECK(cJSON_IsTrue(cJSON_GetObjectItem(root, "session_bound")) == 0);
    CHECK(cJSON_GetNumberValue(cJSON_GetObjectItem(root, "host_boot_id")) == 0.0);
    CHECK(cJSON_GetNumberValue(cJSON_GetObjectItem(root, "queue_used")) == 0.0);
    CHECK(json_array_len(root) == 0);
    cJSON_Delete(root);

    rep_bind_session(0xBB);
    rep_set_fake_session(APP_SESSION_STATE_RUNNING);
    reset_response();
    CHECK(invoke_get(AICAM_TRUE, AICAM_TRUE) == AICAM_OK);
    root = parse_last();
    CHECK(root != NULL);
    CHECK(cJSON_IsTrue(cJSON_GetObjectItem(root, "session_bound")) != 0);
    CHECK(json_array_len(root) == 0);
    CHECK(cJSON_GetNumberValue(cJSON_GetObjectItem(root, "queue_used")) == 0.0);
    cJSON_Delete(root);

    (void)app_report_service_set_session(NULL);
    rep_bind_session(0xAA);
    reset_response();
    CHECK(invoke_get(AICAM_TRUE, AICAM_TRUE) == AICAM_OK);
    root = parse_last();
    CHECK(root != NULL);
    CHECK(json_array_len(root) == 2);
    cJSON_Delete(root);

    (void)app_report_service_set_session(NULL);
    rep_bind_session(0xAA);
    rep_set_fake_session(APP_SESSION_STATE_STOPPING);
    reset_response();
    CHECK(invoke_get(AICAM_TRUE, AICAM_TRUE) == AICAM_OK);
    root = parse_last();
    CHECK(root != NULL);
    CHECK(cJSON_IsTrue(cJSON_GetObjectItem(root, "session_bound")) == 0);
    CHECK(json_array_len(root) == 0);
    cJSON_Delete(root);

    rep_set_fake_session(APP_SESSION_STATE_RUNNING);
    for (uint32_t seq = 3u; seq <= 10u; seq++) {
        CHECK(rep_submit(seq) == APP_REPORT_ABI_OK);
        for (int i = 0; i < 20; i++) {
            app_report_service_pump();
        }
    }
    reset_response();
    CHECK(invoke_get(AICAM_TRUE, AICAM_TRUE) == AICAM_OK);
    root = parse_last();
    CHECK(root != NULL);
    CHECK(json_array_len(root) == APP_REPORT_QUEUE_SLOTS);
    CHECK(json_array_len(root) <= 8);
    cJSON_Delete(root);

    (void)app_report_service_set_session(NULL);
    rep_bind_session(0xAA);
    rep_set_fake_session(APP_SESSION_STATE_RUNNING);
    app_report_service_restart();
    reset_response();
    CHECK(invoke_get(AICAM_TRUE, AICAM_TRUE) == AICAM_OK);
    root = parse_last();
    CHECK(root != NULL);
    CHECK(json_array_len(root) == 0);
    CHECK(cJSON_GetNumberValue(cJSON_GetObjectItem(root, "queue_used")) == 0.0);
    cJSON_Delete(root);

    g_fake_session_valid = AICAM_TRUE;
    memset(&g_fake_session, 0, sizeof(g_fake_session));
    g_fake_session.state = APP_SESSION_STATE_RUNNING;
    strcpy(g_fake_session.publisher_id, "pub-other");
    strcpy(g_fake_session.app_id, "other.app");
    (void)app_report_service_set_session(NULL);
    rep_bind_session(0xAA);
    reset_response();
    CHECK(invoke_get(AICAM_TRUE, AICAM_TRUE) == AICAM_OK);
    root = parse_last();
    CHECK(root != NULL);
    CHECK(cJSON_IsTrue(cJSON_GetObjectItem(root, "session_bound")) == 0);
    CHECK(json_array_len(root) == 0);
    s = cJSON_GetStringValue(cJSON_GetObjectItem(root, "app_id"));
    CHECK(s != NULL && strcmp(s, "other.app") == 0);
    cJSON_Delete(root);

    printf("app_report_gate: total=%d fail=%d\n", g_total, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
