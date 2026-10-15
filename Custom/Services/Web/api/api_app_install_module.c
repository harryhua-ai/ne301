#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "api_app_install_module.h"

#include "web_api.h"
#include "cJSON.h"
#include "debug.h"
#include "aicam_types.h"

#include "app_installer.h"
#include "app_host.h"
#include "app_host_run.h"
#include "app_trust_anchor.h"
#include "app_admission.h"
#include "app_host_abi.h"
#include "app_host_control.h"
#include "app_sidecar_lifecycle.h"

#define APP_INSTALL_HEX_PER_BYTE 2

typedef struct {
    app_admission_policy_t policy;
    app_admission_policy_t policy_v2;
    app_admission_policy_t policy_v3;
    app_admission_trust_view_t trust;
} api_app_install_verify_ctx_t;

static const api_route_t g_app_install_routes[] = {
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/app/install",
        .handler = app_install_package_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "GET",
        .path = API_PATH_PREFIX "/app/installed",
        .handler = app_install_state_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/app/sidecar/install",
        .handler = app_sidecar_install_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "GET",
        .path = API_PATH_PREFIX "/app/sidecar/installed",
        .handler = app_sidecar_state_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "DELETE",
        .path = API_PATH_PREFIX "/app/installed",
        .handler = app_install_remove_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/app/run",
        .handler = app_install_run_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    }
};

static app_admission_result_t api_app_install_verify(const uint8_t *data, size_t len,
                                                     app_admission_verdict_t *out,
                                                     void *ctx)
{
    api_app_install_verify_ctx_t *vctx = (api_app_install_verify_ctx_t *)ctx;
    uint32_t container = app_admission_container_version(data, len);

    if (container == 2u) {
        return app_admission_verify_package(data, len, &vctx->policy_v2, &vctx->trust, out);
    }
    if (container == 3u) {
        return app_admission_verify_package(data, len, &vctx->policy_v3, &vctx->trust, out);
    }
    return app_admission_verify_package(data, len, &vctx->policy, &vctx->trust, out);
}

static aicam_bool_t api_app_install_fill_ctx(api_app_install_verify_ctx_t *vctx)
{
    const app_trust_anchor_t *anchor;
    uint32_t base = 0;
    uint32_t size = 0;

    memset(vctx, 0, sizeof(*vctx));
    if (app_trust_anchor_status() != APP_TRUST_ANCHOR_PROVISIONED) {
        return AICAM_FALSE;
    }
    anchor = app_trust_anchor_get();
    vctx->trust.publisher_id = anchor->publisher_id;
    vctx->trust.publisher_id_len = anchor->publisher_id_len;
    vctx->trust.spki_der = anchor->spki_der;
    vctx->trust.spki_der_len = anchor->spki_der_len;

    vctx->policy.target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    vctx->policy.psram_mib = 64u;
    vctx->policy.abi_version = APP_HOST_ABI_VERSION;
    vctx->policy.supported_caps_mask = APP_ADMISSION_KNOWN_CAPS_MASK;
    vctx->policy.max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN_V2;

    vctx->policy_v2.target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    vctx->policy_v2.psram_mib = 64u;
    vctx->policy_v2.abi_version = APP_ADMISSION_ABI_VERSION_V2;
    vctx->policy_v2.supported_caps_mask = APP_ADMISSION_CAP_LOG |
                                          APP_ADMISSION_CAP_TICK_MS |
                                          APP_ADMISSION_V2_CAP_AI_EVENTS |
                                          APP_ADMISSION_V2_CAP_SESSION_LIFECYCLE;
    vctx->policy_v2.max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN_V2;
    vctx->policy_v2.event_capacity = 16384u;
    vctx->policy_v2.report_capacity = 0u;
    vctx->policy_v2.state_quota_capacity = 0u;

    vctx->policy_v3.target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    vctx->policy_v3.psram_mib = 64u;
    vctx->policy_v3.abi_version = APP_ADMISSION_ABI_VERSION_V3;
    vctx->policy_v3.supported_caps_mask = APP_ADMISSION_CAP_LOG |
                                          APP_ADMISSION_CAP_TICK_MS |
                                          APP_ADMISSION_V2_CAP_AI_EVENTS |
                                          APP_ADMISSION_V2_CAP_APP_STATE |
                                          APP_ADMISSION_V2_CAP_SESSION_LIFECYCLE |
                                          APP_ADMISSION_V3_CAP_CONTROL_RECEIVE |
                                          APP_ADMISSION_V3_CAP_BUSINESS_SNAPSHOT;
    vctx->policy_v3.max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN_V3;
    vctx->policy_v3.event_capacity = 16384u;
    vctx->policy_v3.report_capacity = 0u;
    vctx->policy_v3.state_quota_capacity = 8192u;
    vctx->policy_v3.control_capacity = APP_CONTROL_MSG_RAM_MAX;
    vctx->policy_v3.snapshot_capacity = APP_CONTROL_SNAPSHOT_RAM_MAX;
    vctx->policy_v3.abi_table_size = APP_ADMISSION_V3_ABI_TABLE_SIZE;
    vctx->policy_v3.v3_business_profile = APP_ADMISSION_V3_BUSINESS_GENERIC_FULL;

    if (app_host_exec_region(&base, &size) == AICAM_TRUE) {
        vctx->policy.exec_region_base = base;
        vctx->policy.exec_region_size = size;
        vctx->policy_v2.exec_region_base = base;
        vctx->policy_v2.exec_region_size = size;
    }
    return AICAM_TRUE;
}

cJSON *app_install_record_json(const app_install_record_t *record,
                               const char *status)
{
    cJSON *root = cJSON_CreateObject();
    char pkg_hex[APP_INSTALL_SHA256_LEN * APP_INSTALL_HEX_PER_BYTE + 1];
    char tbs_hex[APP_INSTALL_SHA256_LEN * APP_INSTALL_HEX_PER_BYTE + 1];
    size_t i;

    if (root == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "status", status);
    cJSON_AddStringToObject(root, "publisher_id", record->publisher_id);
    cJSON_AddStringToObject(root, "app_id", record->app_id);
    cJSON_AddNumberToObject(root, "version_major", (double)record->version_major);
    cJSON_AddNumberToObject(root, "version_minor", (double)record->version_minor);
    cJSON_AddNumberToObject(root, "version_patch", (double)record->version_patch);
    cJSON_AddNumberToObject(root, "package_len", (double)record->package_len);

    for (i = 0; i < APP_INSTALL_SHA256_LEN; i++) {
        snprintf(&pkg_hex[i * 2], 3, "%02x", record->package_sha256[i]);
        snprintf(&tbs_hex[i * 2], 3, "%02x", record->tbs_sha256[i]);
    }
    cJSON_AddStringToObject(root, "package_sha256", pkg_hex);
    cJSON_AddStringToObject(root, "tbs_sha256", tbs_hex);
    cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
    return root;
}

static cJSON *app_install_refused_json(app_install_result_t result)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "status", "refused");
    cJSON_AddStringToObject(root, "reason", app_install_result_str(result));
    cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
    return root;
}

static aicam_result_t app_install_respond_json(http_handler_context_t *ctx, cJSON *root,
                                               const char *message)
{
    char *response_text;
    aicam_result_t rc;

    response_text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (response_text == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    rc = api_response_success(ctx, response_text, message);
    if (rc != AICAM_OK) {
        free(response_text);
    }
    return rc;
}

static void app_install_state_add_running(cJSON *root)
{
    if (root == NULL) {
        return;
    }
    cJSON_AddBoolToObject(root, "app_running",
                          (app_install_app_running() == AICAM_TRUE) ? AICAM_TRUE : AICAM_FALSE);
}

aicam_result_t app_install_package_handler(http_handler_context_t *ctx)
{
    api_app_install_verify_ctx_t vctx;
    const app_install_io_t *io;
    cJSON *request_json = NULL;
    cJSON *response_json = NULL;
    cJSON *b64_item;
    size_t b64_len;
    size_t decoded_cap;
    size_t decoded_len;
    uint8_t *package = NULL;
    app_install_record_t record;
    app_install_result_t result;

    if (ctx == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }

    if (web_server_high_privilege_allowed(ctx) != AICAM_TRUE) {
        LOG_SVC_WARN("app install: refused, high-privilege admin authentication required");
        return api_response_error(ctx, API_ERROR_FORBIDDEN,
                                  "High-privilege administrator authentication required");
    }

    if (!api_app_install_fill_ctx(&vctx)) {
        response_json = app_install_refused_json(APP_INSTALL_ERR_TRUST_UNPROVEN);
        if (response_json == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        return app_install_respond_json(ctx, response_json,
                                        "App install completed with result");
    }

    request_json = web_api_parse_body(ctx);
    if (request_json == NULL) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request body");
    }

    b64_item = cJSON_GetObjectItem(request_json, "package_base64");
    if (b64_item == NULL || !cJSON_IsString(b64_item) || b64_item->valuestring == NULL) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Missing package_base64 field");
    }

    b64_len = strlen(b64_item->valuestring);
    if (b64_len == 0u || b64_len > API_APP_INSTALL_MAX_B64_CHARS) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "package_base64 length out of bounds");
    }

    decoded_cap = (b64_len / 4u) * 3u + 3u;
    if ((uint64_t)((b64_len / 4u) * 3u) > (uint64_t)APP_ADMISSION_MAX_PACKAGE_LEN_V2) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                  "package exceeds protocol size limit");
    }

    package = (uint8_t *)malloc(decoded_cap);
    if (package == NULL) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    decoded_len = web_api_base64_decode(b64_item->valuestring, package, decoded_cap);
    cJSON_Delete(request_json);
    request_json = NULL;

    if (decoded_len == 0u) {
        free(package);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "package_base64 decode failed");
    }

    io = app_installer_device_io();
    if (io == NULL) {
        free(package);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "App installer storage unavailable");
    }

    memset(&record, 0, sizeof(record));
    result = app_install(io, api_app_install_verify, &vctx, package, decoded_len, &record);
    free(package);
    package = NULL;

    if (result == APP_INSTALL_OK || result == APP_INSTALL_OK_ALREADY) {
        LOG_SVC_INFO("app install: %s publisher=%s app=%s v%u.%u.%u",
                     app_install_result_str(result), record.publisher_id, record.app_id,
                     record.version_major, record.version_minor, record.version_patch);
        response_json = app_install_record_json(&record,
                                                (result == APP_INSTALL_OK_ALREADY)
                                                    ? "already_installed"
                                                    : "installed");
    } else {
        if (result == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN) {
            LOG_SVC_ERROR("app install: storage state UNKNOWN, fail-closed, reboot resolves pending intent");
        } else {
            LOG_SVC_WARN("app install: refused reason=%s", app_install_result_str(result));
        }
        response_json = app_install_refused_json(result);
    }

    if (response_json == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    return app_install_respond_json(ctx, response_json, "App install completed with result");
}

aicam_result_t app_install_state_handler(http_handler_context_t *ctx)
{
    api_app_install_verify_ctx_t vctx;
    const app_install_io_t *io;
    cJSON *root = NULL;
    app_install_query_t query;
    app_install_result_t result;
    aicam_bool_t ctx_ok;
    app_uninstall_state_t uninstall_state;

    if (ctx == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET method is allowed");
    }

    if (web_server_high_privilege_allowed(ctx) != AICAM_TRUE) {
        LOG_SVC_WARN("app install: query refused, high-privilege admin authentication required");
        return api_response_error(ctx, API_ERROR_FORBIDDEN,
                                  "High-privilege administrator authentication required");
    }

    io = app_installer_device_io();
    if (io == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "App installer storage unavailable");
    }

    result = app_install_uninstall_status(io, &uninstall_state, NULL);
    if (result != APP_INSTALL_OK || uninstall_state == APP_UNINSTALL_STATE_UNKNOWN) {
        root = cJSON_CreateObject();
        if (root == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(root, "state", "unknown");
        cJSON_AddStringToObject(root, "reason", "UNINSTALL_STATUS_UNKNOWN");
        cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
        app_install_state_add_running(root);
        return app_install_respond_json(ctx, root, "App install query result");
    }
    if (uninstall_state == APP_UNINSTALL_STATE_CLEANUP_PENDING) {
        root = cJSON_CreateObject();
        if (root == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(root, "state", "cleanup_pending");
        cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
        app_install_state_add_running(root);
        return app_install_respond_json(ctx, root, "App install query result");
    }

    ctx_ok = api_app_install_fill_ctx(&vctx);
    if (ctx_ok != AICAM_TRUE) {
        app_install_record_t committed;
        aicam_bool_t present = AICAM_FALSE;
        app_install_result_t committed_rc = app_install_query_committed(io, &committed, &present);

        if (committed_rc != APP_INSTALL_OK) {
            root = cJSON_CreateObject();
            if (root == NULL) {
                return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
            }
            cJSON_AddStringToObject(root, "state", "unknown");
            cJSON_AddStringToObject(root, "reason", app_install_result_str(committed_rc));
            cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
            app_install_state_add_running(root);
            return app_install_respond_json(ctx, root, "App install query result");
        }
        if (present != AICAM_TRUE) {
            root = cJSON_CreateObject();
            if (root == NULL) {
                return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
            }
            cJSON_AddStringToObject(root, "state", "none");
            cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
            app_install_state_add_running(root);
            return app_install_respond_json(ctx, root, "App install query result");
        }
        root = app_install_record_json(&committed, "unavailable");
        if (root == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(root, "reason", "TRUST_UNPROVEN");
        app_install_state_add_running(root);
        return app_install_respond_json(ctx, root, "App install query result");
    }

    memset(&query, 0, sizeof(query));
    result = app_install_query(io, api_app_install_verify, &vctx, &query);
    if (result != APP_INSTALL_OK) {
        root = cJSON_CreateObject();
        if (root == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(root, "state", "unknown");
        cJSON_AddStringToObject(root, "reason", app_install_result_str(result));
        cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
        app_install_state_add_running(root);
        return app_install_respond_json(ctx, root, "App install query result");
    }

    if (query.state == APP_INSTALL_STATE_NONE) {
        root = cJSON_CreateObject();
        if (root == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(root, "state", "none");
        cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
        app_install_state_add_running(root);
        return app_install_respond_json(ctx, root, "App install query result");
    }

    root = app_install_record_json(&query.record, app_install_state_str(query.state));
    if (root == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    if (query.state == APP_INSTALL_STATE_UNAVAILABLE) {
        cJSON_AddStringToObject(root, "reason", query.reason);
    }
    app_install_state_add_running(root);
    return app_install_respond_json(ctx, root, "App install query result");
}

static void api_sidecar_hex(const uint8_t *data, size_t len, char *out)
{
    size_t i;

    for (i = 0u; i < len; i++) {
        (void)snprintf(out + (i * 2u), 3u, "%02x", data[i]);
    }
    out[len * 2u] = '\0';
}

static aicam_bool_t api_sidecar_fill_trust(const app_install_record_t *native,
                                           app_sidecar_trust_t *trust)
{
    const app_trust_anchor_t *anchor;

    if (native == NULL || trust == NULL ||
        app_trust_anchor_status() != APP_TRUST_ANCHOR_PROVISIONED) {
        return AICAM_FALSE;
    }
    anchor = app_trust_anchor_get();
    if (anchor == NULL || anchor->publisher_id == NULL || anchor->spki_der == NULL) {
        return AICAM_FALSE;
    }
    memset(trust, 0, sizeof(*trust));
    trust->publisher_id = anchor->publisher_id;
    trust->publisher_id_len = anchor->publisher_id_len;
    trust->app_id = native->app_id;
    trust->app_id_len = strlen(native->app_id);
    trust->spki_der = anchor->spki_der;
    trust->spki_der_len = anchor->spki_der_len;
    trust->native_tbs_sha256 = native->tbs_sha256;
    return AICAM_TRUE;
}

static cJSON *api_sidecar_descriptor_json(const app_sidecar_descriptor_t *descriptor,
                                         const char *status)
{
    cJSON *root;
    cJSON *files;
    char key_hex[APP_SIDECAR_SHA256_LEN * 2u + 1u];
    char native_hex[APP_SIDECAR_SHA256_LEN * 2u + 1u];
    char ui_hex[APP_SIDECAR_SHA256_LEN * 2u + 1u];
    char generation[24];
    uint16_t i;

    if (descriptor == NULL) {
        return NULL;
    }
    root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "status", status);
    cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
    cJSON_AddStringToObject(root, "publisher_id", descriptor->publisher_id);
    cJSON_AddStringToObject(root, "app_id", descriptor->app_id);
    cJSON_AddNumberToObject(root, "version_major", descriptor->version_major);
    cJSON_AddNumberToObject(root, "version_minor", descriptor->version_minor);
    cJSON_AddNumberToObject(root, "version_patch", descriptor->version_patch);
    cJSON_AddNumberToObject(root, "host_ui_abi", descriptor->host_ui_abi);
    cJSON_AddNumberToObject(root, "bundle_len", descriptor->bundle_len);
    cJSON_AddNumberToObject(root, "manifest_len", descriptor->manifest_len);
    cJSON_AddNumberToObject(root, "asset_payload_len", descriptor->asset_payload_len);
    cJSON_AddNumberToObject(root, "file_count", descriptor->file_count);
    api_sidecar_hex(descriptor->publisher_key_sha256,
                    APP_SIDECAR_SHA256_LEN, key_hex);
    api_sidecar_hex(descriptor->native_tbs_sha256,
                    APP_SIDECAR_SHA256_LEN, native_hex);
    api_sidecar_hex(descriptor->ui_tbs_sha256,
                    APP_SIDECAR_SHA256_LEN, ui_hex);
    cJSON_AddStringToObject(root, "publisher_key_sha256", key_hex);
    cJSON_AddStringToObject(root, "native_tbs_sha256", native_hex);
    cJSON_AddStringToObject(root, "ui_tbs_sha256", ui_hex);
    (void)snprintf(generation, sizeof(generation), "%llu",
                   (unsigned long long)descriptor->service_generation);
    cJSON_AddStringToObject(root, "service_generation", generation);
    files = cJSON_CreateArray();
    if (files == NULL) {
        cJSON_Delete(root);
        return NULL;
    }
    for (i = 0u; i < descriptor->file_count; i++) {
        cJSON *item = cJSON_CreateObject();
        char hash[APP_SIDECAR_SHA256_LEN * 2u + 1u];
        if (item == NULL) {
            cJSON_Delete(files);
            cJSON_Delete(root);
            return NULL;
        }
        api_sidecar_hex(descriptor->files[i].sha256,
                        APP_SIDECAR_SHA256_LEN, hash);
        cJSON_AddStringToObject(item, "path", descriptor->files[i].path);
        cJSON_AddStringToObject(item, "storage_path", descriptor->files[i].storage_path);
        cJSON_AddNumberToObject(item, "file_len", descriptor->files[i].file_len);
        cJSON_AddStringToObject(item, "sha256", hash);
        cJSON_AddItemToArray(files, item);
    }
    cJSON_AddItemToObject(root, "files", files);
    return root;
}

static cJSON *api_sidecar_state_json(app_sidecar_lifecycle_state_t state,
                                     const char *reason)
{
    cJSON *root = cJSON_CreateObject();

    if (root == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "state", app_sidecar_lifecycle_state_str(state));
    if (reason != NULL) {
        cJSON_AddStringToObject(root, "reason", reason);
    }
    cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
    return root;
}

aicam_result_t app_sidecar_install_handler(http_handler_context_t *ctx)
{
    const app_install_io_t *io;
    app_install_record_t native;
    app_sidecar_trust_t trust;
    app_sidecar_descriptor_t descriptor;
    app_sidecar_lifecycle_state_t state;
    app_sidecar_lifecycle_result_t result;
    cJSON *request_json;
    cJSON *root;
    cJSON *bundle_item;
    const char *encoded;
    size_t encoded_len;
    size_t decoded_cap;
    size_t decoded_len;
    aicam_bool_t present = AICAM_FALSE;
    uint8_t *bundle;
    app_install_result_t native_rc;

    if (ctx == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED,
                                  "Only POST method is allowed");
    }
    if (web_server_high_privilege_allowed(ctx) != AICAM_TRUE) {
        return api_response_error(ctx, API_ERROR_FORBIDDEN,
                                  "High-privilege administrator authentication required");
    }
    request_json = web_api_parse_body(ctx);
    if (request_json == NULL) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                  "Invalid JSON request body");
    }
    bundle_item = cJSON_GetObjectItem(request_json, "bundle_base64");
    if (bundle_item == NULL || !cJSON_IsString(bundle_item) ||
        bundle_item->valuestring == NULL) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                  "Missing bundle_base64 field");
    }
    encoded = bundle_item->valuestring;
    encoded_len = strlen(encoded);
    if (encoded_len == 0u || encoded_len > API_APP_INSTALL_MAX_B64_CHARS) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                  "bundle_base64 length out of bounds");
    }
    decoded_cap = (encoded_len / 4u) * 3u + 3u;
    if ((uint64_t)((encoded_len / 4u) * 3u) >
        (uint64_t)APP_SIDECAR_LIFECYCLE_BUNDLE_MAX) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                  "bundle exceeds Sidecar v1 size limit");
    }
    bundle = (uint8_t *)malloc(decoded_cap);
    if (bundle == NULL) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    decoded_len = web_api_base64_decode(encoded, bundle, decoded_cap);
    cJSON_Delete(request_json);
    if (decoded_len == 0u || decoded_len > APP_SIDECAR_LIFECYCLE_BUNDLE_MAX) {
        free(bundle);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                  "bundle_base64 decode failed or exceeded the size limit");
    }
    io = app_installer_device_io();
    if (io == NULL) {
        free(bundle);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR,
                                  "App installer storage unavailable");
    }
    native_rc = app_install_query_committed(io, &native, &present);
    if (native_rc != APP_INSTALL_OK || present != AICAM_TRUE ||
        api_sidecar_fill_trust(&native, &trust) != AICAM_TRUE) {
        free(bundle);
        root = api_sidecar_state_json(APP_SIDECAR_LIFECYCLE_UNAVAILABLE,
                                      (native_rc == APP_INSTALL_OK && present != AICAM_TRUE)
                                          ? "NATIVE_NOT_INSTALLED" : "TRUST_UNPROVEN");
        if (root == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        return app_install_respond_json(ctx, root, "Sidecar install result");
    }
    memset(&descriptor, 0, sizeof(descriptor));
    result = app_sidecar_lifecycle_install(io, &trust, &native, bundle, decoded_len,
                                           &descriptor, &state);
    free(bundle);
    if (result == APP_SIDECAR_LIFECYCLE_OK ||
        result == APP_SIDECAR_LIFECYCLE_ALREADY) {
        root = api_sidecar_descriptor_json(&descriptor,
                                           app_sidecar_lifecycle_state_str(state));
    } else {
        root = api_sidecar_state_json(state, app_sidecar_lifecycle_result_str(result));
    }
    if (root == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    return app_install_respond_json(ctx, root, "Sidecar install result");
}

aicam_result_t app_sidecar_state_handler(http_handler_context_t *ctx)
{
    const app_install_io_t *io;
    app_install_record_t native;
    app_sidecar_trust_t trust;
    app_sidecar_descriptor_t descriptor;
    app_sidecar_lifecycle_state_t state;
    app_sidecar_lifecycle_result_t result;
    app_uninstall_state_t uninstall_state;
    app_install_result_t native_rc;
    aicam_bool_t present = AICAM_FALSE;
    cJSON *root;

    if (ctx == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED,
                                  "Only GET method is allowed");
    }
    if (web_server_high_privilege_allowed(ctx) != AICAM_TRUE) {
        return api_response_error(ctx, API_ERROR_FORBIDDEN,
                                  "High-privilege administrator authentication required");
    }
    io = app_installer_device_io();
    if (io == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR,
                                  "App installer storage unavailable");
    }
    if (app_install_uninstall_status(io, &uninstall_state, NULL) != APP_INSTALL_OK ||
        uninstall_state == APP_UNINSTALL_STATE_UNKNOWN) {
        root = api_sidecar_state_json(APP_SIDECAR_LIFECYCLE_UNKNOWN,
                                      "UNINSTALL_STATUS_UNKNOWN");
        return (root == NULL)
                   ? api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory")
                   : app_install_respond_json(ctx, root, "Sidecar query result");
    }
    if (uninstall_state == APP_UNINSTALL_STATE_CLEANUP_PENDING) {
        root = api_sidecar_state_json(APP_SIDECAR_LIFECYCLE_UNAVAILABLE,
                                      "CLEANUP_PENDING");
        return (root == NULL)
                   ? api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory")
                   : app_install_respond_json(ctx, root, "Sidecar query result");
    }
    native_rc = app_install_query_committed(io, &native, &present);
    if (native_rc != APP_INSTALL_OK) {
        root = api_sidecar_state_json(APP_SIDECAR_LIFECYCLE_UNKNOWN,
                                      app_install_result_str(native_rc));
        return (root == NULL)
                   ? api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory")
                   : app_install_respond_json(ctx, root, "Sidecar query result");
    }
    if (present != AICAM_TRUE) {
        root = api_sidecar_state_json(APP_SIDECAR_LIFECYCLE_NONE, NULL);
        return (root == NULL)
                   ? api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory")
                   : app_install_respond_json(ctx, root, "Sidecar query result");
    }
    if (api_sidecar_fill_trust(&native, &trust) != AICAM_TRUE) {
        root = api_sidecar_state_json(APP_SIDECAR_LIFECYCLE_UNAVAILABLE,
                                      "TRUST_UNPROVEN");
        return (root == NULL)
                   ? api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory")
                   : app_install_respond_json(ctx, root, "Sidecar query result");
    }
    memset(&descriptor, 0, sizeof(descriptor));
    result = app_sidecar_lifecycle_query(io, &trust, &native, &descriptor, &state);
    if (result == APP_SIDECAR_LIFECYCLE_OK &&
        state == APP_SIDECAR_LIFECYCLE_ACTIVE) {
        root = api_sidecar_descriptor_json(&descriptor,
                                           app_sidecar_lifecycle_state_str(state));
    } else {
        root = api_sidecar_state_json(state, (result == APP_SIDECAR_LIFECYCLE_OK)
                                               ? NULL
                                               : app_sidecar_lifecycle_result_str(result));
    }
    return (root == NULL)
               ? api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory")
               : app_install_respond_json(ctx, root, "Sidecar query result");
}

aicam_result_t app_install_remove_handler(http_handler_context_t *ctx)
{
    const app_install_io_t *io;
    cJSON *response_json = NULL;
    cJSON *request_json;
    cJSON *confirm_item;
    app_install_record_t removed;
    app_uninstall_result_t result;
    app_uninstall_state_t state;
    aicam_bool_t confirmed;

    if (ctx == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (!web_api_verify_method(ctx, "DELETE")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only DELETE method is allowed");
    }

    if (web_server_high_privilege_allowed(ctx) != AICAM_TRUE) {
        LOG_SVC_WARN("app uninstall: refused, high-privilege admin authentication required");
        return api_response_error(ctx, API_ERROR_FORBIDDEN,
                                  "High-privilege administrator authentication required");
    }

    request_json = web_api_parse_body(ctx);
    if (request_json == NULL) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                  "Invalid JSON request body");
    }
    confirm_item = cJSON_GetObjectItem(request_json, "confirm_delete_data");
    if (confirm_item == NULL) {
        confirmed = AICAM_FALSE;
    } else if (!cJSON_IsBool(confirm_item)) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST,
                                  "confirm_delete_data must be a boolean");
    } else {
        confirmed = cJSON_IsTrue(confirm_item) ? AICAM_TRUE : AICAM_FALSE;
    }
    cJSON_Delete(request_json);
    if (confirmed != AICAM_TRUE) {
        response_json = cJSON_CreateObject();
        if (response_json == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(response_json, "status", "cancelled");
        cJSON_AddBoolToObject(response_json, "side_effects", AICAM_FALSE);
        cJSON_AddBoolToObject(response_json, "executable", AICAM_FALSE);
        return app_install_respond_json(ctx, response_json,
                                       "App uninstall cancelled");
    }

    io = app_installer_device_io();
    if (io == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "App installer storage unavailable");
    }

    memset(&removed, 0, sizeof(removed));
    state = APP_UNINSTALL_STATE_NONE;
    result = app_install_uninstall_confirmed(
        io, AICAM_TRUE, app_installer_device_cleanup_ops(), &removed, &state);

    if (result == APP_UNINSTALL_OK) {
        LOG_SVC_INFO("app uninstall: removed publisher=%s app=%s v%u.%u.%u",
                     removed.publisher_id, removed.app_id,
                     removed.version_major, removed.version_minor, removed.version_patch);
        response_json = app_install_record_json(&removed, "uninstalled");
    } else if (result == APP_UNINSTALL_CLEANUP_PENDING) {
        response_json = app_install_record_json(&removed, "cleanup_pending");
        if (response_json != NULL) {
            cJSON_AddStringToObject(response_json, "reason", "PURGE_UNCONFIRMED_OR_STORAGE_UNKNOWN");
        }
    } else {
        response_json = cJSON_CreateObject();
        if (response_json != NULL) {
            cJSON_AddStringToObject(response_json, "status", "refused");
            cJSON_AddStringToObject(response_json, "reason",
                                    (result == APP_UNINSTALL_ERR_NOT_FOUND)
                                        ? "NOT_FOUND"
                                        : ((result == APP_UNINSTALL_ERR_BUSY)
                                               ? "BUSY" : "UNINSTALL_FAILED"));
            cJSON_AddBoolToObject(response_json, "executable", AICAM_FALSE);
        }
    }

    if (response_json == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    return app_install_respond_json(ctx, response_json, "App uninstall completed with result");
}

aicam_result_t app_install_run_handler(http_handler_context_t *ctx)
{
    const app_install_io_t *io;
    cJSON *root = NULL;
    app_run_outcome_t outcome;
    app_run_result_t result;
    char *log_text;
    size_t log_len;
    aicam_bool_t log_truncated;

    if (ctx == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }

    if (web_server_high_privilege_allowed(ctx) != AICAM_TRUE) {
        LOG_SVC_WARN("app run: refused, high-privilege admin authentication required");
        return api_response_error(ctx, API_ERROR_FORBIDDEN,
                                  "High-privilege administrator authentication required");
    }

    io = app_installer_device_io();
    if (io == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "App installer storage unavailable");
    }

    memset(&outcome, 0, sizeof(outcome));
    result = app_host_run_installed(io, &outcome);
    log_truncated = app_host_run_log_truncated();

    if (result == APP_RUN_OK) {
        LOG_SVC_INFO("app run: completed publisher=%s app=%s v%u.%u.%u entry_result=%d",
                     outcome.record.publisher_id, outcome.record.app_id,
                     outcome.record.version_major, outcome.record.version_minor,
                     outcome.record.version_patch, outcome.entry_result);
        root = app_install_record_json(&outcome.record, "completed");
        if (root == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddBoolToObject(root, "entry_returned",
                              (outcome.entry_returned == AICAM_TRUE) ? AICAM_TRUE : AICAM_FALSE);
        cJSON_AddNumberToObject(root, "entry_result", (double)outcome.entry_result);
        cJSON_AddBoolToObject(root, "app_running",
                              (app_install_app_running() == AICAM_TRUE) ? AICAM_TRUE : AICAM_FALSE);
        log_text = (char *)malloc(APP_RUN_LOG_CAP + 1u);
        if (log_text != NULL) {
            log_len = app_host_run_take_log(log_text, APP_RUN_LOG_CAP + 1u);
            log_text[log_len] = '\0';
            cJSON_AddStringToObject(root, "log", log_text);
            free(log_text);
        }
        cJSON_AddBoolToObject(root, "log_truncated",
                              (log_truncated == AICAM_TRUE) ? AICAM_TRUE : AICAM_FALSE);
    } else {
        if (result == APP_RUN_ERR_STORAGE_STATE_UNKNOWN) {
            LOG_SVC_ERROR("app run: storage state UNKNOWN, fail-closed, no execution");
        } else {
            LOG_SVC_WARN("app run: refused result=%s reason=%s",
                         app_host_run_result_str(result), outcome.reason);
        }
        root = cJSON_CreateObject();
        if (root == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(root, "status", "refused");
        cJSON_AddStringToObject(root, "reason", app_host_run_result_str(result));
        if (outcome.reason[0] != '\0' &&
            strcmp(outcome.reason, app_host_run_result_str(result)) != 0) {
            cJSON_AddStringToObject(root, "detail", outcome.reason);
        }
        cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
        cJSON_AddBoolToObject(root, "app_running",
                              (app_install_app_running() == AICAM_TRUE) ? AICAM_TRUE : AICAM_FALSE);
    }

    return app_install_respond_json(ctx, root, "App run completed with result");
}

aicam_result_t web_api_register_app_install_module(void)
{
    size_t count = sizeof(g_app_install_routes) / sizeof(g_app_install_routes[0]);
    size_t i;
    aicam_result_t result;
    app_install_result_t resolve;

    LOG_SVC_INFO("Registering App Install API module...");

    app_host_run_init(app_host_run_device_backend());

    result = app_installer_storage_init();
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("App install storage init failed: %d", result);
        return result;
    }

    for (i = 0; i < count; i++) {
        result = http_server_register_route(&g_app_install_routes[i]);
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to register route %s: %d", g_app_install_routes[i].path, result);
            return result;
        }
    }

    resolve = app_install_boot_resolve(app_installer_device_io());
    if (resolve != APP_INSTALL_OK) {
        LOG_SVC_ERROR("App install boot intent resolve fail-closed: %s",
                      app_install_result_str(resolve));
    } else {
        LOG_SVC_INFO("App install boot intent resolved");
    }

    LOG_SVC_INFO("App Install API module registered successfully (%zu routes)", count);
    return AICAM_OK;
}
