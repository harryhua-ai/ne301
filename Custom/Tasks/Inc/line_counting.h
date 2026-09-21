#ifndef LINE_COUNTING_H
#define LINE_COUNTING_H

#include <stdint.h>
#include <stddef.h>
#include "lc_types.h"
#include "lc_tracker.h"
#include "lc_line_cross.h"
#include "pp.h"
#include "line_counting_config.h"

#ifndef __LC_TEST__
#include "nn.h"
#endif

#define LC_EVENTS_RING_CAPACITY 50u
#define LC_FRAME_MAX_DETECTIONS 64u

typedef enum {
    LC_STATE_DISABLED = 0,
    LC_STATE_RUNNING,
    LC_STATE_UNSUPPORTED_MODEL,
    LC_STATE_TARGET_CLASS_INVALID
} line_counting_state_t;

typedef enum {
    LC_REASON_NONE = 0,
    LC_REASON_MODEL_NOT_LOADED,
    LC_REASON_CLASS_METADATA_UNAVAILABLE
} line_counting_reason_t;

typedef enum {
    LC_DIRECTION_IN = 0,
    LC_DIRECTION_OUT
} lc_direction_t;

typedef struct {
    uint32_t       sequence;
    uint32_t       timestamp_ms;
    uint32_t       track_id;
    lc_direction_t direction;
} line_count_event_t;

typedef struct {
    uint32_t  generation;
    uint8_t   loaded;
    pp_type_t result_type;
    int16_t   target_class_index;
    char      target_class_name[LC_TARGET_CLASS_NAME_LEN];
} lc_model_binding_t;

typedef struct {
    line_counting_state_t  state;
    line_counting_reason_t reason;
    lc_model_binding_t     binding;
    char                   counter_name[LC_COUNTER_NAME_LEN];
    uint16_t               tracker_active;
} line_counting_status_t;

typedef struct {
    uint32_t window_in;
    uint32_t window_out;
    uint32_t total_in;
    uint32_t total_out;
    uint32_t window_start_ms;
} line_counting_stats_t;

typedef struct {
    uint32_t start_ms;
    uint32_t end_ms;
    uint32_t in;
    uint32_t out;
} lc_window_summary_t;

#define LC_HEAT_GRID_SIZE 256u
#define LC_HEAT_GRID_DIM  16u
#define LC_REPORT_MAX_TRACKS 32u

typedef struct {
    lc_window_summary_t summary;
    uint32_t            heat[LC_HEAT_GRID_SIZE];
    uint8_t             heat_valid;
} lc_window_close_t;

typedef struct {
    char     device_id[32];
    uint32_t boot_id;
    uint32_t report_seq;
    uint8_t  clock_valid;
    char     reported_at[40];
    char     window_start_time[40];
    char     window_end_time[40];
    uint32_t window_duration_sec;
    uint32_t window_in;
    uint32_t window_out;
    uint32_t total_in;
    uint32_t total_out;
    char     counter_name[LC_COUNTER_NAME_LEN];
    char     target_class_name[LC_TARGET_CLASS_NAME_LEN];
    char     model_name[64];
    char     model_version[32];
    float    line_x1;
    float    line_y1;
    float    line_x2;
    float    line_y2;
    float    outside_x;
    float    outside_y;
    float    confidence_threshold;
    uint8_t  tracks_report_enable;
    uint8_t  heat_grid_enable;
    const lc_track_record_t* const* tracks;
    uint16_t n_tracks;
    const uint32_t *heat;
} lc_report_snapshot_t;

typedef struct {
    uint8_t   loaded;
    uint32_t  generation;
    pp_type_t result_type;
    uint16_t  num_classes;
} lc_runtime_model_info_t;

typedef struct {
    float       x, y, w, h, conf;
    const char *class_name;
} lc_det_t;

typedef struct {
    int32_t         result_type;
    uint8_t         nb_detect;
    const lc_det_t *detects;
} lc_frame_input_t;

typedef struct {
    void *user;
    aicam_result_t (*get_model_info)(void *user, lc_runtime_model_info_t *info);
    aicam_result_t (*get_class_name)(void *user, uint16_t index, char *buf, size_t buf_size);
    uint32_t (*now_ms)(void *user);
    aicam_result_t (*load_totals)(void *user, uint32_t *total_in, uint32_t *total_out);
    aicam_result_t (*save_totals)(void *user, uint32_t total_in, uint32_t total_out);
} lc_app_ops_t;

typedef struct {
    lc_app_ops_t           ops;
    line_counting_config_t cfg;
    lc_model_binding_t     binding;
    uint8_t                binding_valid;
    line_counting_state_t  state;
    line_counting_reason_t reason;
    lc_tracker_t          *tracker;
    lc_line_cross_t       *line;
    uint32_t               next_id_carry;
    uint32_t               window_in;
    uint32_t               window_out;
    uint32_t               total_in;
    uint32_t               total_out;
    uint32_t               window_start_ms;
    uint32_t               event_seq;
    line_count_event_t     events[LC_EVENTS_RING_CAPACITY];
    uint16_t               events_head;
    uint16_t               events_count;
    uint32_t               heat[LC_HEAT_GRID_SIZE];
} lc_app_t;

void           lc_app_init(lc_app_t *app, const lc_app_ops_t *ops,
                           const line_counting_config_t *initial);
aicam_result_t lc_app_on_ai_result(lc_app_t *app, const lc_frame_input_t *frame,
                                   uint32_t ts_ms);
aicam_result_t lc_app_apply_config(lc_app_t *app, const line_counting_config_t *candidate,
                                   lc_window_summary_t *closed_out);
aicam_bool_t   lc_app_tick_window(lc_app_t *app, uint32_t now_ms,
                                  lc_window_close_t *closed_out,
                                  lc_track_record_t ***out_records,
                                  uint16_t *out_n_records);
aicam_result_t lc_app_reset(lc_app_t *app, uint32_t now_ms);
void           lc_app_get_status(const lc_app_t *app, line_counting_status_t *out);
void           lc_app_get_stats(const lc_app_t *app, line_counting_stats_t *out);
uint16_t       lc_app_get_events(const lc_app_t *app, line_count_event_t *out,
                                 uint16_t max_events);

size_t lc_report_build_v1(const lc_report_snapshot_t *snap, char *out, size_t cap);

#ifndef __LC_TEST__
aicam_result_t line_counting_init(void);
void           line_counting_on_ai_result(const nn_result_t *result, uint32_t timestamp_ms);
aicam_bool_t   line_counting_is_enabled(void);
aicam_result_t line_counting_get_status(line_counting_status_t *out);
aicam_result_t line_counting_get_stats(line_counting_stats_t *out);
aicam_result_t line_counting_get_events(line_count_event_t *out, uint16_t max_events,
                                        uint16_t *out_n);
aicam_result_t line_counting_apply_config(const line_counting_config_t *candidate);
aicam_result_t line_counting_reset(void);
#endif

#endif
