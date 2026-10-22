#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "api_app_admission_module.h"

#include "web_api.h"
#include "cJSON.h"
#include "debug.h"
#include "aicam_types.h"

#include "app_admission.h"
#include "app_trust_anchor.h"
#include "app_host.h"
#include "app_host_abi.h"

#define APP_ADMISSION_HEX_PER_BYTE 2

static const api_route_t g_app_admission_routes[] = {
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/app/admission/verify",
        .handler = app_admission_verify_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "GET",
        .path = API_PATH_PREFIX "/app/admission/state",
        .handler = app_admission_state_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    }
};

static void app_admission_fill_policy(app_admission_policy_t *policy)
{
    uint32_t base = 0;
    uint32_t size = 0;

    memset(policy, 0, sizeof(*policy));
    policy->target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    policy->psram_mib = 64u;
    policy->abi_version = APP_HOST_ABI_VERSION;
    policy->supported_caps_mask = APP_ADMISSION_KNOWN_CAPS_MASK;
    policy->max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN_V2;

    if (app_host_exec_region(&base, &size) == AICAM_TRUE) {
        policy->exec_region_base = base;
        policy->exec_region_size = size;
    }
}

cJSON *app_admission_verdict_json(const app_admission_verdict_t *verdict,
                                  app_admission_result_t result)
{
    cJSON *root = cJSON_CreateObject();
    char tbs_hex[APP_ADMISSION_SHA256_LEN * APP_ADMISSION_HEX_PER_BYTE + 1];
    char img_hex[APP_ADMISSION_SHA256_LEN * APP_ADMISSION_HEX_PER_BYTE + 1];
    size_t i;

    if (root == NULL) {
        return NULL;
    }

    if (result == APP_ADMISSION_OK) {
        cJSON_AddStringToObject(root, "verdict", app_admission_result_str(result));
        cJSON_AddStringToObject(root, "publisher_id", verdict->publisher_id);
        cJSON_AddStringToObject(root, "app_id", verdict->app_id);
        cJSON_AddNumberToObject(root, "version_major", (double)verdict->version_major);
        cJSON_AddNumberToObject(root, "version_minor", (double)verdict->version_minor);
        cJSON_AddNumberToObject(root, "version_patch", (double)verdict->version_patch);
        cJSON_AddNumberToObject(root, "native_image_size", (double)verdict->native_image_size);

        for (i = 0; i < APP_ADMISSION_SHA256_LEN; i++) {
            snprintf(&tbs_hex[i * 2], 3, "%02x", verdict->tbs_sha256[i]);
            snprintf(&img_hex[i * 2], 3, "%02x", verdict->image_sha256[i]);
        }
        cJSON_AddStringToObject(root, "tbs_sha256", tbs_hex);
        cJSON_AddStringToObject(root, "image_sha256", img_hex);
    } else {
        cJSON_AddStringToObject(root, "verdict", "refused");
        cJSON_AddStringToObject(root, "reason", app_admission_result_str(result));
    }

    cJSON_AddBoolToObject(root, "executable", AICAM_FALSE);
    cJSON_AddBoolToObject(root, "installed", AICAM_FALSE);
    return root;
}

aicam_result_t app_admission_verify_handler(http_handler_context_t *ctx)
{
    app_admission_policy_t policy;
    app_admission_trust_view_t trust;
    app_admission_verdict_t verdict;
    app_admission_result_t result;
    const app_trust_anchor_t *anchor;
    cJSON *request_json = NULL;
    cJSON *response_json = NULL;
    char *response_text = NULL;
    cJSON *b64_item;
    size_t b64_len;
    size_t decoded_cap;
    size_t decoded_len;
    uint8_t *package = NULL;
    aicam_result_t rc;

    if (ctx == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }

    if (ctx->authenticated != AICAM_TRUE) {
        LOG_SVC_WARN("app admission: verify refused, high-privilege admin authentication required");
        return api_response_error(ctx, API_ERROR_FORBIDDEN,
                                  "High-privilege administrator authentication required");
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
    if (b64_len == 0u || b64_len > API_APP_ADMISSION_MAX_B64_CHARS) {
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

    anchor = app_trust_anchor_get();
    if (app_trust_anchor_status() != APP_TRUST_ANCHOR_PROVISIONED) {
        free(package);
        response_json = cJSON_CreateObject();
        if (response_json == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(response_json, "verdict", "refused");
        cJSON_AddStringToObject(response_json, "reason", "TRUST_UNPROVEN");
        cJSON_AddBoolToObject(response_json, "executable", AICAM_FALSE);
        cJSON_AddBoolToObject(response_json, "installed", AICAM_FALSE);
        response_text = cJSON_PrintUnformatted(response_json);
        cJSON_Delete(response_json);
        if (response_text == NULL) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        rc = api_response_success(ctx, response_text, "App admission verification completed");
        if (rc != AICAM_OK) {
            free(response_text);
        }
        return rc;
    }

    trust.publisher_id = anchor->publisher_id;
    trust.publisher_id_len = anchor->publisher_id_len;
    trust.spki_der = anchor->spki_der;
    trust.spki_der_len = anchor->spki_der_len;

    app_admission_fill_policy(&policy);
    result = app_admission_verify_package(package, decoded_len, &policy, &trust, &verdict);
    free(package);
    package = NULL;

    if (result == APP_ADMISSION_OK) {
        LOG_SVC_INFO("app admission: trusted_compatible publisher=%s app=%s",
                     verdict.publisher_id, verdict.app_id);
    } else {
        LOG_SVC_WARN("app admission: refused reason=%s", app_admission_result_str(result));
    }

    response_json = app_admission_verdict_json(&verdict, result);
    if (response_json == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    response_text = cJSON_PrintUnformatted(response_json);
    cJSON_Delete(response_json);
    if (response_text == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    rc = api_response_success(ctx, response_text, "App admission verification completed");
    if (rc != AICAM_OK) {
        free(response_text);
    }
    return rc;
}

aicam_result_t app_admission_state_handler(http_handler_context_t *ctx)
{
    const app_trust_anchor_t *anchor;
    cJSON *root = NULL;
    cJSON *trust_json = NULL;
    cJSON *limits_json = NULL;
    char *response_text = NULL;
    aicam_result_t rc;

    if (ctx == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET method is allowed");
    }

    if (ctx->authenticated != AICAM_TRUE) {
        LOG_SVC_WARN("app admission: state refused, high-privilege admin authentication required");
        return api_response_error(ctx, API_ERROR_FORBIDDEN,
                                  "High-privilege administrator authentication required");
    }

    anchor = app_trust_anchor_get();

    root = cJSON_CreateObject();
    if (root == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(root, "admission", "verify_only");
    cJSON_AddStringToObject(root, "install", "not_supported");
    cJSON_AddStringToObject(root, "execute", "not_supported_by_admission");

    limits_json = cJSON_CreateObject();
    if (limits_json == NULL) {
        cJSON_Delete(root);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    cJSON_AddBoolToObject(limits_json, "uart_loader_armed",
                          (app_host_experiment_armed() == AICAM_TRUE) ? AICAM_TRUE : AICAM_FALSE);
    cJSON_AddItemToObject(root, "experiment_uart_loader", limits_json);

    trust_json = cJSON_CreateObject();
    if (trust_json == NULL) {
        cJSON_Delete(root);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    cJSON_AddNumberToObject(trust_json, "format_version", (double)anchor->format_version);
    cJSON_AddStringToObject(trust_json, "publisher_id", anchor->publisher_id);
    cJSON_AddStringToObject(trust_json, "status",
                            app_trust_anchor_status_str(app_trust_anchor_status()));
    cJSON_AddStringToObject(trust_json, "profile", app_trust_anchor_profile_str());
    cJSON_AddStringToObject(trust_json, "spki_sha256",
                            app_trust_anchor_spki_sha256_hex());
    cJSON_AddStringToObject(trust_json, "provenance",
                            app_trust_anchor_provenance_str());
    cJSON_AddItemToObject(root, "trust_anchor", trust_json);

    response_text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (response_text == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    rc = api_response_success(ctx, response_text, "App admission state");
    if (rc != AICAM_OK) {
        free(response_text);
    }
    return rc;
}

aicam_result_t web_api_register_app_admission_module(void)
{
    size_t count = sizeof(g_app_admission_routes) / sizeof(g_app_admission_routes[0]);
    size_t i;

    LOG_SVC_INFO("Registering App Admission API module...");

    for (i = 0; i < count; i++) {
        aicam_result_t result = http_server_register_route(&g_app_admission_routes[i]);
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to register route %s: %d", g_app_admission_routes[i].path, result);
            return result;
        }
    }

    LOG_SVC_INFO("App Admission API module registered successfully (%zu routes)", count);
    return AICAM_OK;
}
