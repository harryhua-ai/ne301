/**
 * @file api_people_counting_module.c
 * @brief Legacy People Counting API — compatibility adapter over canonical Line Counting
 */

#include "api_people_counting_module.h"
#include "line_counting.h"
#include "ai_service.h"
#include "json_config_mgr.h"
#include "web_api.h"
#include "web_server.h"
#include "buffer_mgr.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

static void pc_api_model_info(char *name, size_t name_cap, char *pp, size_t pp_cap) {
    ai_model_runtime_info_t rt;
    memset(&rt, 0, sizeof(rt));
    if (ai_get_model_runtime_info(&rt) != AICAM_OK || rt.loaded != AICAM_TRUE) {
        name[0] = '\0';
        pp[0] = '\0';
        return;
    }
    snprintf(name, name_cap, "%s", rt.name);
    snprintf(pp, pp_cap, "%s", rt.postprocess_type);
}

static cJSON *pc_config_to_json(const line_counting_config_t *cfg) {
    char model_name[64];
    char model_pp[32];
    pc_api_model_info(model_name, sizeof(model_name), model_pp, sizeof(model_pp));

    cJSON *o = cJSON_CreateObject();
    if (!o) return NULL;
    cJSON_AddBoolToObject(o, "enable", cfg->enable ? 1 : 0);
    cJSON_AddNumberToObject(o, "line_x1_permille", cfg->line_x1_permille);
    cJSON_AddNumberToObject(o, "line_y1_permille", cfg->line_y1_permille);
    cJSON_AddNumberToObject(o, "line_x2_permille", cfg->line_x2_permille);
    cJSON_AddNumberToObject(o, "line_y2_permille", cfg->line_y2_permille);
    cJSON_AddNumberToObject(o, "outside_x_permille", cfg->outside_x_permille);
    cJSON_AddNumberToObject(o, "outside_y_permille", cfg->outside_y_permille);
    cJSON_AddNumberToObject(o, "conf_threshold_permille", cfg->conf_threshold_permille);
    cJSON_AddNumberToObject(o, "max_dist_permille", cfg->max_dist_permille);
    cJSON_AddStringToObject(o, "target_class_name", cfg->target_class_name);
    cJSON_AddStringToObject(o, "model_name", model_name);
    cJSON_AddStringToObject(o, "model_pp_type", model_pp);
    cJSON_AddNumberToObject(o, "track_history_k", cfg->track_history_k);
    cJSON_AddNumberToObject(o, "max_miss", cfg->max_miss);
    cJSON_AddNumberToObject(o, "k_confirm", cfg->k_confirm);
    cJSON_AddNumberToObject(o, "window_minutes", cfg->window_minutes);
    cJSON_AddBoolToObject(o, "mqtt_report_enable", cfg->mqtt_report_enable ? 1 : 0);
    cJSON_AddBoolToObject(o, "webhook_report_enable", cfg->webhook_report_enable ? 1 : 0);
    cJSON_AddBoolToObject(o, "tracks_report_enable", cfg->tracks_report_enable ? 1 : 0);
    cJSON_AddBoolToObject(o, "heat_grid_enable", cfg->heat_grid_enable ? 1 : 0);
    cJSON_AddNumberToObject(o, "backlog_capacity", cfg->backlog_capacity);
    return o;
}

static void pc_config_from_json(line_counting_config_t *cfg, const cJSON *req) {
    cJSON *item;
    #define APPLY_U16(field) \
        if ((item = cJSON_GetObjectItem(req, #field)) && cJSON_IsNumber(item) && \
            item->valueint >= 0 && item->valueint <= 65535) \
            cfg->field = (uint16_t)item->valueint;
    #define APPLY_U8(field) \
        if ((item = cJSON_GetObjectItem(req, #field)) && cJSON_IsNumber(item) && \
            item->valueint >= 0 && item->valueint <= 255) \
            cfg->field = (uint8_t)item->valueint;
    if ((item = cJSON_GetObjectItem(req, "enable")) && cJSON_IsBool(item))
        cfg->enable = cJSON_IsTrue(item) ? AICAM_TRUE : AICAM_FALSE;
    APPLY_U16(line_x1_permille)
    APPLY_U16(line_y1_permille)
    APPLY_U16(line_x2_permille)
    APPLY_U16(line_y2_permille)
    APPLY_U16(outside_x_permille)
    APPLY_U16(outside_y_permille)
    APPLY_U16(conf_threshold_permille)
    APPLY_U16(max_dist_permille)
    if ((item = cJSON_GetObjectItem(req, "target_class_name")) && cJSON_IsString(item) &&
        item->valuestring) {
        snprintf(cfg->target_class_name, sizeof(cfg->target_class_name), "%s",
                 item->valuestring);
    }
    APPLY_U8(track_history_k)
    APPLY_U8(max_miss)
    APPLY_U8(k_confirm)
    APPLY_U16(window_minutes)
    if ((item = cJSON_GetObjectItem(req, "mqtt_report_enable")) && cJSON_IsBool(item))
        cfg->mqtt_report_enable = cJSON_IsTrue(item) ? AICAM_TRUE : AICAM_FALSE;
    if ((item = cJSON_GetObjectItem(req, "webhook_report_enable")) && cJSON_IsBool(item))
        cfg->webhook_report_enable = cJSON_IsTrue(item) ? AICAM_TRUE : AICAM_FALSE;
    if ((item = cJSON_GetObjectItem(req, "tracks_report_enable")) && cJSON_IsBool(item))
        cfg->tracks_report_enable = cJSON_IsTrue(item) ? AICAM_TRUE : AICAM_FALSE;
    if ((item = cJSON_GetObjectItem(req, "heat_grid_enable")) && cJSON_IsBool(item))
        cfg->heat_grid_enable = cJSON_IsTrue(item) ? AICAM_TRUE : AICAM_FALSE;
    APPLY_U16(backlog_capacity)
    #undef APPLY_U16
    #undef APPLY_U8
}

static aicam_bool_t pc_api_target_in_model(const char *target_class) {
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

static aicam_result_t pc_config_get_handler(http_handler_context_t *ctx) {
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    line_counting_config_t cfg;
    if (json_config_get_line_counting_config(&cfg) != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR,
                                  "Failed to get line counting config");
    }
    cJSON *resp = pc_config_to_json(&cfg);
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

static aicam_result_t pc_config_set_handler(http_handler_context_t *ctx) {
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
    if (json_config_get_line_counting_config(&cfg) != AICAM_OK) {
        cJSON_Delete(req);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get current config");
    }
    pc_config_from_json(&cfg, req);
    cJSON_Delete(req);

    if (!line_counting_config_is_valid(&cfg) ||
        !line_counting_config_utf8_valid(cfg.target_class_name) ||
        cfg.target_class_name[0] == '\0') {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "invalid_config");
    }
    if (!pc_api_target_in_model(cfg.target_class_name)) {
        return api_response_error(ctx, API_ERROR_UNPROCESSABLE, "invalid_target_class");
    }

    if (json_config_set_line_counting_config(&cfg) != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR,
                                  "Failed to persist line counting config");
    }
    if (line_counting_apply_config(&cfg) != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR,
                                  "Failed to apply line counting config");
    }

    cJSON *resp = pc_config_to_json(&cfg);
    if (!resp) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to build response");
    }
    char *json_str = cJSON_Print(resp);
    cJSON_Delete(resp);
    if (!json_str) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    return api_response_success(ctx, json_str, "Configuration updated");
}

static aicam_result_t pc_stats_get_handler(http_handler_context_t *ctx) {
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    line_counting_stats_t stats;
    if (line_counting_get_stats(&stats) != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Line counting not ready");
    }
    lc_delivery_stats_t delivery;
    memset(&delivery, 0, sizeof(delivery));
    (void)line_counting_get_delivery_stats(&delivery);

    static uint32_t heat[256];
    aicam_bool_t heat_ok = line_counting_get_heat(heat);

    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to build response");
    }
    cJSON_AddNumberToObject(resp, "window_in", (double)stats.window_in);
    cJSON_AddNumberToObject(resp, "window_out", (double)stats.window_out);
    cJSON_AddNumberToObject(resp, "window_start_ts", (double)stats.window_start_ms);
    cJSON_AddNumberToObject(resp, "total_in", (double)stats.total_in);
    cJSON_AddNumberToObject(resp, "total_out", (double)stats.total_out);
    cJSON_AddNumberToObject(resp, "dropped_windows_mqtt", (double)delivery.mqtt_dropped);
    cJSON_AddNumberToObject(resp, "dropped_windows_webhook", (double)delivery.webhook_dropped);
    cJSON *heat_j = cJSON_CreateArray();
    if (heat_ok == AICAM_TRUE) {
        for (int i = 0; i < 256; i++) cJSON_AddItemToArray(heat_j, cJSON_CreateNumber(heat[i]));
    } else {
        for (int i = 0; i < 256; i++) cJSON_AddItemToArray(heat_j, cJSON_CreateNumber(0));
    }
    cJSON_AddItemToObject(resp, "heat_grid", heat_j);

    char *json_str = cJSON_Print(resp);
    cJSON_Delete(resp);
    if (!json_str) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    return api_response_success(ctx, json_str, "OK");
}

static aicam_result_t pc_reset_handler(http_handler_context_t *ctx) {
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    if (line_counting_reset() != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Reset failed");
    }
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", 1);
    cJSON_AddStringToObject(resp, "message", "Session reset");
    char *json_str = cJSON_Print(resp);
    cJSON_Delete(resp);
    if (!json_str) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    return api_response_success(ctx, json_str, "Reset");
}

static aicam_result_t pc_backlog_get_handler(http_handler_context_t *ctx) {
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    lc_delivery_stats_t delivery;
    memset(&delivery, 0, sizeof(delivery));
    (void)line_counting_get_delivery_stats(&delivery);

    cJSON *resp = cJSON_CreateObject();
    cJSON *mqtt = cJSON_CreateObject();
    cJSON_AddNumberToObject(mqtt, "count", (double)delivery.mqtt_backlog);
    cJSON_AddItemToObject(resp, "mqtt", mqtt);
    cJSON *webhook = cJSON_CreateObject();
    cJSON_AddNumberToObject(webhook, "count", (double)delivery.webhook_backlog);
    cJSON_AddItemToObject(resp, "webhook", webhook);

    char *json_str = cJSON_Print(resp);
    cJSON_Delete(resp);
    if (!json_str) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    return api_response_success(ctx, json_str, "OK");
}

aicam_result_t web_api_register_people_counting_module(void) {
    api_route_t routes[] = {
        {
            .path = API_PATH_PREFIX "/apps/people-counting/config",
            .method = "GET",
            .handler = pc_config_get_handler,
            .require_auth = AICAM_TRUE
        },
        {
            .path = API_PATH_PREFIX "/apps/people-counting/config",
            .method = "POST",
            .handler = pc_config_set_handler,
            .require_auth = AICAM_TRUE
        },
        {
            .path = API_PATH_PREFIX "/apps/people-counting/stats",
            .method = "GET",
            .handler = pc_stats_get_handler,
            .require_auth = AICAM_TRUE
        },
        {
            .path = API_PATH_PREFIX "/apps/people-counting/reset",
            .method = "POST",
            .handler = pc_reset_handler,
            .require_auth = AICAM_TRUE
        },
        {
            .path = API_PATH_PREFIX "/apps/people-counting/backlog",
            .method = "GET",
            .handler = pc_backlog_get_handler,
            .require_auth = AICAM_TRUE
        },
    };

    for (int i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        aicam_result_t ret = http_server_register_route(&routes[i]);
        if (ret != AICAM_OK) return ret;
    }

    return AICAM_OK;
}
