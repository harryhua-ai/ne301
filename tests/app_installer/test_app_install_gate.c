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

static const uint8_t g_gate_anchor_spki_der[] = {
    0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02,
    0x01, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03,
    0x42, 0x00, 0x04, 0x7f, 0xcf, 0x75, 0xcd, 0x65, 0x69, 0xa5, 0x8f, 0x9c,
    0x2b, 0x26, 0xc6, 0xfa, 0x99, 0x16, 0x66, 0xaa, 0xc3, 0x9d, 0x44, 0x3f,
    0x7c, 0x8f, 0xeb, 0xb6, 0x14, 0xeb, 0x9d, 0x6d, 0x89, 0x23, 0xbc, 0xeb,
    0x89, 0xa3, 0xec, 0x42, 0x38, 0xa0, 0xdd, 0x9b, 0x38, 0x3c, 0x2b, 0xfd,
    0x94, 0x90, 0x45, 0x42, 0xa6, 0xa8, 0xb5, 0x70, 0x32, 0xcd, 0x4d, 0xfb,
    0x7b, 0x4c, 0xe5, 0xda, 0x86, 0xad, 0x3f
};

static const uint8_t g_gate_anchor_spki_sha256[] = {
    0x06, 0xe7, 0x32, 0xb1, 0xb9, 0x4d, 0xae, 0xee, 0xf4, 0x33, 0x29, 0x7f,
    0x7a, 0x8e, 0x79, 0x7b, 0xf7, 0x2b, 0x2f, 0xd3, 0xb8, 0x82, 0xa4, 0xec,
    0x6a, 0x54, 0x90, 0x35, 0x29, 0x31, 0xeb, 0xda
};

static const app_trust_anchor_t g_gate_anchor = {
    APP_TRUST_ANCHOR_FORMAT_VERSION,
    "ne301-exp-pub",
    sizeof("ne301-exp-pub") - 1u,
    g_gate_anchor_spki_der,
    sizeof(g_gate_anchor_spki_der),
    g_gate_anchor_spki_sha256
};

static int g_gate_anchor_missing;

static void gate_anchor_set_missing(int missing)
{
    g_gate_anchor_missing = missing;
}

const app_trust_anchor_t *app_trust_anchor_get(void)
{
    return &g_gate_anchor;
}

app_trust_anchor_status_t app_trust_anchor_status(void)
{
    return (g_gate_anchor_missing != 0) ? APP_TRUST_ANCHOR_MISSING : APP_TRUST_ANCHOR_PROVISIONED;
}

const char *app_trust_anchor_status_str(app_trust_anchor_status_t status)
{
    switch (status) {
    case APP_TRUST_ANCHOR_PROVISIONED:
        return "provisioned";
    case APP_TRUST_ANCHOR_MISSING:
        return "missing";
    case APP_TRUST_ANCHOR_MALFORMED:
        return "malformed";
    default:
        return "unknown";
    }
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
    gate_anchor_set_missing(0);
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

static void test_state_query_trust_unproven_paths(void)
{
    mem_kv_t *kv;
    uint8_t blob[APP_INSTALL_RECORD_BIN_SIZE];

    reset_storage();

    get_state(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"state\":\"none\"") != NULL, "unproven_none");

    post_install(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN, 1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"installed\"") != NULL, "unproven_setup");

    gate_anchor_set_missing(1);
    get_state(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"unavailable\"") != NULL,
              "unproven_unavailable");
    CHECK_MSG(strstr(g_last_response_data, "\"publisher_id\":\"ne301-exp-pub\"") != NULL,
              "unproven_identity_publisher");
    CHECK_MSG(strstr(g_last_response_data, "\"app_id\":\"hello-app\"") != NULL,
              "unproven_identity_app");
    CHECK_MSG(strstr(g_last_response_data, "\"version_major\":1") != NULL,
              "unproven_identity_version");
    CHECK_MSG(strstr(g_last_response_data,
                     "\"package_sha256\":\"f98811f8f5eab265ad9e0df532bf71b1e54bdb2b8cc111866c2c4c87f1b7dcd8\"") !=
                  NULL,
              "unproven_identity_hash");
    CHECK_MSG(strstr(g_last_response_data, "\"reason\":\"TRUST_UNPROVEN\"") != NULL,
              "unproven_reason");
    CHECK_MSG(strstr(g_last_response_data, "\"executable\":false") != NULL,
              "unproven_not_executable");

    post_install(HELLO_V2_PACKAGE, HELLO_V2_PACKAGE_LEN, 1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"refused\"") != NULL,
              "unproven_install_refused");
    CHECK_MSG(strstr(g_last_response_data, "TRUST_UNPROVEN") != NULL,
              "unproven_install_reason");
    CHECK(count_files() == 1);

    kv = mem_find_key(APP_INSTALL_KEY_RECORD);
    CHECK(kv != NULL);
    memcpy(blob, kv->data, sizeof(blob));
    blob[30] ^= 0x80u;
    memcpy(kv->data, blob, sizeof(blob));

    get_state(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"state\":\"unknown\"") != NULL,
              "corrupt_unknown");
    CHECK_MSG(strstr(g_last_response_data, "STORAGE_STATE_UNKNOWN") != NULL,
              "corrupt_unknown_reason");

    gate_anchor_set_missing(0);
    get_state(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"state\":\"unknown\"") != NULL,
              "corrupt_still_unknown_after_anchor_back");

    reset_storage();
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
    test_state_query_trust_unproven_paths();
    test_install_busy_running();

    printf("app_install_gate: %d checks, %d failures\n", g_total, g_fail);
    return g_fail == 0 ? 0 : 1;
}
