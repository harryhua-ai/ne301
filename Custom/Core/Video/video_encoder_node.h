/**
 * @file video_encoder_node.h
 * @brief Video Encoder Node
 * @details Simple encoder node based on driver_test.c device interaction with zero-copy
 */

#ifndef VIDEO_ENCODER_NODE_H
#define VIDEO_ENCODER_NODE_H

#include "video_pipeline.h"
#include "aicam_types.h"
#include "dev_manager.h"
#include "enc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Encoder Node Configuration ==================== */

/**
 * @brief Encoder node configuration
 */
typedef struct {
    uint32_t width;                       // Input width
    uint32_t height;                      // Input height
    uint32_t fps;                         // Frame rate
    uint32_t input_type;                  // Input format (JPEGENC_RGB565, etc.)
    uint32_t quality;                     // Encoding quality (0-100)
    uint32_t bitrate;                     // Target bitrate (kbps)
    uint32_t pipe_id;                     // Pipeline ID (1 or 2)
} video_encoder_config_t;

/**
 * @brief Encoder node statistics
 */
typedef struct {
    uint64_t frames_encoded;              // Total frames encoded
    uint64_t encode_errors;               // Encoding errors
    uint64_t avg_encode_time_us;          // Average encoding time
    uint64_t max_encode_time_us;          // Maximum encoding time
    uint64_t total_bytes_encoded;         // Total bytes encoded
    uint32_t avg_frame_size;              // Average frame size
} video_encoder_stats_t;

/**
 * @brief Encoder node private data
 */
typedef struct {
    device_t *encoder_dev;                // Encoder device handle
    video_encoder_config_t config;        // Encoder configuration
    video_encoder_stats_t stats;          // Encoder statistics
    enc_param_t enc_param;                // Encoder parameters
    aicam_bool_t is_initialized;          // Initialization status
    aicam_bool_t is_running;              // Running status
} video_encoder_node_data_t;

/* ==================== API Functions ==================== */

/**
 * @brief Create encoder node
 * @param name Node name
 * @param config Encoder configuration
 * @return Encoder node handle or NULL on error
 */
video_node_t* video_encoder_node_create(const char *name, const video_encoder_config_t *config);


/**
 * @brief Get default encoder configuration
 * @param config Configuration structure to fill
 */
void video_encoder_get_default_config(video_encoder_config_t *config);

/**
 * @brief Set encoder parameters
 * @param node Encoder node handle
 * @param config New configuration
 * @return Operation result
 */
aicam_result_t video_encoder_node_set_config(video_node_t *node, const video_encoder_config_t *config);

/**
 * @brief Get encoder parameters
 * @param node Encoder node handle
 * @param config Configuration structure to fill
 * @return Operation result
 */
aicam_result_t video_encoder_node_get_config(video_node_t *node, video_encoder_config_t *config);

/**
 * @brief Get encoder statistics
 * @param node Encoder node handle
 * @param stats Statistics structure to fill
 * @return Operation result
 */
aicam_result_t video_encoder_node_get_stats(video_node_t *node, video_encoder_stats_t *stats);

/**
 * @brief Reset encoder statistics
 * @param node Encoder node handle
 * @return Operation result
 */
aicam_result_t video_encoder_node_reset_stats(video_node_t *node);

/**
 * @brief Start encoder
 * @param node Encoder node handle
 * @return Operation result
 */
aicam_result_t video_encoder_node_start(video_node_t *node);

/**
 * @brief Stop encoder
 * @param node Encoder node handle
 * @return Operation result
 */
aicam_result_t video_encoder_node_stop(video_node_t *node);

/**
 * @brief Check if encoder is running
 * @param node Encoder node handle
 * @return Running status
 */
aicam_bool_t video_encoder_node_is_running(video_node_t *node);

/* ==================== Control Commands ==================== */

#define ENCODER_CMD_START_ENCODE         0x2001
#define ENCODER_CMD_STOP_ENCODE          0x2002
#define ENCODER_CMD_SET_QUALITY          0x2003
#define ENCODER_CMD_SET_BITRATE          0x2004
#define ENCODER_CMD_GET_PARAM            0x2005

#ifdef __cplusplus
}
#endif

#endif /* VIDEO_ENCODER_NODE_H */
