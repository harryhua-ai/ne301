#ifndef __QUICK_STORAGE_H__
#define __QUICK_STORAGE_H__

#include "isp_services.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Same numeric values as `IMAGE_ISP_MODE_*` in `json_config_mgr.h` / `image_config_t.isp_mode`. */
#define QS_IMAGE_ISP_MODE_OUTDOOR  0u
#define QS_IMAGE_ISP_MODE_INDOOR   1u
#define QS_IMAGE_ISP_MODE_CUSTOM   255u

/**
 * @brief Snapshot configuration
 */
typedef struct {
    uint8_t ai_enabled;
    uint8_t ai_1_active;
    uint32_t ai_pipe_width;   /**< 0 or invalid: no reliable value from NVS; AI pipe sized after model info */
    uint32_t ai_pipe_height;  /**< Same as ai_pipe_width */
    uint32_t confidence_threshold;
    uint32_t nms_threshold;

    uint8_t light_mode;             //0: off, 1: on, 2: auto, 3: custom
    uint32_t light_threshold;
    uint32_t light_brightness;
    uint32_t light_start_time;
    uint32_t light_end_time;

    uint8_t mirror_flip;            //0: none, 1: flip, 2: mirror, 3: flip + mirror
    uint32_t fast_capture_skip_frames;
    uint32_t fast_capture_resolution;
    uint32_t fast_capture_jpeg_quality;
    uint8_t capture_storage_ai;

    /** One of `QS_IMAGE_ISP_MODE_*` (aligned with app `image_config_t.isp_mode`). */
    uint32_t isp_mode;

    /** Same as `image_config_t.grayscale` — ISP luma matrix when non-zero. */
    uint8_t grayscale;

} qs_snapshot_config_t;

/**
 * @brief Read snapshot configuration (derived config cache, legacy NVS keys as fallback)
 * @param snapshot_config Snapshot configuration
 * @return 0 on success, other values on error
 */
int quick_storage_read_snapshot_config(qs_snapshot_config_t *snapshot_config);

/**
 * @brief Build ISP IQ parameters for fast capture without json_config.
 * @param isp_mode `QS_IMAGE_ISP_MODE_*`
 * @param grayscale Same as `image_config_t.grayscale` (0/1, ISP grayscale overlay).
 * @param isp_param Output for `CAM_CMD_SET_ISP_PARAM`
 * @return AICAM_OK or AICAM_ERROR_INVALID_PARAM
 */
int quick_storage_fill_isp_iq_param(uint32_t isp_mode, uint8_t grayscale,
                                    ISP_IQParamTypeDef *isp_param);

#ifdef __cplusplus
}
#endif

#endif /* __QUICK_STORAGE_H__ */
