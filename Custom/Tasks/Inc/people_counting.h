/* Custom/Tasks/Inc/people_counting.h */
#ifndef PEOPLE_COUNTING_H
#define PEOPLE_COUNTING_H
#include "aicam_types.h"
#include "nn.h"   /* nn_result_t */

typedef struct {
    uint32_t window_in, window_out;
    uint32_t window_start_ts;
    uint32_t heat[16*16];
    uint32_t total_in, total_out;
    uint32_t boot_id;
    const char* boot_id_kind;   /* "rtc" or "monotonic" */
    uint32_t last_report_ts;
    uint32_t dropped_windows_mqtt;
    uint32_t dropped_windows_webhook;
} people_counting_stats_t;

aicam_result_t people_counting_init(void);
void people_counting_on_ai_result(const nn_result_t* result, uint32_t timestamp_ms);
const people_counting_stats_t* people_counting_get_stats(void);
void people_counting_reset_totals(void);

/**
 * @brief Whether people counting is enabled and thus needs the AI pipeline
 *        running continuously, independent of any video viewer.
 * @details Consumed by the AI service's pipeline-hold reconciliation so that
 *          counting keeps working headless (no web/RTSP/RTMP viewer attached).
 */
aicam_bool_t people_counting_is_enabled(void);

/**
 * @brief Draw the people-counting overlay (count line + IN/OUT text) on a frame buffer.
 * @param fb Frame buffer (RGB565)
 * @param w  Frame buffer width (pixels)
 * @param h  Frame buffer height (pixels)
 * Self-guards: no-ops if people-counting disabled or draw service unavailable.
 */
void people_counting_draw_overlay(uint8_t *fb, int w, int h);

/* Debug diagnostics — exposed via stats API */
typedef struct {
    uint32_t sub_calls;
    uint32_t matched_person;
    int      last_type;
    int      last_nb_detect;
    char     last_class[32];
    uint32_t last_det_x_permille;
    uint32_t last_det_y_permille;
} people_counting_debug_t;
people_counting_debug_t people_counting_get_debug(void);

#endif
