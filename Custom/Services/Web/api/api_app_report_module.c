#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "api_app_report_module.h"

#include "web_api.h"
#include "cJSON.h"
#include "debug.h"
#include "aicam_types.h"

#include "app_report_service.h"
#include "app_host_session.h"

static aicam_result_t app_report_status_handler(http_handler_context_t *ctx);

static const api_route_t g_app_report_routes[] = {
    {
        .method = "GET",
        .path = API_PATH_PREFIX "/app/report/status",
        .handler = app_report_status_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    }
};

static const char *app_report_channel_state_str(uint32_t state)
{
    switch (state) {
    case APP_REPORT_MQTT_NOT_CONFIGURED:
        return "not_configured";
    case APP_REPORT_MQTT_PENDING:
        return "pending";
    case APP_REPORT_MQTT_TRANSPORT_SUCCEEDED:
        return "succeeded";
    case APP_REPORT_MQTT_TRANSPORT_FAILED:
        return "failed";
    case APP_REPORT_MQTT_UNKNOWN:
        return "unknown";
    default:
        return "unknown";
    }
}

static aicam_result_t app_report_respond_json(http_handler_context_t *ctx, cJSON *root,
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

static aicam_result_t app_report_status_handler(http_handler_context_t *ctx)
{
    cJSON *root = NULL;
    cJSON *reports_json = NULL;
    app_report_session_view_t view;
    app_report_session_report_view_t entries[APP_REPORT_QUEUE_SLOTS];
    app_host_session_status_t session;
    aicam_bool_t project = AICAM_FALSE;
    uint32_t count = 0u;
    uint32_t i;

    if (ctx == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only GET method is allowed");
    }

    if (web_server_high_privilege_allowed(ctx) != AICAM_TRUE) {
        LOG_SVC_WARN("app report: status refused, high-privilege administrator authentication required");
        return api_response_error(ctx, API_ERROR_FORBIDDEN,
                                  "High-privilege administrator authentication required");
    }

    memset(&view, 0, sizeof(view));
    (void)app_report_service_session_view(&view);

    memset(&session, 0, sizeof(session));
    app_host_session_status(&session);

    if (view.session_bound == AICAM_TRUE &&
        session.state == APP_SESSION_STATE_RUNNING &&
        strcmp(view.app_id, session.app_id) == 0) {
        project = AICAM_TRUE;
    }

    root = cJSON_CreateObject();
    if (root == NULL) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddBoolToObject(root, "session_bound",
                          (project == AICAM_TRUE) ? AICAM_TRUE : AICAM_FALSE);
    cJSON_AddStringToObject(root, "session_state", app_session_state_str(session.state));
    if (session.publisher_id[0] != '\0' || session.app_id[0] != '\0') {
        cJSON_AddStringToObject(root, "publisher_id", session.publisher_id);
        cJSON_AddStringToObject(root, "app_id", session.app_id);
    }
    if (project == AICAM_TRUE) {
        cJSON_AddNumberToObject(root, "host_boot_id", (double)view.host_boot_id);
    } else {
        cJSON_AddNumberToObject(root, "host_boot_id", 0.0);
    }
    cJSON_AddNumberToObject(root, "queue_slots", (double)APP_REPORT_QUEUE_SLOTS);

    reports_json = cJSON_CreateArray();
    if (reports_json == NULL) {
        cJSON_Delete(root);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    if (project == AICAM_TRUE) {
        memset(entries, 0, sizeof(entries));
        count = app_report_service_session_reports(entries,
                                                   (uint32_t)APP_REPORT_QUEUE_SLOTS);
    }
    cJSON_AddNumberToObject(root, "queue_used", (double)count);
    for (i = 0u; i < count; i++) {
        cJSON *item = cJSON_CreateObject();

        if (item == NULL) {
            cJSON_Delete(reports_json);
            cJSON_Delete(root);
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddNumberToObject(item, "report_seq", (double)entries[i].report_seq);
        cJSON_AddStringToObject(item, "mqtt_state",
                                app_report_channel_state_str(entries[i].mqtt_state));
        cJSON_AddStringToObject(item, "webhook_state",
                                app_report_channel_state_str(entries[i].webhook_state));
        cJSON_AddNumberToObject(item, "mqtt_attempts", (double)entries[i].mqtt_attempts);
        cJSON_AddNumberToObject(item, "webhook_attempts",
                                (double)entries[i].webhook_attempts);
        cJSON_AddNumberToObject(item, "payload_len", (double)entries[i].payload_len);
        cJSON_AddItemToArray(reports_json, item);
    }
    cJSON_AddItemToObject(root, "reports", reports_json);

    return app_report_respond_json(ctx, root, "App report status");
}

aicam_result_t web_api_register_app_report_module(void)
{
    size_t count = sizeof(g_app_report_routes) / sizeof(g_app_report_routes[0]);
    size_t i;
    aicam_result_t result;

    LOG_SVC_INFO("Registering App Report API module...");

    for (i = 0; i < count; i++) {
        result = http_server_register_route(&g_app_report_routes[i]);
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to register route %s: %d", g_app_report_routes[i].path, result);
            return result;
        }
    }

    LOG_SVC_INFO("App Report API module registered successfully (%zu routes)", count);
    return AICAM_OK;
}
