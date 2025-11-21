/**
 * @file video_frame_mgr.c
 * @brief Zero-Copy Video Frame Manager Implementation
 * @details Direct use of hardware buffers without copying
 */

#include "video_frame_mgr.h"
#include "buffer_mgr.h"
#include "debug.h"
#include <string.h>
#include <stdlib.h>

/* ==================== Zero-Copy Frame Structure ==================== */

/**
 * @brief Zero-copy frame that directly references hardware buffer
 */
typedef struct {
    video_frame_t base;                  // Base frame structure
    uint8_t *hw_buffer;                  // Hardware buffer pointer
    uint32_t hw_buffer_size;             // Hardware buffer size
    aicam_bool_t buffer_returned;        // Whether buffer has been returned to hardware
    void (*return_callback)(uint8_t *buffer); // Callback to return buffer
} video_frame_zero_copy_t;

/* ==================== Zero-Copy Frame Management ==================== */

video_frame_t* video_frame_create_zero_copy(const video_frame_info_t *info,
                                           uint8_t *hw_buffer,
                                           uint32_t hw_buffer_size,
                                           void (*return_callback)(uint8_t *buffer))
{
    if (!info || !hw_buffer) {
        LOG_CORE_ERROR("Invalid parameters for video_frame_create_zero_copy");
        return NULL;
    }
    
    // Allocate zero-copy frame structure
    video_frame_zero_copy_t *frame = buffer_calloc(1, sizeof(video_frame_zero_copy_t));
    if (!frame) {
        LOG_CORE_ERROR("Failed to allocate zero-copy frame structure");
        return NULL;
    }
    
    // Initialize base frame
    memcpy(&frame->base.info, info, sizeof(video_frame_info_t));
    frame->base.data = hw_buffer;  // Directly use hardware buffer - NO COPYING!
    frame->base.ref_count = 1;
    frame->base.private_data = NULL;
    frame->base.is_key_frame = AICAM_TRUE;
    frame->base.quality = 100;
    
    // Initialize zero-copy specific fields
    frame->hw_buffer = hw_buffer;
    frame->hw_buffer_size = hw_buffer_size;
    frame->buffer_returned = AICAM_FALSE;
    frame->return_callback = return_callback;
    
    //LOG_CORE_INFO("Created zero-copy frame: %dx%d, hw_buffer=0x%p, size=%d",
    //             info->width, info->height, hw_buffer, hw_buffer_size);
    
    return (video_frame_t*)frame;
}

uint32_t video_frame_ref(video_frame_t *frame)
{
    if (!frame) {
        return 0;
    }
    
    frame->ref_count++;
    return frame->ref_count;
}

uint32_t video_frame_unref(video_frame_t *frame)
{
    if (!frame) {
        return 0;
    }
    
    if (frame->ref_count == 0) {
        LOG_CORE_ERROR("Attempting to unref frame with zero ref count");
        return 0;
    }
    
    frame->ref_count--;
    uint32_t remaining = frame->ref_count;
    
    if (remaining == 0) {
        // Check if this is a zero-copy frame
        video_frame_zero_copy_t *zero_copy_frame = (video_frame_zero_copy_t*)frame;
        
        // Check if the structure size matches (simple type checking)
        if (zero_copy_frame->hw_buffer == frame->data) {
            // This is a zero-copy frame, return buffer to hardware
            if (!zero_copy_frame->buffer_returned && zero_copy_frame->return_callback) {
                zero_copy_frame->return_callback(zero_copy_frame->hw_buffer);
                zero_copy_frame->buffer_returned = AICAM_TRUE;
            }
        }
        
        buffer_free(frame);
    }
    
    return remaining;
}

uint32_t video_frame_get_ref_count(const video_frame_t *frame)
{
    return frame ? frame->ref_count : 0;
}
