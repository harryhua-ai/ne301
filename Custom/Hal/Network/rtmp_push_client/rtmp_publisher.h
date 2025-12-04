/**
 * @file rtmp_publisher.h
 * @brief Simple RTMP Publisher Library (No Encryption)
 * @details A simplified RTMP streaming library for publishing H.264 video streams
 */

#ifndef RTMP_PUBLISHER_H
#define RTMP_PUBLISHER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* ==================== Error Codes ==================== */

typedef enum {
    RTMP_PUB_OK = 0,
    RTMP_PUB_ERR_INVALID_ARG = -1,
    RTMP_PUB_ERR_INIT_FAILED = -2,
    RTMP_PUB_ERR_CONNECT_FAILED = -3,
    RTMP_PUB_ERR_PUBLISH_FAILED = -4,
    RTMP_PUB_ERR_NOT_CONNECTED = -5,
    RTMP_PUB_ERR_SEND_FAILED = -6,
    RTMP_PUB_ERR_MEMORY = -7,
    RTMP_PUB_ERR_TIMEOUT = -8,
    RTMP_PUB_ERR_UNKNOWN = -99
} rtmp_pub_err_t;

/* ==================== Configuration ==================== */

/**
 * @brief RTMP publisher configuration
 */
typedef struct {
    char url[128];                  // RTMP URL (e.g., "rtmp://example.com/live/stream")
    uint32_t width;                // Video width
    uint32_t height;               // Video height
    uint32_t fps;                  // Frame rate
    uint32_t timeout_ms;           // Connection timeout in milliseconds (default: 5000)
    bool enable_audio;             // Enable audio (currently not supported)
} rtmp_pub_config_t;

/**
 * @brief RTMP publisher statistics
 */
typedef struct {
    uint64_t frames_sent;          // Total frames sent
    uint64_t bytes_sent;            // Total bytes sent
    uint64_t errors;                // Total errors
    uint32_t last_frame_size;       // Last frame size
    uint32_t avg_frame_size;       // Average frame size
} rtmp_pub_stats_t;

/* ==================== Handle ==================== */

typedef struct rtmp_publisher rtmp_publisher_t;

/* ==================== API Functions ==================== */

/**
 * @brief Create RTMP publisher instance
 * @param config Publisher configuration
 * @return Publisher handle or NULL on error
 */
rtmp_publisher_t* rtmp_publisher_create(const rtmp_pub_config_t *config);

/**
 * @brief Destroy RTMP publisher instance
 * @param pub Publisher handle
 */
void rtmp_publisher_destroy(rtmp_publisher_t *pub);

/**
 * @brief Connect to RTMP server
 * @param pub Publisher handle
 * @return Error code
 */
rtmp_pub_err_t rtmp_publisher_connect(rtmp_publisher_t *pub);

/**
 * @brief Disconnect from RTMP server
 * @param pub Publisher handle
 */
void rtmp_publisher_disconnect(rtmp_publisher_t *pub);

/**
 * @brief Check if connected
 * @param pub Publisher handle
 * @return true if connected, false otherwise
 */
bool rtmp_publisher_is_connected(rtmp_publisher_t *pub);

/**
 * @brief Send H.264 video frame
 * @param pub Publisher handle
 * @param data Frame data (H.264 NAL units)
 * @param size Frame size in bytes
 * @param is_keyframe true if keyframe (I-frame), false for P-frame
 * @param timestamp_ms Timestamp in milliseconds (relative to stream start)
 * @return Error code
 */
rtmp_pub_err_t rtmp_publisher_send_video_frame(rtmp_publisher_t *pub,
                                                const uint8_t *data,
                                                uint32_t size,
                                                bool is_keyframe,
                                                uint32_t timestamp_ms);

/**
 * @brief Send H.264 SPS/PPS (should be called before sending frames)
 * @param pub Publisher handle
 * @param sps SPS data
 * @param sps_size SPS size
 * @param pps PPS data
 * @param pps_size PPS size
 * @return Error code
 */
rtmp_pub_err_t rtmp_publisher_send_sps_pps(rtmp_publisher_t *pub,
                                            const uint8_t *sps,
                                            uint32_t sps_size,
                                            const uint8_t *pps,
                                            uint32_t pps_size);

/**
 * @brief Get publisher statistics
 * @param pub Publisher handle
 * @param stats Statistics structure to fill
 * @return Error code
 */
rtmp_pub_err_t rtmp_publisher_get_stats(rtmp_publisher_t *pub, rtmp_pub_stats_t *stats);

/**
 * @brief Reset publisher statistics
 * @param pub Publisher handle
 */
void rtmp_publisher_reset_stats(rtmp_publisher_t *pub);

/**
 * @brief Set RTMP chunk size for this publisher
 * @param pub Publisher handle
 * @param chunk_size Chunk size in bytes (1–65536 recommended)
 * @return Error code
 */
rtmp_pub_err_t rtmp_publisher_set_chunk_size(rtmp_publisher_t *pub, uint32_t chunk_size);

/**
 * @brief Get current RTMP chunk size used by this publisher
 * @param pub Publisher handle
 * @return Chunk size in bytes (0 if pub is NULL or not initialized)
 */
uint32_t rtmp_publisher_get_chunk_size(rtmp_publisher_t *pub);

/**
 * @brief Send onMetaData script tag with video metadata (width/height/fps, etc.)
 * @param pub Publisher handle
 * @return Error code
 */
rtmp_pub_err_t rtmp_publisher_send_metadata(rtmp_publisher_t *pub);

/**
 * @brief Get default configuration
 * @param config Configuration structure to fill
 */
void rtmp_publisher_get_default_config(rtmp_pub_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* RTMP_PUBLISHER_H */

