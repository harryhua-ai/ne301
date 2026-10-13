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
#include "app_trust_anchor.h"
#include "app_admission.h"
#include "app_host_abi.h"

#define APP_INSTALL_HEX_PER_BYTE 2

typedef struct {
    app_admission_policy_t policy;
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
        .method = "DELETE",
        .path = API_PATH_PREFIX "/app/installed",
        .handler = app_install_remove_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    }
};

static app_admission_result_t api_app_install_verify(const uint8_t *data, size_t len,
                                                     app_admission_verdict_t *out,
                                                     void *ctx)
{
    api_app_install_verify_ctx_t *vctx = (api_app_install_verify_ctx_t *)ctx;
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
    vctx->policy.max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN;
    if (app_host_exec_region(&base, &size) == AICAM_TRUE) {
        vctx->policy.exec_region_base = base;
        vctx->policy.exec_region_size = size;
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
    if ((uint64_t)decoded_cap > (uint64_t)APP_ADMISSION_MAX_PACKAGE_LEN) {
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

    ctx_ok = api_app_install_fill_ctx(&vctx);
    if (ctx_ok != AICAM_TRUE) {
        root = cJSON_CreateObject();
        if (root == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(root, "state", "unavailable");
        cJSON_AddStringToObject(root, "reason", "TRUST_UNPROVEN");
        cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
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
        return app_install_respond_json(ctx, root, "App install query result");
    }

    if (query.state == APP_INSTALL_STATE_NONE) {
        root = cJSON_CreateObject();
        if (root == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(root, "state", "none");
        cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
        return app_install_respond_json(ctx, root, "App install query result");
    }

    root = app_install_record_json(&query.record, app_install_state_str(query.state));
    if (root == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    if (query.state == APP_INSTALL_STATE_UNAVAILABLE) {
        cJSON_AddStringToObject(root, "reason", query.reason);
    }
    return app_install_respond_json(ctx, root, "App install query result");
}

aicam_result_t app_install_remove_handler(http_handler_context_t *ctx)
{
    const app_install_io_t *io;
    cJSON *response_json = NULL;
    app_install_record_t removed;
    app_install_result_t result;

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

    io = app_installer_device_io();
    if (io == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "App installer storage unavailable");
    }

    memset(&removed, 0, sizeof(removed));
    result = app_install_uninstall(io, &removed);

    if (result == APP_INSTALL_OK) {
        LOG_SVC_INFO("app uninstall: removed publisher=%s app=%s v%u.%u.%u",
                     removed.publisher_id, removed.app_id,
                     removed.version_major, removed.version_minor, removed.version_patch);
        response_json = app_install_record_json(&removed, "uninstalled");
    } else {
        if (result == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN) {
            LOG_SVC_ERROR("app uninstall: storage state UNKNOWN, fail-closed, reboot resolves pending intent");
        } else {
            LOG_SVC_WARN("app uninstall: refused reason=%s", app_install_result_str(result));
        }
        response_json = app_install_refused_json(result);
    }

    if (response_json == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    return app_install_respond_json(ctx, response_json, "App uninstall completed with result");
}

aicam_result_t web_api_register_app_install_module(void)
{
    size_t count = sizeof(g_app_install_routes) / sizeof(g_app_install_routes[0]);
    size_t i;
    aicam_result_t result;
    app_install_result_t resolve;

    LOG_SVC_INFO("Registering App Install API module...");

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
