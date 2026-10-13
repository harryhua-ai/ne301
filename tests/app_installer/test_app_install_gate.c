#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "mongoose.h"
#include "web_server.h"
#include "api_app_install_module.h"
#include "aicam_types.h"
#include "app_installer.h"
#include "app_admission.h"
#include "app_trust_anchor.h"
#include "installer_vectors.h"
#include "mem_io_backend.h"

static int g_total;
static int g_fail;

static char g_last_response_kind[16];
static int g_last_response_code;
static char g_last_response_message[256];
static char g_last_response_data[4096];

static void CHECK(int cond)
{
    g_total++;
    if (!cond) {
        g_fail++;
        printf("FAIL gate:%d\n", g_total);
    }
}

static void CHECK_MSG(int cond, const char *what)
{
    g_total++;
    if (!cond) {
        g_fail++;
        printf("FAIL gate:%d: %s\n", g_total, what);
        printf("  kind=[%s] code=%d msg=[%s]\n", g_last_response_kind, g_last_response_code,
               g_last_response_message);
        printf("  data=[%.600s]\n", g_last_response_data);
    }
}

aicam_bool_t web_server_high_privilege_allowed(http_handler_context_t *ctx)
{
    if (!ctx) {
        return AICAM_FALSE;
    }
    return (ctx->authenticated == AICAM_TRUE && ctx->high_privilege == AICAM_TRUE) ? AICAM_TRUE : AICAM_FALSE;
}

aicam_result_t api_response_success(http_handler_context_t *ctx, const char *data, const char *message)
{
    (void)ctx;
    snprintf(g_last_response_kind, sizeof(g_last_response_kind), "success");
    snprintf(g_last_response_message, sizeof(g_last_response_message), "%s",
             message ? message : "");
    g_last_response_data[0] = '\0';
    if (data) {
        snprintf(g_last_response_data, sizeof(g_last_response_data), "%s", data);
        free((void *)data);
    }
    return AICAM_OK;
}

aicam_result_t api_response_success_static(http_handler_context_t *ctx, const char *data, const char *message)
{
    (void)data;
    return api_response_success(ctx, NULL, message);
}

aicam_result_t api_response_error(http_handler_context_t *ctx, api_error_code_t error_code, const char *message)
{
    (void)ctx;
    snprintf(g_last_response_kind, sizeof(g_last_response_kind), "error");
    g_last_response_code = (int)error_code;
    snprintf(g_last_response_message, sizeof(g_last_response_message), "%s",
             message ? message : "");
    return AICAM_ERROR;
}

aicam_result_t api_response_error_data(http_handler_context_t *ctx, api_error_code_t error_code,
                                       const char *message, const char *data)
{
    (void)data;
    return api_response_error(ctx, error_code, message);
}

aicam_bool_t app_host_exec_region(uint32_t *base, uint32_t *size)
{
    *base = 0x93E00000u;
    *size = 2u * 1024u * 1024u;
    return AICAM_TRUE;
}

static int g_routes_registered;

aicam_result_t http_server_register_route(const api_route_t *route)
{
    (void)route;
    g_routes_registered++;
    return AICAM_OK;
}

static aicam_bool_t g_storage_initialized;

aicam_result_t app_installer_storage_init(void)
{
    g_storage_initialized = AICAM_TRUE;
    return AICAM_OK;
}

static app_install_io_t g_gate_io;

const app_install_io_t *app_installer_device_io(void)
{
    return &g_gate_io;
}

static http_handler_context_t make_ctx(const char *method, const char *body)
{
    static char body_buf[12000];
    http_handler_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    memset(body_buf, 0, sizeof(body_buf));
    snprintf(ctx.request.method, sizeof(ctx.request.method), "%s", method);
    snprintf(ctx.request.content_type, sizeof(ctx.request.content_type), "%s", "application/json");
    if (body) {
        snprintf(body_buf, sizeof(body_buf), "%s", body);
        ctx.request.body = body_buf;
    }
    return ctx;
}

static http_handler_context_t make_ctx_heap(const char *method, char *body)
{
    http_handler_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    snprintf(ctx.request.method, sizeof(ctx.request.method), "%s", method);
    snprintf(ctx.request.content_type, sizeof(ctx.request.content_type), "%s", "application/json");
    ctx.request.body = body;
    return ctx;
}

static void reset_response(void)
{
    g_last_response_kind[0] = '\0';
    g_last_response_code = -1;
    g_last_response_message[0] = '\0';
    g_last_response_data[0] = '\0';
}

static void reset_storage(void)
{
    mem_reset();
    mem_io_init(&g_gate_io);
    app_install_boot_resolve_reset();
    app_install_set_app_running(AICAM_FALSE);
}

static size_t b64_encode(const uint8_t *src, size_t len, char *dst, size_t cap)
{
    return mg_base64_encode((const unsigned char *)src, len, dst, cap);
}

static aicam_result_t post_install(const uint8_t *package_bytes, size_t package_len,
                                   int authenticated, int high_privilege)
{
    char b64[8192];
    char *body;
    size_t n;
    http_handler_context_t ctx;

    n = b64_encode(package_bytes, package_len, b64, sizeof(b64));
    if (n == 0 || n >= sizeof(b64)) {
        g_total++;
        g_fail++;
        printf("FAIL gate: b64 encode\n");
        return AICAM_ERROR;
    }
    body = malloc(n + 64);
    if (body == NULL) {
        g_total++;
        g_fail++;
        printf("FAIL gate: alloc\n");
        return AICAM_ERROR;
    }
    snprintf(body, n + 64, "{\"package_base64\":\"%s\"}", b64);
    ctx = make_ctx_heap("POST", body);
    ctx.authenticated = (authenticated != 0) ? AICAM_TRUE : AICAM_FALSE;
    ctx.high_privilege = (high_privilege != 0) ? AICAM_TRUE : AICAM_FALSE;
    reset_response();
    {
        aicam_result_t rc = app_install_package_handler(&ctx);
        free(body);
        return rc;
    }
}

static aicam_result_t get_state(int authenticated, int high_privilege)
{
    http_handler_context_t ctx = make_ctx("GET", NULL);
    ctx.authenticated = (authenticated != 0) ? AICAM_TRUE : AICAM_FALSE;
    ctx.high_privilege = (high_privilege != 0) ? AICAM_TRUE : AICAM_FALSE;
    reset_response();
    return app_install_state_handler(&ctx);
}

static aicam_result_t delete_installed(int authenticated, int high_privilege)
{
    http_handler_context_t ctx = make_ctx("DELETE", NULL);
    ctx.authenticated = (authenticated != 0) ? AICAM_TRUE : AICAM_FALSE;
    ctx.high_privilege = (high_privilege != 0) ? AICAM_TRUE : AICAM_FALSE;
    reset_response();
    return app_install_remove_handler(&ctx);
}

static void test_route_table_registered(void)
{
    reset_storage();
    g_routes_registered = 0;
    CHECK(web_api_register_app_install_module() == AICAM_OK);
    CHECK(g_routes_registered == 3);
    CHECK(g_storage_initialized == AICAM_TRUE);
}

static void test_install_auth_gate(void)
{
    reset_storage();

    CHECK(post_install(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN, 0, 0) == AICAM_ERROR);
    CHECK(g_last_response_kind[0] == 'e');
    CHECK(g_last_response_code == API_ERROR_FORBIDDEN);
    CHECK(g_last_response_data[0] == '\0');
    CHECK(count_files() == 0);

    CHECK(post_install(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN, 1, 0) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_FORBIDDEN);
    CHECK(count_files() == 0);

    CHECK(get_state(1, 0) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_FORBIDDEN);

    CHECK(delete_installed(1, 0) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_FORBIDDEN);
    CHECK(count_files() == 0);
}

static void test_install_method_gate(void)
{
    http_handler_context_t ctx;
    char b64[8192];
    char body[8700];

    reset_storage();
    (void)b64_encode(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN, b64, sizeof(b64));
    snprintf(body, sizeof(body), "{\"package_base64\":\"%s\"}", b64);

    ctx = make_ctx("GET", body);
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    reset_response();
    CHECK(app_install_package_handler(&ctx) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_METHOD_NOT_ALLOWED);

    ctx = make_ctx("POST", NULL);
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    reset_response();
    CHECK(app_install_state_handler(&ctx) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_METHOD_NOT_ALLOWED);

    ctx = make_ctx("POST", NULL);
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    reset_response();
    CHECK(app_install_remove_handler(&ctx) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_METHOD_NOT_ALLOWED);
}

static void test_install_payload_negatives(void)
{
    http_handler_context_t ctx;

    reset_storage();

    ctx = make_ctx("POST", NULL);
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    reset_response();
    CHECK(app_install_package_handler(&ctx) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_INVALID_REQUEST);

    ctx = make_ctx("POST", "not-json");
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    reset_response();
    CHECK(app_install_package_handler(&ctx) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_INVALID_REQUEST);

    ctx = make_ctx("POST", "{\"other\":1}");
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    reset_response();
    CHECK(app_install_package_handler(&ctx) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_INVALID_REQUEST);

    ctx = make_ctx("POST", "{\"package_base64\":\"not-base64!!\"}");
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    reset_response();
    CHECK(app_install_package_handler(&ctx) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_INVALID_REQUEST);

    {
        char *huge = malloc(2900000u);
        char *body = malloc(2900100u);
        size_t i;
        CHECK(huge != NULL && body != NULL);
        if (huge != NULL && body != NULL) {
            for (i = 0; i < 2899999u; i++) {
                huge[i] = 'A';
            }
            huge[2899999u] = '\0';
            snprintf(body, 2900100u, "{\"package_base64\":\"%s\"}", huge);
            ctx = make_ctx_heap("POST", body);
            ctx.authenticated = AICAM_TRUE;
            ctx.high_privilege = AICAM_TRUE;
            reset_response();
            CHECK(app_install_package_handler(&ctx) == AICAM_ERROR);
            CHECK(g_last_response_code == API_ERROR_INVALID_REQUEST);
            free(huge);
            free(body);
        }
    }
    CHECK(count_files() == 0);
}

static void test_install_lifecycle_end_to_end(void)
{
    reset_storage();

    get_state(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"state\":\"none\"") != NULL, "query_none");

    post_install(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN, 1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"installed\"") != NULL, "v1_installed");
    CHECK_MSG(strstr(g_last_response_data, "\"app_id\":\"hello-app\"") != NULL, "v1_app_id");
    CHECK_MSG(strstr(g_last_response_data, "\"publisher_id\":\"ne301-exp-pub\"") != NULL, "v1_publisher");
    CHECK_MSG(strstr(g_last_response_data, "\"version_major\":1") != NULL, "v1_version");
    CHECK_MSG(strstr(g_last_response_data, "\"package_sha256\":\"f98811f8f5eab265ad9e0df532bf71b1e54bdb2b8cc111866c2c4c87f1b7dcd8\"") != NULL, "v1_pkg_sha");
    CHECK(strstr(g_last_response_data, "\"tbs_sha256\":\"c6c6fe041b7795e73577aac355a83c1e9195ff30f0322d346ee2f18b38ccc177\"") != NULL);
    CHECK_MSG(strstr(g_last_response_data, "\"executable\":false") != NULL, "v1_not_executable");
    CHECK(count_files() == 1);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);

    get_state(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"installed\"") != NULL, "v1_query_installed");
    CHECK(strstr(g_last_response_data, "\"version_major\":1") != NULL);

    post_install(HELLO_V2_PACKAGE, HELLO_V2_PACKAGE_LEN, 1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"installed\"") != NULL, "v2_installed");
    CHECK(strstr(g_last_response_data, "\"version_major\":2") != NULL);
    CHECK(count_files() == 1);

    get_state(1, 1);
    CHECK(strstr(g_last_response_data, "\"version_major\":2") != NULL);

    post_install(HELLO_V2_PACKAGE, HELLO_V2_PACKAGE_LEN, 1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"already_installed\"") != NULL, "v2_idempotent");

    post_install(HELLO_V2_ALT_PACKAGE, HELLO_V2_ALT_PACKAGE_LEN, 1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"refused\"") != NULL, "v2alt_refused");
    CHECK_MSG(strstr(g_last_response_data, "VERSION_CONFLICT") != NULL, "v2alt_conflict");

    post_install(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN, 1, 1);
    CHECK_MSG(strstr(g_last_response_data, "DOWNGRADE_REFUSED") != NULL, "downgrade_refused");

    post_install(HELLO_BADBOARD_PACKAGE, HELLO_BADBOARD_PACKAGE_LEN, 1, 1);
    CHECK_MSG(strstr(g_last_response_data, "TARGET_INCOMPATIBLE") != NULL, "badboard_refused");

    post_install(HELLO_FOREIGN_PACKAGE, HELLO_FOREIGN_PACKAGE_LEN, 1, 1);
    CHECK_MSG(strstr(g_last_response_data, "SIGNATURE_INVALID") != NULL, "foreign_refused");

    get_state(1, 1);
    CHECK(strstr(g_last_response_data, "\"version_major\":2") != NULL);
    CHECK(count_files() == 1);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);

    delete_installed(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"uninstalled\"") != NULL, "uninstalled");
    CHECK(count_files() == 0);

    get_state(1, 1);
    CHECK(strstr(g_last_response_data, "\"state\":\"none\"") != NULL);

    delete_installed(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "NOT_FOUND") != NULL, "uninstall_not_found");
}

static void test_install_busy_running(void)
{
    reset_storage();
    post_install(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN, 1, 1);
    CHECK(strstr(g_last_response_data, "\"status\":\"installed\"") != NULL);

    app_install_set_app_running(AICAM_TRUE);
    post_install(HELLO_V2_PACKAGE, HELLO_V2_PACKAGE_LEN, 1, 1);
    CHECK_MSG(strstr(g_last_response_data, "BUSY") != NULL, "install_busy_running");
    delete_installed(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "BUSY") != NULL, "uninstall_busy_running");
    app_install_set_app_running(AICAM_FALSE);
}

int main(void)
{
    test_route_table_registered();
    test_install_auth_gate();
    test_install_method_gate();
    test_install_payload_negatives();
    test_install_lifecycle_end_to_end();
    test_install_busy_running();

    printf("app_install_gate: %d checks, %d failures\n", g_total, g_fail);
    return g_fail == 0 ? 0 : 1;
}
