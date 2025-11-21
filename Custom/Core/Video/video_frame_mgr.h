/**
 * @file video_frame_mgr.h
 * @brief Zero-Copy Video Frame Manager
 * @details Direct use of hardware buffers without copying
 */

#ifndef VIDEO_FRAME_MGR_H
#define VIDEO_FRAME_MGR_H

#include "aicam_types.h"
#include "video_pipeline.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Zero-Copy Frame Management ==================== */

/**
 * @brief Create a zero-copy frame that directly uses hardware buffer
 * @param info Frame information
 * @param hw_buffer Hardware buffer pointer (directly used, no copying)
 * @param hw_buffer_size Hardware buffer size
 * @param return_callback Callback to return buffer to hardware when frame is released
 * @return Frame pointer, NULL on failure
 */
video_frame_t* video_frame_create_zero_copy(const video_frame_info_t *info,
                                           uint8_t *hw_buffer,
                                           uint32_t hw_buffer_size,
                                           void (*return_callback)(uint8_t *buffer));

/**
 * @brief Increment frame reference count
 * @param frame Frame to reference
 * @return New reference count
 */
uint32_t video_frame_ref(video_frame_t *frame);

/**
 * @brief Decrement frame reference count and free if zero
 * @param frame Frame to dereference
 * @return Remaining reference count (0 if freed)
 */
uint32_t video_frame_unref(video_frame_t *frame);

/**
 * @brief Get current reference count
 * @param frame Frame to check
 * @return Current reference count
 */
uint32_t video_frame_get_ref_count(const video_frame_t *frame);

#ifdef __cplusplus
}
#endif

#endif /* VIDEO_FRAME_MGR_H */
