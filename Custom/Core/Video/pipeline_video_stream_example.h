/**
 * @file pipeline_video_stream_example.h
 * @brief Complete Pipeline Video Stream Example
 * @details Header for complete zero-copy video pipeline examples
 */

#ifndef PIPELINE_VIDEO_STREAM_EXAMPLE_H
#define PIPELINE_VIDEO_STREAM_EXAMPLE_H

#include "aicam_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Pipeline Configuration ==================== */

/**
 * @brief Pipeline configuration structure
 */
typedef struct {
    uint32_t width;                       // Video width
    uint32_t height;                      // Video height
    uint32_t fps;                         // Frame rate
    uint32_t duration_seconds;            // Stream duration in seconds
    uint32_t quality;                     // Encoding quality (0-100)
    aicam_bool_t enable_stats;            // Enable statistics logging
    aicam_bool_t enable_debug;            // Enable debug logging
} pipeline_config_t;

/**
 * @brief Pipeline statistics
 */
typedef struct {
    uint64_t total_frames_captured;       // Total frames captured by camera
    uint64_t total_frames_encoded;        // Total frames encoded
    uint64_t total_bytes_encoded;         // Total bytes encoded
    uint64_t pipeline_errors;             // Pipeline errors
    uint64_t start_time_ms;               // Pipeline start time
    uint64_t end_time_ms;                 // Pipeline end time
    uint32_t avg_fps;                     // Average FPS achieved
    uint32_t avg_bitrate_kbps;            // Average bitrate in kbps
} pipeline_stats_t;

/* ==================== Pipeline Management Functions ==================== */

/**
 * @brief Initialize pipeline configuration with default values
 * @param config Configuration structure to initialize
 */
void pipeline_get_default_config(pipeline_config_t *config);

/**
 * @brief Initialize video pipeline
 * @param config Pipeline configuration
 * @return AICAM_OK on success
 */
aicam_result_t pipeline_init(const pipeline_config_t *config);

/**
 * @brief Start video pipeline
 * @return AICAM_OK on success
 */
aicam_result_t pipeline_start(void);

/**
 * @brief Stop video pipeline
 * @return AICAM_OK on success
 */
aicam_result_t pipeline_stop(void);

/**
 * @brief Deinitialize video pipeline
 */
void pipeline_deinit(void);

/**
 * @brief Get pipeline statistics
 * @param stats Statistics structure to fill
 * @return AICAM_OK on success
 */
aicam_result_t pipeline_get_stats(pipeline_stats_t *stats);

/**
 * @brief Print pipeline statistics
 */
void pipeline_print_stats(void);

/* ==================== Pipeline Control Functions ==================== */

/**
 * @brief Check if pipeline is running
 * @return Running status
 */
aicam_bool_t pipeline_is_running(void);

/**
 * @brief Check if pipeline is initialized
 * @return Initialization status
 */
aicam_bool_t pipeline_is_initialized(void);

/**
 * @brief Set pipeline configuration
 * @param config New configuration
 * @return AICAM_OK on success
 */
aicam_result_t pipeline_set_config(const pipeline_config_t *config);

/* ==================== Example Functions ==================== */

/**
 * @brief Complete pipeline video stream example
 */
void complete_pipeline_video_stream_example(void);

/**
 * @brief High-performance pipeline example with different configurations
 */
void high_performance_pipeline_example(void);

/**
 * @brief Run all pipeline examples
 */
void run_pipeline_examples(void);

#ifdef __cplusplus
}
#endif

#endif /* PIPELINE_VIDEO_STREAM_EXAMPLE_H */
