#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "fake_flash.h"
#include "storage.h"
#include "json_config_mgr.h"
#include "json_config_internal.h"
#include "json_config_boot_gate.h"
#include "auth_mgr.h"
#include "api_auth_module.h"
#include "api_device_module.h"
#include "web_api.h"
#include "web_server.h"
#include "aicam_types.h"

extern fake_flash_t g_e2e_user_ff;
extern bool g_user_io_fail;
extern void e2e_storage_reset(void);
extern int e2e_storage_boot(void);
extern void e2e_reset_response(void);
extern char g_last_response_kind[16];
extern int g_last_response_code;
extern char g_last_response_message[512];
extern bool g_service_running;

static int g_total;
static int g_fail;

#define CHECK(cond) do { \
    g_total++; \
    if (!(cond)) { \
        g_fail++; \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define ADMIN_PASSWORD_MAX 64

static void boot_fresh(void)
{
    e2e_storage_reset();
    CHECK(e2e_storage_boot() == 0);
}

static void run_config_and_auth(void)
{
    CHECK(json_config_mgr_init() == AICAM_OK);
    CHECK(auth_mgr_init() == AICAM_OK);
}

static uint8_t g_snapshot_user[8 * 4096];

static void snapshot_user(void)
{
    memcpy(g_snapshot_user, g_e2e_user_ff.mem, sizeof(g_snapshot_user));
}

static int user_bytes_changed(void)
{
    return memcmp(g_snapshot_user, g_e2e_user_ff.mem, sizeof(g_snapshot_user)) != 0;
}

static http_handler_context_t make_json_ctx(const char *method, const char *body)
{
    http_handler_context_t ctx;
    static char body_buf[512];
    memset(&ctx, 0, sizeof(ctx));
    memset(body_buf, 0, sizeof(body_buf));
    snprintf(ctx.request.method, sizeof(ctx.request.method), "%s", method);
    snprintf(ctx.request.content_type, sizeof(ctx.request.content_type), "%s", "application/json");
    if (body) {
        strncpy(body_buf, body, sizeof(body_buf) - 1);
        ctx.request.body = body_buf;
    }
    return ctx;
}

static void legacy_weak_hash(const char *password, char *hex_out)
{
    static const char *hex_chars = "0123456789abcdef";
    size_t len = strlen(password);
    uint32_t hash_val = 0x12345678u;
    uint8_t hash[32];
    memset(hash, 0, sizeof(hash));
    for (size_t i = 0; i < len; i++) {
        hash_val = hash_val * 33u + (uint8_t)password[i];
        hash_val ^= (hash_val >> 16);
    }
    for (int i = 0; i < 32; i += 4) {
        hash[i] = (uint8_t)(hash_val >> 24);
        hash[i + 1] = (uint8_t)(hash_val >> 16);
        hash[i + 2] = (uint8_t)(hash_val >> 8);
        hash[i + 3] = (uint8_t)(hash_val);
        hash_val = hash_val * 1664525u + 1013904223u;
    }
    for (int i = 0; i < 32; i++) {
        hex_out[i * 2] = hex_chars[hash[i] >> 4];
        hex_out[i * 2 + 1] = hex_chars[hash[i] & 0x0F];
    }
    hex_out[64] = '\0';
}

static void test_credential_record_codec(void)
{
    char record[AUTH_CREDENTIAL_RECORD_MAX];
    char small[8];

    CHECK(auth_mgr_credential_record_encode("codec-pw-123", record, sizeof(record)) == AICAM_OK);
    CHECK(auth_mgr_credential_is_record(record) == AICAM_TRUE);
    CHECK(strlen(record) == 100);
    CHECK(strncmp(record, "A1:", 3) == 0);
    CHECK(auth_mgr_credential_record_verify(record, "codec-pw-123") == AICAM_TRUE);
    CHECK(auth_mgr_credential_record_verify(record, "codec-pw-124") == AICAM_FALSE);
    CHECK(auth_mgr_credential_record_verify(record, "") == AICAM_FALSE);

    record[10] ^= 0x01;
    CHECK(auth_mgr_credential_record_verify(record, "codec-pw-123") == AICAM_FALSE);
    record[10] ^= 0x01;
    CHECK(auth_mgr_credential_record_verify(record, "codec-pw-123") == AICAM_TRUE);

    CHECK(auth_mgr_credential_record_encode("codec-pw-123", small, sizeof(small)) == AICAM_ERROR_INVALID_PARAM);
    CHECK(auth_mgr_credential_record_encode("", record, sizeof(record)) == AICAM_ERROR_INVALID_PARAM);
    CHECK(auth_mgr_credential_is_record("hicamthink") == AICAM_FALSE);
    CHECK(auth_mgr_credential_is_record("A1:short") == AICAM_FALSE);
    CHECK(auth_mgr_credential_is_record("") == AICAM_FALSE);
    CHECK(auth_mgr_credential_is_record(NULL) == AICAM_FALSE);
    CHECK(auth_mgr_credential_record_verify("hicamthink", "hicamthink") == AICAM_FALSE);
}

static void test_default_password_persisted_as_record(void)
{
    char credential[AUTH_CREDENTIAL_RECORD_MAX];
    char raw[128];
    int raw_len;

    boot_fresh();
    run_config_and_auth();
    CHECK(json_config_mgr_config_healthy() == AICAM_TRUE);
    CHECK(json_config_mgr_credential_trusted() == AICAM_TRUE);
    CHECK(auth_mgr_default_password_active() == AICAM_TRUE);
    CHECK(json_config_get_device_credential(credential, sizeof(credential)) == AICAM_OK);
    CHECK(strncmp(credential, "A1:", 3) == 0);
    memset(raw, 0, sizeof(raw));
    raw_len = storage_nvs_read(NVS_USER, "auth_password", raw, sizeof(raw) - 1);
    CHECK(raw_len == 101);
    CHECK(strncmp(raw, "A1:", 3) == 0);
    CHECK(strstr(raw, "hicamthink") == NULL);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_TRUE);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_export_contains_no_admin_secret(void)
{
    aicam_global_config_t config;
    char exported[JSON_CONFIG_MAX_BUFFER_SIZE];

    boot_fresh();
    run_config_and_auth();
    CHECK(auth_mgr_change_password("custom-secret-9") == AUTH_RESULT_SUCCESS);

    CHECK(json_config_get_config(&config) == AICAM_OK);
    CHECK(json_config_serialize_to_string(&config, exported, sizeof(exported)) == AICAM_OK);
    CHECK(strstr(exported, "custom-secret-9") == NULL);
    CHECK(strstr(exported, "hicamthink") == NULL);
    CHECK(strstr(exported, "A1:") == NULL);
    CHECK(strstr(exported, "admin_password\"") == NULL);
    CHECK(strstr(exported, "admin_password_configured") != NULL);
    CHECK(strstr(exported, "session_timeout_ms") != NULL);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_verdict_default_gate_matrix(void)
{
    auth_verdict_t verdict;

    boot_fresh();
    run_config_and_auth();

    verdict = auth_mgr_authorize(NULL, "hicamthink");
    CHECK(verdict.authenticated == AICAM_FALSE);
    verdict = auth_mgr_authorize("admin", NULL);
    CHECK(verdict.authenticated == AICAM_FALSE);
    verdict = auth_mgr_authorize("", "");
    CHECK(verdict.authenticated == AICAM_FALSE);
    verdict = auth_mgr_authorize("root", "hicamthink");
    CHECK(verdict.authenticated == AICAM_FALSE);
    CHECK(verdict.high_privilege_allowed == AICAM_FALSE);
    verdict = auth_mgr_authorize("admin", "wrong-password");
    CHECK(verdict.authenticated == AICAM_FALSE);
    CHECK(verdict.high_privilege_allowed == AICAM_FALSE);

    verdict = auth_mgr_authorize("admin", "hicamthink");
    CHECK(verdict.authenticated == AICAM_TRUE);
    CHECK(verdict.default_password_active == AICAM_TRUE);
    CHECK(verdict.high_privilege_allowed == AICAM_FALSE);

    CHECK(auth_mgr_change_password("custom-secret-9") == AUTH_RESULT_SUCCESS);

    verdict = auth_mgr_authorize("admin", "hicamthink");
    CHECK(verdict.authenticated == AICAM_FALSE);
    CHECK(verdict.high_privilege_allowed == AICAM_FALSE);

    verdict = auth_mgr_authorize("admin", "custom-secret-9");
    CHECK(verdict.authenticated == AICAM_TRUE);
    CHECK(verdict.default_password_active == AICAM_FALSE);
    CHECK(verdict.high_privilege_allowed == AICAM_TRUE);

    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);

    CHECK(e2e_storage_boot() == 0);
    run_config_and_auth();
    CHECK(auth_mgr_verify_password("custom-secret-9") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    CHECK(auth_mgr_default_password_active() == AICAM_FALSE);
    verdict = auth_mgr_authorize("admin", "custom-secret-9");
    CHECK(verdict.high_privilege_allowed == AICAM_TRUE);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_untrusted_media_authorize_failclosed(void)
{
    uint8_t snapshot[sizeof(g_snapshot_user)];

    boot_fresh();
    run_config_and_auth();
    CHECK(auth_mgr_change_password("real-password") == AUTH_RESULT_SUCCESS);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);

    snapshot_user();
    memcpy(snapshot, g_snapshot_user, sizeof(snapshot));
    g_user_io_fail = true;
    CHECK(e2e_storage_boot() != 0);

    CHECK(json_config_mgr_init() == AICAM_OK);
    CHECK(auth_mgr_init() == AICAM_OK);
    CHECK(json_config_mgr_credential_trusted() == AICAM_FALSE);
    CHECK(json_config_get_device_credential((char *)snapshot, 16) != AICAM_OK);
    CHECK(auth_mgr_verify_password("real-password") == AICAM_FALSE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    auth_verdict_t verdict = auth_mgr_authorize("admin", "hicamthink");
    CHECK(verdict.authenticated == AICAM_FALSE);
    CHECK(verdict.high_privilege_allowed == AICAM_FALSE);
    CHECK(auth_mgr_change_password("anything-else") == AUTH_RESULT_INTERNAL_ERROR);
    CHECK(json_config_set_device_password("anything-else") != AICAM_OK);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);

    g_user_io_fail = false;
    CHECK(e2e_storage_boot() == 0);
    run_config_and_auth();
    CHECK(auth_mgr_verify_password("real-password") == AICAM_TRUE);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_persist_failure_no_fake_success(void)
{
    boot_fresh();
    run_config_and_auth();
    CHECK(auth_mgr_change_password("first-pw-123") == AUTH_RESULT_SUCCESS);
    CHECK(auth_mgr_verify_password("first-pw-123") == AICAM_TRUE);

    snapshot_user();
    g_e2e_user_ff.fail_next_writes = 1;
    CHECK(auth_mgr_change_password("second-pw-456") == AUTH_RESULT_INTERNAL_ERROR);
    CHECK(auth_mgr_verify_password("second-pw-456") == AICAM_FALSE);
    CHECK(auth_mgr_verify_password("first-pw-123") == AICAM_TRUE);
    CHECK(json_config_set_device_password("second-pw-456") != AICAM_OK);
    CHECK(user_bytes_changed() == 0);
    g_e2e_user_ff.fail_next_writes = 0;
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);

    CHECK(e2e_storage_boot() == 0);
    run_config_and_auth();
    CHECK(auth_mgr_verify_password("first-pw-123") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("second-pw-456") == AICAM_FALSE);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_import_cannot_change_identity(void)
{
    aicam_global_config_t imported;
    aicam_global_config_t config;
    json_config_validation_options_t validation_opts = {
        .validate_json_syntax = AICAM_TRUE,
        .validate_data_types = AICAM_TRUE,
        .validate_value_ranges = AICAM_TRUE,
        .validate_checksum = AICAM_FALSE,
        .strict_mode = AICAM_FALSE
    };
    const char *attack_json =
        "{\"log_config\":{\"log_level\":3},"
        "\"auth_mgr\":{\"admin_password\":\"backdoor-pass\",\"session_timeout_ms\":7200000}}";
    char exported[JSON_CONFIG_MAX_BUFFER_SIZE];

    boot_fresh();
    run_config_and_auth();
    CHECK(auth_mgr_change_password("custom-secret-9") == AUTH_RESULT_SUCCESS);

    CHECK(json_config_parse_from_string(attack_json, &imported, &validation_opts) == AICAM_OK);
    CHECK(json_config_set_config(&imported) == AICAM_OK);

    CHECK(auth_mgr_verify_password("backdoor-pass") == AICAM_FALSE);
    CHECK(auth_mgr_verify_password("custom-secret-9") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    CHECK(json_config_get_config(&config) == AICAM_OK);
    CHECK(config.log_config.log_level == 3);
    CHECK(config.auth_mgr.session_timeout_ms == 7200000);
    CHECK(config.auth_mgr.admin_password[0] == '\0' ||
          strcmp(config.auth_mgr.admin_password, "custom-secret-9") == 0);

    CHECK(json_config_get_config(&config) == AICAM_OK);
    CHECK(json_config_serialize_to_string(&config, exported, sizeof(exported)) == AICAM_OK);
    CHECK(strstr(exported, "custom-secret-9") == NULL);
    CHECK(strstr(exported, "A1:") == NULL);

    aicam_global_config_t roundtrip;
    CHECK(json_config_parse_from_string(exported, &roundtrip, &validation_opts) == AICAM_OK);
    CHECK(json_config_set_config(&roundtrip) == AICAM_OK);
    CHECK(auth_mgr_verify_password("custom-secret-9") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("backdoor-pass") == AICAM_FALSE);

    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_set_config_persist_failure_propagates(void)
{
    aicam_global_config_t config;
    uint32_t original_level;

    boot_fresh();
    run_config_and_auth();
    CHECK(json_config_get_config(&config) == AICAM_OK);
    original_level = config.log_config.log_level;

    g_e2e_user_ff.fail_next_writes = 1;
    config.log_config.log_level = 3;
    CHECK(json_config_set_config(&config) != AICAM_OK);
    g_e2e_user_ff.fail_next_writes = 0;

    CHECK(json_config_get_config(&config) == AICAM_OK);
    CHECK(config.log_config.log_level == original_level);

    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_change_password_handler_paths(void)
{
    http_handler_context_t ctx;

    boot_fresh();
    g_service_running = true;
    run_config_and_auth();

    ctx = make_json_ctx("POST", "{\"password\":\"handler-pw-1\"}");
    e2e_reset_response();
    CHECK(change_password_handler(&ctx) == AICAM_ERROR);
    CHECK(strcmp(g_last_response_kind, "error") == 0);
    CHECK(g_last_response_code == API_ERROR_UNAUTHORIZED);
    CHECK(auth_mgr_verify_password("handler-pw-1") == AICAM_FALSE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_TRUE);

    ctx = make_json_ctx("POST", "{\"password\":\"short\"}");
    ctx.authenticated = AICAM_TRUE;
    e2e_reset_response();
    CHECK(change_password_handler(&ctx) == AICAM_ERROR);
    CHECK(strcmp(g_last_response_kind, "error") == 0);
    CHECK(g_last_response_code == API_ERROR_INVALID_REQUEST);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_TRUE);

    ctx = make_json_ctx("POST", "{}");
    ctx.authenticated = AICAM_TRUE;
    e2e_reset_response();
    CHECK(change_password_handler(&ctx) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_INVALID_REQUEST);

    ctx = make_json_ctx("POST", "{\"password\":\"handler-pw-1\"}");
    ctx.authenticated = AICAM_TRUE;
    e2e_reset_response();
    CHECK(change_password_handler(&ctx) == AICAM_OK);
    CHECK(strcmp(g_last_response_kind, "success") == 0);
    CHECK(strcmp(g_last_response_message, "Password changed successfully") == 0);
    CHECK(auth_mgr_verify_password("handler-pw-1") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);

    ctx = make_json_ctx("POST", "{\"password\":\"handler-pw-1\"}");
    e2e_reset_response();
    CHECK(login_handler(&ctx) == AICAM_OK);
    CHECK(strcmp(g_last_response_kind, "success") == 0);

    ctx = make_json_ctx("POST", "{\"password\":\"hicamthink\"}");
    e2e_reset_response();
    CHECK(login_handler(&ctx) == AICAM_ERROR);
    CHECK(strcmp(g_last_response_kind, "error") == 0);
    CHECK(g_last_response_code == API_BUSINESS_ERROR_INVALID_PASSWORD);

    ctx = make_json_ctx("GET", NULL);
    e2e_reset_response();
    CHECK(change_password_handler(&ctx) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_METHOD_NOT_ALLOWED);

    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_session_expiry_revocation_and_limits(void)
{
    uint32_t session_a;
    uint32_t session_b;
    uint32_t session_c;
    uint32_t session_d;
    uint32_t session_e;
    int iterations;
    auth_result_t rc;

    boot_fresh();
    run_config_and_auth();

    CHECK(auth_mgr_login("admin", "hicamthink", &session_a) == AUTH_RESULT_SUCCESS);
    CHECK(auth_mgr_validate_session(session_a, NULL) == AUTH_RESULT_SUCCESS);

    rc = AUTH_RESULT_SUCCESS;
    for (iterations = 0; iterations < 5000; iterations++) {
        rc = auth_mgr_validate_session(1, NULL);
        if (rc != AUTH_RESULT_SESSION_NOT_FOUND) {
            break;
        }
    }
    CHECK(rc == AUTH_RESULT_SESSION_NOT_FOUND);
    CHECK(iterations >= 1500);
    CHECK(auth_mgr_validate_session(session_a, NULL) != AUTH_RESULT_SUCCESS);

    CHECK(auth_mgr_login("admin", "hicamthink", &session_a) == AUTH_RESULT_SUCCESS);
    CHECK(auth_mgr_login("admin", "hicamthink", &session_b) == AUTH_RESULT_SUCCESS);
    CHECK(auth_mgr_login("admin", "hicamthink", &session_c) == AUTH_RESULT_SUCCESS);
    CHECK(auth_mgr_login("admin", "hicamthink", &session_d) == AUTH_RESULT_SUCCESS);
    CHECK(auth_mgr_login("admin", "hicamthink", &session_e) == AUTH_RESULT_MAX_SESSIONS_REACHED);

    CHECK(auth_mgr_logout(session_b) == AUTH_RESULT_SUCCESS);
    CHECK(auth_mgr_validate_session(session_b, NULL) == AUTH_RESULT_SESSION_NOT_FOUND);
    CHECK(auth_mgr_logout(session_b) == AUTH_RESULT_SESSION_NOT_FOUND);
    CHECK(auth_mgr_validate_session(session_a, NULL) == AUTH_RESULT_SUCCESS);

    CHECK(auth_mgr_change_password("revoked-pw-123") == AUTH_RESULT_SUCCESS);
    CHECK(auth_mgr_validate_session(session_a, NULL) == AUTH_RESULT_SESSION_NOT_FOUND);
    CHECK(auth_mgr_validate_session(session_c, NULL) == AUTH_RESULT_SESSION_NOT_FOUND);
    CHECK(auth_mgr_validate_session(session_d, NULL) == AUTH_RESULT_SESSION_NOT_FOUND);

    CHECK(auth_mgr_login("admin", "hicamthink", &session_a) == AUTH_RESULT_INVALID_CREDENTIALS);
    CHECK(auth_mgr_login("admin", "revoked-pw-123", &session_a) == AUTH_RESULT_SUCCESS);

    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_weak_hash_and_corrupt_record_rejected(void)
{
    char weak_hash_hex[65];
    aicam_global_config_t cfg;

    legacy_weak_hash("victim-pw", weak_hash_hex);

    boot_fresh();
    run_config_and_auth();
    CHECK(auth_mgr_change_password("victim-pw") == AUTH_RESULT_SUCCESS);
    CHECK(storage_nvs_write(NVS_USER, "auth_password", weak_hash_hex, strlen(weak_hash_hex) + 1) > 0);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);

    CHECK(e2e_storage_boot() == 0);
    run_config_and_auth();
    CHECK(auth_mgr_verify_password("victim-pw") == AICAM_FALSE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    CHECK(auth_mgr_verify_password(weak_hash_hex) == AICAM_FALSE);
    CHECK(auth_mgr_authorize("admin", "victim-pw").authenticated == AICAM_FALSE);
    CHECK(auth_mgr_change_password("recovered-pw-1") == AUTH_RESULT_SUCCESS);
    CHECK(auth_mgr_verify_password("recovered-pw-1") == AICAM_TRUE);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);

    boot_fresh();
    run_config_and_auth();
    CHECK(auth_mgr_change_password("trusted-pw-123") == AUTH_RESULT_SUCCESS);
    CHECK(storage_nvs_write(NVS_USER, "auth_password", "A1:00:01", 9) > 0);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);

    CHECK(e2e_storage_boot() == 0);
    CHECK(json_config_load_from_nvs(&cfg) != AICAM_OK);
    CHECK(json_config_mgr_init() == AICAM_OK);
    CHECK(json_config_mgr_credential_trusted() == AICAM_FALSE);
    CHECK(auth_mgr_init() == AICAM_OK);
    CHECK(json_config_get_device_credential(weak_hash_hex, sizeof(weak_hash_hex)) != AICAM_OK);
    CHECK(auth_mgr_verify_password("trusted-pw-123") == AICAM_FALSE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    CHECK(auth_mgr_verify_password("A1:00:01") == AICAM_FALSE);
    CHECK(auth_mgr_authorize("admin", "hicamthink").authenticated == AICAM_FALSE);
    CHECK(auth_mgr_change_password("repaired-pw-1") == AUTH_RESULT_INTERNAL_ERROR);
    CHECK(json_config_set_device_password("repaired-pw-1") != AICAM_OK);

    CHECK(json_config_reset_to_default(NULL) == AICAM_OK);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
    CHECK(e2e_storage_boot() == 0);
    run_config_and_auth();
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_TRUE);
    CHECK(auth_mgr_default_password_active() == AICAM_TRUE);
    CHECK(auth_mgr_authorize("admin", "hicamthink").high_privilege_allowed == AICAM_FALSE);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_sensitive_key_predicate(void)
{
    CHECK(json_config_nvs_key_is_sensitive("auth_password") == 1);
    CHECK(json_config_nvs_key_is_sensitive("dev_info_password") == 1);
    CHECK(json_config_nvs_key_is_sensitive("AUTH_PASSWORD") == 0);
    CHECK(json_config_nvs_key_is_sensitive("auth_password_extra") == 0);
    CHECK(json_config_nvs_key_is_sensitive("log_level") == 0);
    CHECK(json_config_nvs_key_is_sensitive("") == 0);
    CHECK(json_config_nvs_key_is_sensitive(NULL) == 0);
}

static void test_legacy_upgrade_rejected_when_unpersistable(void)
{
    char credential[AUTH_CREDENTIAL_RECORD_MAX];

    boot_fresh();
    run_config_and_auth();
    CHECK(auth_mgr_change_password("legacy-keep-1") == AUTH_RESULT_SUCCESS);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
    CHECK(storage_nvs_write(NVS_USER, "dev_info_password", "legacy-keep-1", 14) > 0);
    CHECK(storage_nvs_delete(NVS_USER, "auth_password") == 0);

    g_e2e_user_ff.fail_next_writes = 1000000;
    CHECK(e2e_storage_boot() == 0);
    CHECK(json_config_load_from_nvs(&(aicam_global_config_t){0}) != AICAM_OK);
    CHECK(json_config_mgr_init() == AICAM_OK);
    CHECK(json_config_mgr_credential_trusted() == AICAM_FALSE);
    CHECK(auth_mgr_init() == AICAM_OK);
    CHECK(auth_mgr_verify_password("legacy-keep-1") == AICAM_FALSE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    g_e2e_user_ff.fail_next_writes = 0;
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);

    CHECK(e2e_storage_boot() == 0);
    CHECK(json_config_load_from_nvs(&(aicam_global_config_t){0}) == AICAM_OK);
    run_config_and_auth();
    CHECK(auth_mgr_verify_password("legacy-keep-1") == AICAM_TRUE);
    memset(credential, 0, sizeof(credential));
    CHECK(json_config_get_device_credential(credential, sizeof(credential)) == AICAM_OK);
    CHECK(strncmp(credential, "A1:", 3) == 0);
    CHECK(storage_nvs_read(NVS_USER, "dev_info_password", credential, sizeof(credential)) == -2);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    test_credential_record_codec();
    test_default_password_persisted_as_record();
    test_export_contains_no_admin_secret();
    test_verdict_default_gate_matrix();
    test_untrusted_media_authorize_failclosed();
    test_persist_failure_no_fake_success();
    test_import_cannot_change_identity();
    test_set_config_persist_failure_propagates();
    test_change_password_handler_paths();
    test_session_expiry_revocation_and_limits();
    test_weak_hash_and_corrupt_record_rejected();
    test_sensitive_key_predicate();
    test_legacy_upgrade_rejected_when_unpersistable();
    printf("admin auth checks=%d failures=%d\n", g_total, g_fail);
    return g_fail == 0 ? 0 : 1;
}
