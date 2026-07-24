/**
 * @file api_people_counting_module.c
 * @brief People Counting API Module — config get/set + stats + reset + backlog
 */

#include "api_people_counting_module.h"
#include "people_counting.h"
#include "pc_backlog.h"
#include "pc_types.h"           /* PC_K_MAX */
#include "web_api.h"
#include "web_server.h"
#include "json_config_mgr.h"
#include "buffer_mgr.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

/* ==================== Validation (spec §4.1, verbatim from plan) ==================== */

static aicam_result_t validate_pc_config(const people_counting_config_t* c, char* err, size_t err_len) {
    /* permille fields must be in [0, 1000] */
    #define IN_PM(v) ((v) <= 1000u)
    if (!IN_PM(c->line_x1_permille) || !IN_PM(c->line_y1_permille) ||
        !IN_PM(c->line_x2_permille) || !IN_PM(c->line_y2_permille) ||
        !IN_PM(c->outside_x_permille) || !IN_PM(c->outside_y_permille)) {
        snprintf(err, err_len, "line/outside coordinates must be in [0,1000] permille");
        return AICAM_ERROR_INVALID_PARAM;
    }
    /* line must be non-degenerate in normalized space (len >= 0.01) */
    float dx = (c->line_x2_permille - c->line_x1_permille) / 1000.0f;
    float dy = (c->line_y2_permille - c->line_y1_permille) / 1000.0f;
    if (dx*dx + dy*dy < 0.01f * 0.01f) {
        snprintf(err, err_len, "line is degenerate (L1≈L2)");
        return AICAM_ERROR_INVALID_PARAM;
    }
    /* outside point must NOT lie on the line (dot product != 0) */
    if (c->outside_x_permille == c->line_x1_permille &&
        c->outside_y_permille == c->line_y1_permille) {
        snprintf(err, err_len, "outside point coincides with L1");
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (c->conf_threshold_permille > 1000u) {
        snprintf(err, err_len, "conf_threshold_permille must be <= 1000");
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (c->max_dist_permille == 0u || c->max_dist_permille > 1000u) {
        snprintf(err, err_len, "max_dist_permille must be in (0, 1000]");
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (c->track_history_k < 4u || c->track_history_k > PC_K_MAX) {
        snprintf(err, err_len, "track_history_k must be in [4, %u]", PC_K_MAX);
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (c->k_confirm < 3u || c->k_confirm > c->track_history_k) {
        snprintf(err, err_len, "k_confirm must be in [3, track_history_k]");
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (c->max_miss == 0u) {
        snprintf(err, err_len, "max_miss must be >= 1");
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (c->window_minutes == 0u || c->window_minutes > 60u) {
        snprintf(err, err_len, "window_minutes must be in [1, 60]");
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (c->target_class_name[0] == '\0') {
        snprintf(err, err_len, "target_class_name must be non-empty");
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (c->backlog_capacity < 6u || c->backlog_capacity > 96u) {
        snprintf(err, err_len, "backlog_capacity must be in [6, 96]");
        return AICAM_ERROR_INVALID_PARAM;
    }
    return AICAM_OK;
    #undef IN_PM
}

/* ==================== Helper: serialize config to cJSON ==================== */

static cJSON* pc_config_to_json(const people_counting_config_t* cfg) {
    cJSON* o = cJSON_CreateObject();
    if (!o) return NULL;
    cJSON_AddBoolToObject(o, "enable", cfg->enable);
    cJSON_AddNumberToObject(o, "line_x1_permille", cfg->line_x1_permille);
    cJSON_AddNumberToObject(o, "line_y1_permille", cfg->line_y1_permille);
    cJSON_AddNumberToObject(o, "line_x2_permille", cfg->line_x2_permille);
    cJSON_AddNumberToObject(o, "line_y2_permille", cfg->line_y2_permille);
    cJSON_AddNumberToObject(o, "outside_x_permille", cfg->outside_x_permille);
    cJSON_AddNumberToObject(o, "outside_y_permille", cfg->outside_y_permille);
    cJSON_AddNumberToObject(o, "conf_threshold_permille", cfg->conf_threshold_permille);
    cJSON_AddNumberToObject(o, "max_dist_permille", cfg->max_dist_permille);
    cJSON_AddStringToObject(o, "target_class_name", cfg->target_class_name);
    cJSON_AddStringToObject(o, "model_name", cfg->model_name);
    cJSON_AddStringToObject(o, "model_pp_type", cfg->model_pp_type);
    cJSON_AddNumberToObject(o, "track_history_k", cfg->track_history_k);
    cJSON_AddNumberToObject(o, "max_miss", cfg->max_miss);
    cJSON_AddNumberToObject(o, "k_confirm", cfg->k_confirm);
    cJSON_AddNumberToObject(o, "window_minutes", cfg->window_minutes);
    cJSON_AddBoolToObject(o, "mqtt_report_enable", cfg->mqtt_report_enable);
    cJSON_AddBoolToObject(o, "webhook_report_enable", cfg->webhook_report_enable);
    cJSON_AddBoolToObject(o, "tracks_report_enable", cfg->tracks_report_enable);
    cJSON_AddBoolToObject(o, "heat_grid_enable", cfg->heat_grid_enable);
    cJSON_AddNumberToObject(o, "backlog_capacity", cfg->backlog_capacity);
    return o;
}

/* Helper: apply JSON fields onto an existing config (only updates fields present in JSON). */
static void pc_config_apply_json(people_counting_config_t* cfg, const cJSON* req) {
    cJSON* item;
    if ((item = cJSON_GetObjectItem(req, "enable")) && cJSON_IsBool(item))
        cfg->enable = cJSON_IsTrue(item) ? AICAM_TRUE : AICAM_FALSE;
    if ((item = cJSON_GetObjectItem(req, "line_x1_permille")) && cJSON_IsNumber(item))
        cfg->line_x1_permille = (uint16_t)item->valueint;
    if ((item = cJSON_GetObjectItem(req, "line_y1_permille")) && cJSON_IsNumber(item))
        cfg->line_y1_permille = (uint16_t)item->valueint;
    if ((item = cJSON_GetObjectItem(req, "line_x2_permille")) && cJSON_IsNumber(item))
        cfg->line_x2_permille = (uint16_t)item->valueint;
    if ((item = cJSON_GetObjectItem(req, "line_y2_permille")) && cJSON_IsNumber(item))
        cfg->line_y2_permille = (uint16_t)item->valueint;
    if ((item = cJSON_GetObjectItem(req, "outside_x_permille")) && cJSON_IsNumber(item))
        cfg->outside_x_permille = (uint16_t)item->valueint;
    if ((item = cJSON_GetObjectItem(req, "outside_y_permille")) && cJSON_IsNumber(item))
        cfg->outside_y_permille = (uint16_t)item->valueint;
    if ((item = cJSON_GetObjectItem(req, "conf_threshold_permille")) && cJSON_IsNumber(item))
        cfg->conf_threshold_permille = (uint16_t)item->valueint;
    if ((item = cJSON_GetObjectItem(req, "max_dist_permille")) && cJSON_IsNumber(item))
        cfg->max_dist_permille = (uint16_t)item->valueint;
    if ((item = cJSON_GetObjectItem(req, "target_class_name")) && cJSON_IsString(item)) {
        strncpy(cfg->target_class_name, item->valuestring, sizeof(cfg->target_class_name) - 1);
        cfg->target_class_name[sizeof(cfg->target_class_name) - 1] = '\0';
    }
    if ((item = cJSON_GetObjectItem(req, "model_name")) && cJSON_IsString(item)) {
        strncpy(cfg->model_name, item->valuestring, sizeof(cfg->model_name) - 1);
        cfg->model_name[sizeof(cfg->model_name) - 1] = '\0';
    }
    if ((item = cJSON_GetObjectItem(req, "model_pp_type")) && cJSON_IsString(item)) {
        strncpy(cfg->model_pp_type, item->valuestring, sizeof(cfg->model_pp_type) - 1);
        cfg->model_pp_type[sizeof(cfg->model_pp_type) - 1] = '\0';
    }
    if ((item = cJSON_GetObjectItem(req, "track_history_k")) && cJSON_IsNumber(item))
        cfg->track_history_k = (uint8_t)item->valueint;
    if ((item = cJSON_GetObjectItem(req, "max_miss")) && cJSON_IsNumber(item))
        cfg->max_miss = (uint8_t)item->valueint;
    if ((item = cJSON_GetObjectItem(req, "k_confirm")) && cJSON_IsNumber(item))
        cfg->k_confirm = (uint8_t)item->valueint;
    if ((item = cJSON_GetObjectItem(req, "window_minutes")) && cJSON_IsNumber(item))
        cfg->window_minutes = (uint16_t)item->valueint;
    if ((item = cJSON_GetObjectItem(req, "mqtt_report_enable")) && cJSON_IsBool(item))
        cfg->mqtt_report_enable = cJSON_IsTrue(item) ? AICAM_TRUE : AICAM_FALSE;
    if ((item = cJSON_GetObjectItem(req, "webhook_report_enable")) && cJSON_IsBool(item))
        cfg->webhook_report_enable = cJSON_IsTrue(item) ? AICAM_TRUE : AICAM_FALSE;
    if ((item = cJSON_GetObjectItem(req, "tracks_report_enable")) && cJSON_IsBool(item))
        cfg->tracks_report_enable = cJSON_IsTrue(item) ? AICAM_TRUE : AICAM_FALSE;
    if ((item = cJSON_GetObjectItem(req, "heat_grid_enable")) && cJSON_IsBool(item))
        cfg->heat_grid_enable = cJSON_IsTrue(item) ? AICAM_TRUE : AICAM_FALSE;
    if ((item = cJSON_GetObjectItem(req, "backlog_capacity")) && cJSON_IsNumber(item))
        cfg->backlog_capacity = (uint16_t)item->valueint;
}

/* ==================== Handlers ==================== */

static aicam_result_t pc_config_get_handler(http_handler_context_t *ctx)
{
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }

    people_counting_config_t cfg;
    if (json_config_get_people_counting_config(&cfg) != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get people-counting config");
    }

    cJSON* resp = pc_config_to_json(&cfg);
    if (!resp) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to build response");
    }

    char* json_str = cJSON_Print(resp);
    cJSON_Delete(resp);
    if (!json_str) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }

    return api_response_success(ctx, json_str, "OK");
}

static aicam_result_t pc_config_set_handler(http_handler_context_t *ctx)
{
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    if (!web_api_verify_content_type(ctx, "application/json")) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid Content-Type");
    }

    cJSON* req = web_api_parse_body(ctx);
    if (!req) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON");
    }

    /* Start from current config so omitted fields are preserved */
    people_counting_config_t cfg;
    if (json_config_get_people_counting_config(&cfg) != AICAM_OK) {
        cJSON_Delete(req);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get current config");
    }
    pc_config_apply_json(&cfg, req);
    cJSON_Delete(req);

    char err[128];
    if (validate_pc_config(&cfg, err, sizeof(err)) != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, err);
    }

    if (json_config_set_people_counting_config(&cfg) != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to save people-counting config");
    }

    /* Respond with the canonicalized (re-serialized) config */
    cJSON* resp = pc_config_to_json(&cfg);
    if (!resp) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to build response");
    }
    char* json_str = cJSON_Print(resp);
    cJSON_Delete(resp);
    if (!json_str) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }

    return api_response_success(ctx, json_str, "People-counting configuration updated");
}

static aicam_result_t pc_stats_get_handler(http_handler_context_t *ctx)
{
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }

    const people_counting_stats_t* s = people_counting_get_stats();
    if (!s) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "People-counting not initialized");
    }

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "window_in", s->window_in);
    cJSON_AddNumberToObject(resp, "window_out", s->window_out);
    cJSON_AddNumberToObject(resp, "window_start_ts", s->window_start_ts);
    cJSON_AddNumberToObject(resp, "total_in", s->total_in);
    cJSON_AddNumberToObject(resp, "total_out", s->total_out);
    cJSON_AddNumberToObject(resp, "boot_id", s->boot_id);
    cJSON_AddStringToObject(resp, "boot_id_kind", s->boot_id_kind ? s->boot_id_kind : "monotonic");
    cJSON_AddNumberToObject(resp, "last_report_ts", s->last_report_ts);
    cJSON_AddNumberToObject(resp, "dropped_windows_mqtt", s->dropped_windows_mqtt);
    cJSON_AddNumberToObject(resp, "dropped_windows_webhook", s->dropped_windows_webhook);
    /* heat grid — 16x16 array of uint32 */
    cJSON* heat = cJSON_CreateArray();
    for (int i = 0; i < 16 * 16; ++i) {
        cJSON_AddItemToArray(heat, cJSON_CreateNumber(s->heat[i]));
    }
    cJSON_AddItemToObject(resp, "heat_grid", heat);
    /* Debug diagnostics */
    people_counting_debug_t dbg = people_counting_get_debug();
    cJSON_AddNumberToObject(resp, "dbg_sub_calls", dbg.sub_calls);
    cJSON_AddNumberToObject(resp, "dbg_matched_person", dbg.matched_person);
    cJSON_AddNumberToObject(resp, "dbg_last_type", dbg.last_type);
    cJSON_AddNumberToObject(resp, "dbg_last_nb_detect", dbg.last_nb_detect);
    cJSON_AddStringToObject(resp, "dbg_last_class", dbg.last_class);
    cJSON_AddNumberToObject(resp, "dbg_last_det_x", dbg.last_det_x_permille);
    cJSON_AddNumberToObject(resp, "dbg_last_det_y", dbg.last_det_y_permille);

    char* json_str = cJSON_Print(resp);
    cJSON_Delete(resp);
    if (!json_str) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }

    return api_response_success(ctx, json_str, "OK");
}

static aicam_result_t pc_reset_handler(http_handler_context_t *ctx)
{
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }

    people_counting_reset_totals();

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", 1);
    cJSON_AddStringToObject(resp, "message", "Totals reset to zero");
    char* json_str = cJSON_Print(resp);
    cJSON_Delete(resp);
    if (!json_str) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }

    return api_response_success(ctx, json_str, "Totals reset");
}

static aicam_result_t pc_backlog_get_handler(http_handler_context_t *ctx)
{
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }

    cJSON* resp = cJSON_CreateObject();
    cJSON* mqtt = cJSON_CreateObject();
    cJSON_AddNumberToObject(mqtt, "count", backlog_count(BACKLOG_MQTT));
    cJSON_AddItemToObject(resp, "mqtt", mqtt);
    cJSON* webhook = cJSON_CreateObject();
    cJSON_AddNumberToObject(webhook, "count", backlog_count(BACKLOG_WEBHOOK));
    cJSON_AddItemToObject(resp, "webhook", webhook);

    char* json_str = cJSON_Print(resp);
    cJSON_Delete(resp);
    if (!json_str) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }

    return api_response_success(ctx, json_str, "OK");
}

/* ==================== Module Registration ==================== */

aicam_result_t web_api_register_people_counting_module(void)
{
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
