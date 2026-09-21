#ifndef LINE_COUNTING_CONFIG_H
#define LINE_COUNTING_CONFIG_H

#include <aicam_types.h>

#define LC_COUNTER_NAME_LEN      64
#define LC_TARGET_CLASS_NAME_LEN 32
#define LC_BACKLOG_CAPACITY_MAX  256

#define PC_TARGET_CLASS_NAME_LEN  32
#define PC_MODEL_NAME_LEN         64
#define PC_PP_TYPE_LEN            32

typedef struct {
    aicam_bool_t enable;
    uint16_t line_x1_permille, line_y1_permille;
    uint16_t line_x2_permille, line_y2_permille;
    uint16_t outside_x_permille, outside_y_permille;
    uint16_t conf_threshold_permille;
    uint16_t max_dist_permille;
    char target_class_name[PC_TARGET_CLASS_NAME_LEN];
    char model_name[PC_MODEL_NAME_LEN];
    char model_pp_type[PC_PP_TYPE_LEN];
    uint8_t  track_history_k;
    uint8_t  max_miss;
    uint8_t  k_confirm;
    uint16_t window_minutes;
    aicam_bool_t mqtt_report_enable;
    aicam_bool_t webhook_report_enable;
    aicam_bool_t tracks_report_enable;
    aicam_bool_t heat_grid_enable;
    uint16_t backlog_capacity;
} people_counting_config_t;

typedef struct {
    aicam_bool_t enable;
    char counter_name[LC_COUNTER_NAME_LEN];
    char target_class_name[LC_TARGET_CLASS_NAME_LEN];
    uint16_t line_x1_permille, line_y1_permille;
    uint16_t line_x2_permille, line_y2_permille;
    uint16_t outside_x_permille, outside_y_permille;
    uint16_t conf_threshold_permille;
    uint16_t max_dist_permille;
    uint8_t  track_history_k;
    uint8_t  max_miss;
    uint8_t  k_confirm;
    uint16_t window_minutes;
    aicam_bool_t mqtt_report_enable;
    aicam_bool_t webhook_report_enable;
    aicam_bool_t tracks_report_enable;
    aicam_bool_t heat_grid_enable;
    uint16_t backlog_capacity;
} line_counting_config_t;

void line_counting_config_defaults(line_counting_config_t *config);

void line_counting_config_map_from_people(const people_counting_config_t *legacy,
                                          line_counting_config_t *out);

aicam_bool_t line_counting_config_is_valid(const line_counting_config_t *config);

#endif
