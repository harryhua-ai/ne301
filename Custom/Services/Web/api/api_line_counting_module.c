#include "api_line_counting_module.h"
#include "line_counting.h"
#include "lc_delivery_queue.h"
#include "ai_service.h"
#include "json_config_mgr.h"
#include "web_api.h"
#include "web_server.h"
#include "buffer_mgr.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

#define LC_API_STATE_DISABLED          "disabled"
#define LC_API_STATE_RUNNING           "running"
#define LC_API_STATE_UNSUPPORTED_MODEL "unsupported_model"
#define LC_API_STATE_TARGET_INVALID    "target_class_invalid"

static const char *lc_api_state_str(line_counting_state_t state) {
    switch (state) {
    case LC_STATE_RUNNING: return LC_API_STATE_RUNNING;
    case LC_STATE_UNSUPPORTED_MODEL: return LC_API_STATE_UNSUPPORTED_MODEL;
    case LC_STATE_TARGET_CLASS_INVALID: return LC_API_STATE_TARGET_INVALID;
    default: return LC_API_STATE_DISABLED;
    }
}

static uint16_t lc_api_from_norm(double v) {
    if (v < 0.0) v = 0.0;
    if (v > 1.0) v = 1.0;
    return (uint16_t)(v * 1000.0 + 0.5);
}

static aicam_bool_t lc_api_target_in_model(const char *target_class) {
    ai_model_runtime_info_t rt;
    if (ai_get_model_runtime_info(&rt) != AICAM_OK) return AICAM_TRUE;
    if (rt.loaded != AICAM_TRUE || rt.result_type != PP_TYPE_OD) return AICAM_TRUE;
    char name[LC_TARGET_CLASS_NAME_LEN];
    for (uint16_t i = 0; i < rt.num_classes; i++) {
        if (ai_get_model_class_name(i, name, sizeof(name)) != AICAM_OK) return AICAM_TRUE;
        if (strcmp(name, target_class) == 0) return AICAM_TRUE;
    }
    return AICAM_FALSE;
}

static cJSON *lc_api_config_to_json(const line_counting_config_t *cfg) {
    cJSON *o = cJSON_CreateObject();
    if (!o) return NULL;
    cJSON_AddBoolToObject(o, "enable", cfg->enable ? 1 : 0);
    cJSON_AddStringToObject(o, "counter_name", cfg->counter_name);
    cJSON_AddStringToObject(o, "target_class", cfg->target_class_name);
    cJSON *line = cJSON_CreateObject();
    cJSON_AddNumberToObject(line, "x1", cfg->line_x1_permille / 1000.0);
    cJSON_AddNumberToObject(line, "y1", cfg->line_y1_permille / 1000.0);
    cJSON_AddNumberToObject(line, "x2", cfg->line_x2_permille / 1000.0);
    cJSON_AddNumberToObject(line, "y2", cfg->line_y2_permille / 1000.0);
    cJSON_AddNumberToObject(line, "outside_x", cfg->outside_x_permille / 1000.0);
    cJSON_AddNumberToObject(line, "outside_y", cfg->outside_y_permille / 1000.0);
    cJSON_AddItemToObject(o, "line", line);
    cJSON_AddNumberToObject(o, "confidence_threshold", cfg->conf_threshold_permille / 1000.0);
    cJSON *tracking = cJSON_CreateObject();
    cJSON_AddNumberToObject(tracking, "association_distance", cfg->max_dist_permille / 1000.0);
    cJSON_AddNumberToObject(tracking, "history_length", cfg->track_history_k);
    cJSON_AddNumberToObject(tracking, "max_missed_frames", cfg->max_miss);
    cJSON_AddNumberToObject(tracking, "confirmation_frames", cfg->k_confirm);
    cJSON_AddItemToObject(o, "tracking", tracking);
    cJSON_AddNumberToObject(o, "window_minutes", cfg->window_minutes);
    cJSON *reporting = cJSON_CreateObject();
    cJSON_AddBoolToObject(reporting, "mqtt_enabled", cfg->mqtt_report_enable ? 1 : 0);
    cJSON_AddBoolToObject(reporting, "webhook_enabled", cfg->webhook_report_enable ? 1 : 0);
    cJSON_AddBoolToObject(reporting, "tracks_enabled", cfg->tracks_report_enable ? 1 : 0);
    cJSON_AddBoolToObject(reporting, "heat_grid_enabled", cfg->heat_grid_enable ? 1 : 0);
    cJSON_AddNumberToObject(reporting, "backlog_capacity", cfg->backlog_capacity);
    cJSON_AddItemToObject(o, "reporting", reporting);
    return o;
}

static void lc_api_config_from_json(line_counting_config_t *cfg, const cJSON *req) {
    const cJSON *item;
    if ((item = cJSON_GetObjectItem(req, "enable")) && cJSON_IsBool(item))
        cfg->enable = cJSON_IsTrue(item) ? AICAM_TRUE : AICAM_FALSE;
    if ((item = cJSON_GetObjectItem(req, "counter_name")) && cJSON_IsString(item) &&
        item->valuestring)
        snprintf(cfg->counter_name, sizeof(cfg->counter_name), "%s", item->valuestring);
    if ((item = cJSON_GetObjectItem(req, "target_class")) && cJSON_IsString(item) &&
        item->valuestring)
        snprintf(cfg->target_class_name, sizeof(cfg->target_class_name), "%s",
                 item->valuestring);
    const cJSON *line = cJSON_GetObjectItem(req, "line");
    if (cJSON_IsObject(line)) {
        const cJSON *c;
        if ((c = cJSON_GetObjectItem(line, "x1")) && cJSON_IsNumber(c))
            cfg->line_x1_permille = lc_api_from_norm(c->valuedouble);
        if ((c = cJSON_GetObjectItem(line, "y1")) && cJSON_IsNumber(c))
            cfg->line_y1_permille = lc_api_from_norm(c->valuedouble);
        if ((c = cJSON_GetObjectItem(line, "x2")) && cJSON_IsNumber(c))
            cfg->line_x2_permille = lc_api_from_norm(c->valuedouble);
        if ((c = cJSON_GetObjectItem(line, "y2")) && cJSON_IsNumber(c))
            cfg->line_y2_permille = lc_api_from_norm(c->valuedouble);
        if ((c = cJSON_GetObjectItem(line, "outside_x")) && cJSON_IsNumber(c))
            cfg->outside_x_permille = lc_api_from_norm(c->valuedouble);
        if ((c = cJSON_GetObjectItem(line, "outside_y")) && cJSON_IsNumber(c))
            cfg->outside_y_permille = lc_api_from_norm(c->valuedouble);
    }
    if ((item = cJSON_GetObjectItem(req, "confidence_threshold")) && cJSON_IsNumber(item))
        cfg->conf_threshold_permille = lc_api_from_norm(item->valuedouble);
    const cJSON *tracking = cJSON_GetObjectItem(req, "tracking");
    if (cJSON_IsObject(tracking)) {
        const cJSON *c;
        if ((c = cJSON_GetObjectItem(tracking, "association_distance")) && cJSON_IsNumber(c))
            cfg->max_dist_permille = lc_api_from_norm(c->valuedouble);
        if ((c = cJSON_GetObjectItem(tracking, "history_length")) && cJSON_IsNumber(c) &&
            c->valueint >= 0 && c->valueint <= 255)
            cfg->track_history_k = (uint8_t)c->valueint;
        if ((c = cJSON_GetObjectItem(tracking, "max_missed_frames")) && cJSON_IsNumber(c) &&
            c->valueint >= 0 && c->valueint <= 255)
            cfg->max_miss = (uint8_t)c->valueint;
        if ((c = cJSON_GetObjectItem(tracking, "confirmation_frames")) && cJSON_IsNumber(c) &&
            c->valueint >= 0 && c->valueint <= 255)
            cfg->k_confirm = (uint8_t)c->valueint;
    }
    if ((item = cJSON_GetObjectItem(req, "window_minutes")) && cJSON_IsNumber(item) &&
        item->valueint >= 0 && item->valueint <= 65535)
        cfg->window_minutes = (uint16_t)item->valueint;
    const cJSON *reporting = cJSON_GetObjectItem(req, "reporting");
    if (cJSON_IsObject(reporting)) {
        const cJSON *c;
        if ((c = cJSON_GetObjectItem(reporting, "mqtt_enabled")) && cJSON_IsBool(c))
            cfg->mqtt_report_enable = cJSON_IsTrue(c) ? AICAM_TRUE : AICAM_FALSE;
        if ((c = cJSON_GetObjectItem(reporting, "webhook_enabled")) && cJSON_IsBool(c))
            cfg->webhook_report_enable = cJSON_IsTrue(c) ? AICAM_TRUE : AICAM_FALSE;
        if ((c = cJSON_GetObjectItem(reporting, "tracks_enabled")) && cJSON_IsBool(c))
            cfg->tracks_report_enable = cJSON_IsTrue(c) ? AICAM_TRUE : AICAM_FALSE;
        if ((c = cJSON_GetObjectItem(reporting, "heat_grid_enabled")) && cJSON_IsBool(c))
            cfg->heat_grid_enable = cJSON_IsTrue(c) ? AICAM_TRUE : AICAM_FALSE;
        if ((c = cJSON_GetObjectItem(reporting, "backlog_capacity")) && cJSON_IsNumber(c) &&
            c->valueint >= 0 && c->valueint <= 65535)
            cfg->backlog_capacity = (uint16_t)c->valueint;
    }
}

static aicam_result_t lc_api_config_get_handler(http_handler_context_t *ctx) {
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    line_counting_config_t cfg;
    if (json_config_get_line_counting_config(&cfg) != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR,
                                  "Failed to get line counting config");
    }
    cJSON *resp = lc_api_config_to_json(&cfg);
    if (!resp) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to build response");
    }
    char *json_str = cJSON_Print(resp);
    cJSON_Delete(resp);
    if (!json_str) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    return api_response_success(ctx, json_str, "OK");
}

static aicam_result_t lc_api_config_post_handler(http_handler_context_t *ctx) {
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    if (!web_api_verify_content_type(ctx, "application/json")) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid Content-Type");
    }
    cJSON *req = web_api_parse_body(ctx);
    if (!req) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON");
    }

    line_counting_config_t cfg;
    aicam_result_t r = json_config_get_line_counting_config(&cfg);
    if (r != AICAM_OK) {
        cJSON_Delete(req);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR,
                                  "Failed to get current config");
    }
    lc_api_config_from_json(&cfg, req);
    cJSON_Delete(req);

    if (!line_counting_config_is_valid(&cfg) ||
        !line_counting_config_utf8_valid(cfg.counter_name) ||
        !line_counting_config_utf8_valid(cfg.target_class_name) ||
        cfg.counter_name[0] == '\0' || cfg.target_class_name[0] == '\0') {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "invalid_config");
    }
    if (!lc_api_target_in_model(cfg.target_class_name)) {
        return api_response_error(ctx, API_ERROR_UNPROCESSABLE, "invalid_target_class");
    }

    r = line_counting_apply_config(&cfg);
    if (r != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR,
                                  "Failed to apply line counting config");
    }

    cJSON *resp = lc_api_config_to_json(&cfg);
    if (!resp) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to build response");
    }
    char *json_str = cJSON_Print(resp);
    cJSON_Delete(resp);
    if (!json_str) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    return api_response_success(ctx, json_str, "Line counting configuration updated");
}

static aicam_result_t lc_api_status_handler(http_handler_context_t *ctx) {
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    line_counting_status_t st;
    aicam_result_t r = line_counting_get_status(&st);
    if (r != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Line counting not ready");
    }

    ai_model_runtime_info_t rt;
    memset(&rt, 0, sizeof(rt));
    (void)ai_get_model_runtime_info(&rt);

    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to build response");
    }
    line_counting_config_t cfg;
    aicam_bool_t enabled = AICAM_FALSE;
    if (json_config_get_line_counting_config(&cfg) == AICAM_OK) {
        enabled = cfg.enable;
    }
    cJSON_AddBoolToObject(resp, "enabled", enabled ? 1 : 0);
    cJSON_AddStringToObject(resp, "state", lc_api_state_str(st.state));
    if (st.reason == LC_REASON_MODEL_NOT_LOADED) {
        cJSON_AddStringToObject(resp, "reason", "model_not_loaded");
    } else if (st.reason == LC_REASON_CLASS_METADATA_UNAVAILABLE) {
        cJSON_AddStringToObject(resp, "reason", "class_metadata_unavailable");
    } else {
        cJSON_AddNullToObject(resp, "reason");
    }
    cJSON_AddStringToObject(resp, "target_class", st.binding.target_class_name);
    cJSON_AddBoolToObject(resp, "resetting", line_counting_is_resetting() ? 1 : 0);

    cJSON *model = cJSON_CreateObject();
    cJSON_AddStringToObject(model, "name", rt.name);
    cJSON_AddStringToObject(model, "version", rt.version);
    cJSON_AddStringToObject(model, "postprocess_type", rt.postprocess_type);
    cJSON_AddNumberToObject(model, "generation", (double)rt.generation);
    cJSON *classes = cJSON_CreateArray();
    if (rt.loaded == AICAM_TRUE) {
        char cname[LC_TARGET_CLASS_NAME_LEN];
        for (uint16_t i = 0; i < rt.num_classes; i++) {
            if (ai_get_model_class_name(i, cname, sizeof(cname)) != AICAM_OK) break;
            cJSON *cls = cJSON_CreateObject();
            cJSON_AddNumberToObject(cls, "id", (double)i);
            cJSON_AddStringToObject(cls, "name", cname);
            cJSON_AddItemToArray(classes, cls);
        }
    }
    cJSON_AddItemToObject(model, "classes", classes);
    cJSON_AddItemToObject(resp, "model", model);

    char *json_str = cJSON_Print(resp);
    cJSON_Delete(resp);
    if (!json_str) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    return api_response_success(ctx, json_str, "OK");
}

static aicam_result_t lc_api_stats_handler(http_handler_context_t *ctx) {
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    line_counting_stats_t stats;
    aicam_result_t r = line_counting_get_stats(&stats);
    if (r != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Line counting not ready");
    }
    lc_delivery_stats_t delivery;
    memset(&delivery, 0, sizeof(delivery));
    (void)line_counting_get_delivery_stats(&delivery);

    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to build response");
    }
    cJSON *window = cJSON_CreateObject();
    cJSON_AddNumberToObject(window, "in", (double)stats.window_in);
    cJSON_AddNumberToObject(window, "out", (double)stats.window_out);
    cJSON_AddItemToObject(resp, "window", window);
    cJSON *total = cJSON_CreateObject();
    cJSON_AddNumberToObject(total, "in", (double)stats.total_in);
    cJSON_AddNumberToObject(total, "out", (double)stats.total_out);
    cJSON_AddItemToObject(resp, "total", total);
    cJSON *delivery_j = cJSON_CreateObject();
    cJSON *mqtt = cJSON_CreateObject();
    cJSON_AddNumberToObject(mqtt, "backlog", (double)delivery.mqtt_backlog);
    cJSON_AddNumberToObject(mqtt, "dropped", (double)delivery.mqtt_dropped);
    cJSON_AddItemToObject(delivery_j, "mqtt", mqtt);
    cJSON *webhook = cJSON_CreateObject();
    cJSON_AddNumberToObject(webhook, "backlog", (double)delivery.webhook_backlog);
    cJSON_AddNumberToObject(webhook, "dropped", (double)delivery.webhook_dropped);
    cJSON_AddItemToObject(delivery_j, "webhook", webhook);
    cJSON_AddItemToObject(resp, "delivery", delivery_j);

    char *json_str = cJSON_Print(resp);
    cJSON_Delete(resp);
    if (!json_str) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    return api_response_success(ctx, json_str, "OK");
}

static aicam_result_t lc_api_events_handler(http_handler_context_t *ctx) {
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    line_count_event_t events[LC_EVENTS_RING_CAPACITY];
    uint16_t n = 0;
    aicam_result_t r = line_counting_get_events(events, LC_EVENTS_RING_CAPACITY, &n);
    if (r != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Line counting not ready");
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    if (!resp || !arr) {
        if (resp) cJSON_Delete(resp);
        if (arr) cJSON_Delete(arr);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to build response");
    }
    for (uint16_t i = 0; i < n; i++) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddNumberToObject(e, "sequence", (double)events[i].sequence);
        cJSON_AddNumberToObject(e, "timestamp_ms", (double)events[i].timestamp_ms);
        cJSON_AddNumberToObject(e, "track_id", (double)events[i].track_id);
        cJSON_AddStringToObject(e, "direction",
                                events[i].direction == LC_DIRECTION_IN ? "in" : "out");
        cJSON_AddItemToArray(arr, e);
    }
    cJSON_AddItemToObject(resp, "events", arr);

    char *json_str = cJSON_Print(resp);
    cJSON_Delete(resp);
    if (!json_str) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    return api_response_success(ctx, json_str, "OK");
}

static aicam_result_t lc_api_reset_handler(http_handler_context_t *ctx) {
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    aicam_result_t r = line_counting_reset();
    if (r == AICAM_ERROR_BUSY) {
        return api_response_error(ctx, API_ERROR_TOO_MANY_REQUESTS, "Reset already in progress");
    }
    if (r != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Reset failed");
    }
    return api_response_success(ctx, "{\"resetting\":true}", "Reset started");
}

aicam_result_t web_api_register_line_counting_module(void) {
    api_route_t routes[] = {
        {
            .path = API_PATH_PREFIX "/apps/line-counting/config",
            .method = "GET",
            .handler = lc_api_config_get_handler,
            .require_auth = AICAM_TRUE
        },
        {
            .path = API_PATH_PREFIX "/apps/line-counting/config",
            .method = "POST",
            .handler = lc_api_config_post_handler,
            .require_auth = AICAM_TRUE
        },
        {
            .path = API_PATH_PREFIX "/apps/line-counting/status",
            .method = "GET",
            .handler = lc_api_status_handler,
            .require_auth = AICAM_TRUE
        },
        {
            .path = API_PATH_PREFIX "/apps/line-counting/stats",
            .method = "GET",
            .handler = lc_api_stats_handler,
            .require_auth = AICAM_TRUE
        },
        {
            .path = API_PATH_PREFIX "/apps/line-counting/events",
            .method = "GET",
            .handler = lc_api_events_handler,
            .require_auth = AICAM_TRUE
        },
        {
            .path = API_PATH_PREFIX "/apps/line-counting/reset",
            .method = "POST",
            .handler = lc_api_reset_handler,
            .require_auth = AICAM_TRUE
        },
    };

    for (int i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        aicam_result_t ret = http_server_register_route(&routes[i]);
        if (ret != AICAM_OK) return ret;
    }

    return AICAM_OK;
}
