#include "line_counting_config.h"
#include <string.h>
#include <stdio.h>

void line_counting_config_defaults(line_counting_config_t *config)
{
    if (!config) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->enable = AICAM_FALSE;
    snprintf(config->counter_name, sizeof(config->counter_name), "客流统计");
    snprintf(config->target_class_name, sizeof(config->target_class_name), "person");
    config->line_x1_permille = 200;
    config->line_y1_permille = 500;
    config->line_x2_permille = 800;
    config->line_y2_permille = 500;
    config->outside_x_permille = 500;
    config->outside_y_permille = 200;
    config->conf_threshold_permille = 250;
    config->max_dist_permille = 250;
    config->track_history_k = 8;
    config->max_miss = 5;
    config->k_confirm = 5;
    config->window_minutes = 5;
    config->mqtt_report_enable = AICAM_TRUE;
    config->webhook_report_enable = AICAM_FALSE;
    config->tracks_report_enable = AICAM_TRUE;
    config->heat_grid_enable = AICAM_FALSE;
    config->backlog_capacity = 24;
}

void line_counting_config_map_from_people(const people_counting_config_t *legacy,
                                          line_counting_config_t *out)
{
    if (!legacy || !out) {
        return;
    }
    line_counting_config_defaults(out);
    out->enable = legacy->enable;
    out->line_x1_permille = legacy->line_x1_permille;
    out->line_y1_permille = legacy->line_y1_permille;
    out->line_x2_permille = legacy->line_x2_permille;
    out->line_y2_permille = legacy->line_y2_permille;
    out->outside_x_permille = legacy->outside_x_permille;
    out->outside_y_permille = legacy->outside_y_permille;
    out->conf_threshold_permille = legacy->conf_threshold_permille;
    out->max_dist_permille = legacy->max_dist_permille;
    snprintf(out->target_class_name, sizeof(out->target_class_name), "%s",
             legacy->target_class_name);
    out->track_history_k = legacy->track_history_k;
    out->max_miss = legacy->max_miss;
    out->k_confirm = legacy->k_confirm;
    out->window_minutes = legacy->window_minutes;
    out->mqtt_report_enable = legacy->mqtt_report_enable;
    out->webhook_report_enable = legacy->webhook_report_enable;
    out->tracks_report_enable = legacy->tracks_report_enable;
    out->heat_grid_enable = legacy->heat_grid_enable;
    out->backlog_capacity = legacy->backlog_capacity;
}

aicam_bool_t line_counting_config_is_valid(const line_counting_config_t *config)
{
    if (!config) {
        return AICAM_FALSE;
    }
    if (config->counter_name[0] == '\0') {
        return AICAM_FALSE;
    }
    if (config->target_class_name[0] == '\0') {
        return AICAM_FALSE;
    }
    if (config->line_x1_permille > 1000 || config->line_y1_permille > 1000 ||
        config->line_x2_permille > 1000 || config->line_y2_permille > 1000 ||
        config->outside_x_permille > 1000 || config->outside_y_permille > 1000) {
        return AICAM_FALSE;
    }
    if (config->line_x1_permille == config->line_x2_permille &&
        config->line_y1_permille == config->line_y2_permille) {
        return AICAM_FALSE;
    }
    if (config->conf_threshold_permille > 1000) {
        return AICAM_FALSE;
    }
    if (config->max_dist_permille == 0 || config->max_dist_permille > 1000) {
        return AICAM_FALSE;
    }
    if (config->track_history_k < 4 || config->track_history_k > 16) {
        return AICAM_FALSE;
    }
    if (config->max_miss == 0) {
        return AICAM_FALSE;
    }
    if (config->k_confirm == 0) {
        return AICAM_FALSE;
    }
    if (config->window_minutes == 0 || config->window_minutes > 1440) {
        return AICAM_FALSE;
    }
    if (config->backlog_capacity == 0 || config->backlog_capacity > LC_BACKLOG_CAPACITY_MAX) {
        return AICAM_FALSE;
    }
    return AICAM_TRUE;
}
