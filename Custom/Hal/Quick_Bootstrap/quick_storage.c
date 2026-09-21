#include "quick_storage.h"
#include "quick_trace.h"
#include "json_config_internal.h"
#include "json_config_mgr.h"
#include "cfg_config_cache.h"
#include "cfg_config_cache_nvs.h"
#include "camera.h"
#include "storage.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern const aicam_global_config_t default_config;

static aicam_result_t qs_nvs_read_string(const char *key, char *value, size_t max_len)
{
    int result = storage_nvs_read(NVS_USER, key, value, max_len);
    return (result >= 0) ? AICAM_OK : AICAM_ERROR;
}

static aicam_result_t qs_nvs_read_uint32(const char *key, uint32_t *value)
{
    char value_str[12];
    int result = storage_nvs_read(NVS_USER, key, value_str, sizeof(value_str));
    if (result >= 0) {
        *value = (uint32_t)strtoul(value_str, NULL, 10);
        return AICAM_OK;
    }
    return AICAM_ERROR;
}

static aicam_result_t qs_nvs_read_uint8(const char *key, uint8_t *value)
{
    char value_str[4];
    int result = storage_nvs_read(NVS_USER, key, value_str, sizeof(value_str));
    if (result >= 0) {
        *value = (uint8_t)strtoul(value_str, NULL, 10);
        return AICAM_OK;
    }
    return AICAM_ERROR;
}

static aicam_result_t qs_nvs_read_bool(const char *key, aicam_bool_t *value)
{
    char value_str[2];
    int result = storage_nvs_read(NVS_USER, key, value_str, sizeof(value_str));
    if (result >= 0) {
        *value = (strcmp(value_str, "1") == 0) ? AICAM_TRUE : AICAM_FALSE;
        return AICAM_OK;
    }
    return AICAM_ERROR;
}

static aicam_result_t qs_nvs_read_int32(const char *key, int32_t *value)
{
    char value_str[12];
    int result = storage_nvs_read(NVS_USER, key, value_str, sizeof(value_str));
    if (result >= 0) {
        *value = (int32_t)strtol(value_str, NULL, 10);
        return AICAM_OK;
    }
    return AICAM_ERROR;
}

/* Load `isp_config_t` from user NVS (same layout as json_config_load path). */
static void qs_load_isp_config_from_nvs(isp_config_t *isp)
{
    if (!isp) {
        return;
    }
    memset(isp, 0, sizeof(*isp));
    aicam_bool_t temp_bool = AICAM_FALSE;
    uint32_t temp_uint32 = 0;
    uint8_t temp_uint8 = 0;
    int32_t temp_int32 = 0;

    if (qs_nvs_read_bool(NVS_KEY_ISP_VALID, &temp_bool) != AICAM_OK) {
        return;
    }
    isp->valid = temp_bool;
    if (!isp->valid) {
        return;
    }

    if (qs_nvs_read_bool(NVS_KEY_ISP_SR_ENABLE, &temp_bool) == AICAM_OK) {
        isp->stat_removal_enable = temp_bool;
    }
    if (qs_nvs_read_uint32(NVS_KEY_ISP_SR_HEADLINES, &temp_uint32) == AICAM_OK) {
        isp->stat_removal_head_lines = temp_uint32;
    }
    if (qs_nvs_read_uint32(NVS_KEY_ISP_SR_VALIDLINES, &temp_uint32) == AICAM_OK) {
        isp->stat_removal_valid_lines = temp_uint32;
    }

    if (qs_nvs_read_bool(NVS_KEY_ISP_DEMO_ENABLE, &temp_bool) == AICAM_OK) {
        isp->demosaic_enable = temp_bool;
    }
    if (qs_nvs_read_uint8(NVS_KEY_ISP_DEMO_TYPE, &temp_uint8) == AICAM_OK) {
        isp->demosaic_type = temp_uint8;
    }
    if (qs_nvs_read_uint8(NVS_KEY_ISP_DEMO_PEAK, &temp_uint8) == AICAM_OK) {
        isp->demosaic_peak = temp_uint8;
    }
    if (qs_nvs_read_uint8(NVS_KEY_ISP_DEMO_LINEV, &temp_uint8) == AICAM_OK) {
        isp->demosaic_line_v = temp_uint8;
    }
    if (qs_nvs_read_uint8(NVS_KEY_ISP_DEMO_LINEH, &temp_uint8) == AICAM_OK) {
        isp->demosaic_line_h = temp_uint8;
    }
    if (qs_nvs_read_uint8(NVS_KEY_ISP_DEMO_EDGE, &temp_uint8) == AICAM_OK) {
        isp->demosaic_edge = temp_uint8;
    }

    if (qs_nvs_read_bool(NVS_KEY_ISP_CONTRAST_ENABLE, &temp_bool) == AICAM_OK) {
        isp->contrast_enable = temp_bool;
    }
    (void)storage_nvs_read(NVS_USER, NVS_KEY_ISP_CONTRAST_LUT, isp->contrast_lut, sizeof(isp->contrast_lut));

    if (qs_nvs_read_uint32(NVS_KEY_ISP_STAT_X, &temp_uint32) == AICAM_OK) {
        isp->stat_area_x = temp_uint32;
    }
    if (qs_nvs_read_uint32(NVS_KEY_ISP_STAT_Y, &temp_uint32) == AICAM_OK) {
        isp->stat_area_y = temp_uint32;
    }
    if (qs_nvs_read_uint32(NVS_KEY_ISP_STAT_W, &temp_uint32) == AICAM_OK) {
        isp->stat_area_width = temp_uint32;
    }
    if (qs_nvs_read_uint32(NVS_KEY_ISP_STAT_H, &temp_uint32) == AICAM_OK) {
        isp->stat_area_height = temp_uint32;
    }

    if (qs_nvs_read_uint32(NVS_KEY_ISP_SENSOR_GAIN, &temp_uint32) == AICAM_OK) {
        isp->sensor_gain = temp_uint32;
    }
    if (qs_nvs_read_uint32(NVS_KEY_ISP_SENSOR_EXPO, &temp_uint32) == AICAM_OK) {
        isp->sensor_exposure = temp_uint32;
    }

    if (qs_nvs_read_bool(NVS_KEY_ISP_BPA_ENABLE, &temp_bool) == AICAM_OK) {
        isp->bad_pixel_algo_enable = temp_bool;
    }
    if (qs_nvs_read_uint32(NVS_KEY_ISP_BPA_THRESH, &temp_uint32) == AICAM_OK) {
        isp->bad_pixel_algo_threshold = temp_uint32;
    }

    if (qs_nvs_read_bool(NVS_KEY_ISP_BP_ENABLE, &temp_bool) == AICAM_OK) {
        isp->bad_pixel_enable = temp_bool;
    }
    if (qs_nvs_read_uint8(NVS_KEY_ISP_BP_STRENGTH, &temp_uint8) == AICAM_OK) {
        isp->bad_pixel_strength = temp_uint8;
    }

    if (qs_nvs_read_bool(NVS_KEY_ISP_BL_ENABLE, &temp_bool) == AICAM_OK) {
        isp->black_level_enable = temp_bool;
    }
    if (qs_nvs_read_uint8(NVS_KEY_ISP_BL_R, &temp_uint8) == AICAM_OK) {
        isp->black_level_r = temp_uint8;
    }
    if (qs_nvs_read_uint8(NVS_KEY_ISP_BL_G, &temp_uint8) == AICAM_OK) {
        isp->black_level_g = temp_uint8;
    }
    if (qs_nvs_read_uint8(NVS_KEY_ISP_BL_B, &temp_uint8) == AICAM_OK) {
        isp->black_level_b = temp_uint8;
    }

    if (qs_nvs_read_bool(NVS_KEY_ISP_AEC_ENABLE, &temp_bool) == AICAM_OK) {
        isp->aec_enable = temp_bool;
    }
    if (qs_nvs_read_int32(NVS_KEY_ISP_AEC_EXPCOMP, &temp_int32) == AICAM_OK) {
        isp->aec_exposure_compensation = temp_int32;
    }
    if (qs_nvs_read_uint32(NVS_KEY_ISP_AEC_AFLK, &temp_uint32) == AICAM_OK) {
        isp->aec_anti_flicker_freq = temp_uint32;
    }

    if (qs_nvs_read_bool(NVS_KEY_ISP_AWB_ENABLE, &temp_bool) == AICAM_OK) {
        isp->awb_enable = temp_bool;
    }
    typedef struct {
        char label[ISP_AWB_PROFILES_MAX][ISP_AWB_LABEL_MAX_LEN];
        uint32_t ref_color_temp[ISP_AWB_PROFILES_MAX];
        uint32_t gain_r[ISP_AWB_PROFILES_MAX];
        uint32_t gain_g[ISP_AWB_PROFILES_MAX];
        uint32_t gain_b[ISP_AWB_PROFILES_MAX];
        int32_t ccm[ISP_AWB_PROFILES_MAX][3][3];
        uint8_t ref_rgb[ISP_AWB_PROFILES_MAX][3];
    } awb_data_t;
    awb_data_t awb_data;
    if (storage_nvs_read(NVS_USER, NVS_KEY_ISP_AWB_DATA, &awb_data, sizeof(awb_data)) >= 0) {
        memcpy(isp->awb_label, awb_data.label, sizeof(isp->awb_label));
        memcpy(isp->awb_ref_color_temp, awb_data.ref_color_temp, sizeof(isp->awb_ref_color_temp));
        memcpy(isp->awb_gain_r, awb_data.gain_r, sizeof(isp->awb_gain_r));
        memcpy(isp->awb_gain_g, awb_data.gain_g, sizeof(isp->awb_gain_g));
        memcpy(isp->awb_gain_b, awb_data.gain_b, sizeof(isp->awb_gain_b));
        memcpy(isp->awb_ccm, awb_data.ccm, sizeof(isp->awb_ccm));
        memcpy(isp->awb_ref_rgb, awb_data.ref_rgb, sizeof(isp->awb_ref_rgb));
    }

    if (qs_nvs_read_bool(NVS_KEY_ISP_GAIN_ENABLE, &temp_bool) == AICAM_OK) {
        isp->isp_gain_enable = temp_bool;
    }
    if (qs_nvs_read_uint32(NVS_KEY_ISP_GAIN_R, &temp_uint32) == AICAM_OK) {
        isp->isp_gain_r = temp_uint32;
    }
    if (qs_nvs_read_uint32(NVS_KEY_ISP_GAIN_G, &temp_uint32) == AICAM_OK) {
        isp->isp_gain_g = temp_uint32;
    }
    if (qs_nvs_read_uint32(NVS_KEY_ISP_GAIN_B, &temp_uint32) == AICAM_OK) {
        isp->isp_gain_b = temp_uint32;
    }

    if (qs_nvs_read_bool(NVS_KEY_ISP_CCM_ENABLE, &temp_bool) == AICAM_OK) {
        isp->color_conv_enable = temp_bool;
    }
    (void)storage_nvs_read(NVS_USER, NVS_KEY_ISP_CCM_DATA, isp->color_conv_matrix, sizeof(isp->color_conv_matrix));

    if (qs_nvs_read_bool(NVS_KEY_ISP_GAMMA_ENABLE, &temp_bool) == AICAM_OK) {
        isp->gamma_enable = temp_bool;
    }

    if (qs_nvs_read_uint8(NVS_KEY_ISP_SENSOR_DELAY, &temp_uint8) == AICAM_OK) {
        isp->sensor_delay = temp_uint8;
    }

    typedef struct {
        uint32_t hl_ref, hl_expo1, hl_expo2;
        uint8_t hl_lum1, hl_lum2;
        uint32_t ll_ref, ll_expo1, ll_expo2;
        uint8_t ll_lum1, ll_lum2;
        float calib_factor;
    } lux_data_t;
    lux_data_t lux_data;
    if (storage_nvs_read(NVS_USER, NVS_KEY_ISP_LUX_DATA, &lux_data, sizeof(lux_data)) >= 0) {
        isp->lux_hl_ref = lux_data.hl_ref;
        isp->lux_hl_expo1 = lux_data.hl_expo1;
        isp->lux_hl_expo2 = lux_data.hl_expo2;
        isp->lux_hl_lum1 = lux_data.hl_lum1;
        isp->lux_hl_lum2 = lux_data.hl_lum2;
        isp->lux_ll_ref = lux_data.ll_ref;
        isp->lux_ll_expo1 = lux_data.ll_expo1;
        isp->lux_ll_expo2 = lux_data.ll_expo2;
        isp->lux_ll_lum1 = lux_data.ll_lum1;
        isp->lux_ll_lum2 = lux_data.ll_lum2;
        isp->lux_calib_factor = lux_data.calib_factor;
    }
}

/* Same field mapping as json_config_config_to_isp_param (no json_config runtime). */
static void qs_isp_config_to_iq_param(const isp_config_t *isp_config, ISP_IQParamTypeDef *isp_param)
{
    if (isp_config == NULL || isp_param == NULL) {
        return;
    }

    memset(isp_param, 0, sizeof(ISP_IQParamTypeDef));

    isp_param->statRemoval.enable = isp_config->stat_removal_enable;
    isp_param->statRemoval.nbHeadLines = isp_config->stat_removal_head_lines;
    isp_param->statRemoval.nbValidLines = isp_config->stat_removal_valid_lines;

    isp_param->demosaicing.enable = isp_config->demosaic_enable;
    isp_param->demosaicing.type = (ISP_DemosTypeTypeDef)isp_config->demosaic_type;
    isp_param->demosaicing.peak = isp_config->demosaic_peak;
    isp_param->demosaicing.lineV = isp_config->demosaic_line_v;
    isp_param->demosaicing.lineH = isp_config->demosaic_line_h;
    isp_param->demosaicing.edge = isp_config->demosaic_edge;

    isp_param->contrast.enable = isp_config->contrast_enable;
    isp_param->contrast.coeff.LUM_0 = isp_config->contrast_lut[0];
    isp_param->contrast.coeff.LUM_32 = isp_config->contrast_lut[1];
    isp_param->contrast.coeff.LUM_64 = isp_config->contrast_lut[2];
    isp_param->contrast.coeff.LUM_96 = isp_config->contrast_lut[3];
    isp_param->contrast.coeff.LUM_128 = isp_config->contrast_lut[4];
    isp_param->contrast.coeff.LUM_160 = isp_config->contrast_lut[5];
    isp_param->contrast.coeff.LUM_192 = isp_config->contrast_lut[6];
    isp_param->contrast.coeff.LUM_224 = isp_config->contrast_lut[7];
    isp_param->contrast.coeff.LUM_256 = isp_config->contrast_lut[8];

    isp_param->statAreaStatic.X0 = isp_config->stat_area_x;
    isp_param->statAreaStatic.Y0 = isp_config->stat_area_y;
    isp_param->statAreaStatic.XSize = isp_config->stat_area_width;
    isp_param->statAreaStatic.YSize = isp_config->stat_area_height;

    isp_param->sensorGainStatic.gain = isp_config->sensor_gain;
    isp_param->sensorExposureStatic.exposure = isp_config->sensor_exposure;

    isp_param->badPixelAlgo.enable = isp_config->bad_pixel_algo_enable;
    isp_param->badPixelAlgo.threshold = isp_config->bad_pixel_algo_threshold;

    isp_param->badPixelStatic.enable = isp_config->bad_pixel_enable;
    isp_param->badPixelStatic.strength = isp_config->bad_pixel_strength;

    isp_param->blackLevelStatic.enable = isp_config->black_level_enable;
    isp_param->blackLevelStatic.BLCR = isp_config->black_level_r;
    isp_param->blackLevelStatic.BLCG = isp_config->black_level_g;
    isp_param->blackLevelStatic.BLCB = isp_config->black_level_b;

    isp_param->AECAlgo.enable = isp_config->aec_enable;
    isp_param->AECAlgo.exposureCompensation = isp_config->aec_exposure_compensation;
    isp_param->AECAlgo.antiFlickerFreq = isp_config->aec_anti_flicker_freq;

    isp_param->AWBAlgo.enable = isp_config->awb_enable;
    for (int i = 0; i < ISP_AWB_PROFILES_MAX; i++) {
        memcpy(isp_param->AWBAlgo.label[i], isp_config->awb_label[i], ISP_AWB_LABEL_MAX_LEN);
        isp_param->AWBAlgo.referenceColorTemp[i] = isp_config->awb_ref_color_temp[i];
        isp_param->AWBAlgo.ispGainR[i] = isp_config->awb_gain_r[i];
        isp_param->AWBAlgo.ispGainG[i] = isp_config->awb_gain_g[i];
        isp_param->AWBAlgo.ispGainB[i] = isp_config->awb_gain_b[i];
        memcpy(isp_param->AWBAlgo.coeff[i], isp_config->awb_ccm[i], sizeof(isp_param->AWBAlgo.coeff[i]));
        memcpy(isp_param->AWBAlgo.referenceRGB[i], isp_config->awb_ref_rgb[i], sizeof(isp_param->AWBAlgo.referenceRGB[i]));
    }

    isp_param->ispGainStatic.enable = isp_config->isp_gain_enable;
    isp_param->ispGainStatic.ispGainR = isp_config->isp_gain_r;
    isp_param->ispGainStatic.ispGainG = isp_config->isp_gain_g;
    isp_param->ispGainStatic.ispGainB = isp_config->isp_gain_b;

    isp_param->colorConvStatic.enable = isp_config->color_conv_enable;
    memcpy(isp_param->colorConvStatic.coeff, isp_config->color_conv_matrix, sizeof(isp_param->colorConvStatic.coeff));

    isp_param->gamma.enable = isp_config->gamma_enable;

    isp_param->sensorDelay.delay = isp_config->sensor_delay;

    isp_param->luxRef.HL_LuxRef = isp_config->lux_hl_ref;
    isp_param->luxRef.HL_Expo1 = isp_config->lux_hl_expo1;
    isp_param->luxRef.HL_Expo2 = isp_config->lux_hl_expo2;
    isp_param->luxRef.HL_Lum1 = isp_config->lux_hl_lum1;
    isp_param->luxRef.HL_Lum2 = isp_config->lux_hl_lum2;
    isp_param->luxRef.LL_LuxRef = isp_config->lux_ll_ref;
    isp_param->luxRef.LL_Expo1 = isp_config->lux_ll_expo1;
    isp_param->luxRef.LL_Expo2 = isp_config->lux_ll_expo2;
    isp_param->luxRef.LL_Lum1 = isp_config->lux_ll_lum1;
    isp_param->luxRef.LL_Lum2 = isp_config->lux_ll_lum2;
    isp_param->luxRef.calibFactor = isp_config->lux_calib_factor;
}

int quick_storage_fill_isp_iq_param(uint32_t isp_mode, uint8_t grayscale,
                                    ISP_IQParamTypeDef *isp_param)
{
    aicam_bool_t gray_on = (grayscale != 0u) ? AICAM_TRUE : AICAM_FALSE;

    if (!isp_param) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (isp_mode == QS_IMAGE_ISP_MODE_CUSTOM) {
        isp_config_t cfg = {0};
        cfg_derived_view_t view;
        cfg_config_cache_core_t *core = NULL;
        if (cfg_config_cache_nvs_begin(&core)) {
            cfg_cache_boot_source_t src = cfg_config_cache_boot_source(core, &view);
            cfg_config_cache_nvs_end();
            if (src == CFG_CACHE_BOOT_COMMITTED) {
                cfg = view.device_service.isp_config;
            } else if (src == CFG_CACHE_BOOT_PRE_MIGRATION) {
                qs_load_isp_config_from_nvs(&cfg);
            }
        } else {
            qs_load_isp_config_from_nvs(&cfg);
        }
        if (cfg.valid) {
            qs_isp_config_to_iq_param(&cfg, isp_param);
            camera_apply_grayscale_iq(isp_param, gray_on);
            return AICAM_OK;
        }
    }

    cam_iq_scene_t scene = CAM_IQ_SCENE_INDOOR;
    if (isp_mode == QS_IMAGE_ISP_MODE_OUTDOOR) {
        scene = CAM_IQ_SCENE_OUTDOOR;
    } else if (isp_mode == QS_IMAGE_ISP_MODE_CUSTOM) {
        /* Align with device_service_build_isp_iq_param: custom without valid NVS profile. */
        scene = CAM_IQ_SCENE_INDOOR;
    }
    camera_fill_isp_iq_scene(scene, isp_param);
    camera_apply_grayscale_iq(isp_param, gray_on);
    return AICAM_OK;
}

int quick_storage_read_snapshot_config(qs_snapshot_config_t *snapshot_config)
{
    if (!snapshot_config) return AICAM_ERROR_INVALID_PARAM;

    /* Defaults aligned with `default_config` in json_config_mgr.c */
    memset(snapshot_config, 0, sizeof(*snapshot_config));
    snapshot_config->ai_enabled = (uint8_t)default_config.ai_debug.ai_enabled;
    snapshot_config->ai_1_active = (uint8_t)default_config.ai_debug.ai_1_active;
    snapshot_config->ai_pipe_width = 0;
    snapshot_config->ai_pipe_height = 0;
    snapshot_config->confidence_threshold = default_config.ai_debug.confidence_threshold;
    snapshot_config->nms_threshold = default_config.ai_debug.nms_threshold;

    snapshot_config->light_mode = (uint8_t)default_config.device_service.light_config.mode;
    snapshot_config->light_threshold = default_config.device_service.light_config.light_threshold;
    snapshot_config->light_brightness = default_config.device_service.light_config.brightness_level;
    snapshot_config->light_start_time =
        (default_config.device_service.light_config.start_hour * 3600U) +
        (default_config.device_service.light_config.start_minute * 60U);
    snapshot_config->light_end_time =
        (default_config.device_service.light_config.end_hour * 3600U) +
        (default_config.device_service.light_config.end_minute * 60U);

    if (default_config.device_service.image_config.horizontal_flip &&
        default_config.device_service.image_config.vertical_flip) snapshot_config->mirror_flip = 3;
    else if (default_config.device_service.image_config.horizontal_flip) snapshot_config->mirror_flip = 2;
    else if (default_config.device_service.image_config.vertical_flip) snapshot_config->mirror_flip = 1;
    else snapshot_config->mirror_flip = 0;

    snapshot_config->fast_capture_skip_frames = default_config.device_service.image_config.fast_capture_skip_frames;
    snapshot_config->fast_capture_resolution = default_config.device_service.image_config.fast_capture_resolution;
    snapshot_config->fast_capture_jpeg_quality = default_config.device_service.image_config.fast_capture_jpeg_quality;
    snapshot_config->capture_storage_ai = default_config.device_service.image_config.capture_storage_ai;
    snapshot_config->isp_mode = default_config.device_service.image_config.isp_mode;
    snapshot_config->grayscale = (uint8_t)default_config.device_service.image_config.grayscale;

    cfg_derived_view_t view;
    cfg_cache_boot_source_t boot_src = CFG_CACHE_BOOT_SAFE_DEFAULTS;
    {
        cfg_config_cache_core_t *core = NULL;
        if (cfg_config_cache_nvs_begin(&core)) {
            boot_src = cfg_config_cache_boot_source(core, &view);
            cfg_config_cache_nvs_end();
        }
    }
    aicam_bool_t have_cache = (boot_src == CFG_CACHE_BOOT_COMMITTED);
    aicam_bool_t legacy_allowed = (boot_src == CFG_CACHE_BOOT_PRE_MIGRATION);

    aicam_result_t result;
    aicam_bool_t temp_bool = AICAM_FALSE;
    uint32_t temp_u32 = 0;
    uint8_t temp_u8 = 0;

    /* Match application layer: AI enabled by default */
    snapshot_config->ai_enabled = AICAM_TRUE;

    if (snapshot_config->ai_enabled) {
        if (have_cache) {
            snapshot_config->ai_1_active = (uint8_t)view.ai_debug.ai_1_active;
            snapshot_config->confidence_threshold = view.ai_debug.confidence_threshold;
            snapshot_config->nms_threshold = view.ai_debug.nms_threshold;
        } else if (legacy_allowed) {
            result = qs_nvs_read_bool(NVS_KEY_AI_1_ACTIVE, &temp_bool);
            if (result == AICAM_OK) snapshot_config->ai_1_active = (uint8_t)temp_bool;

            result = qs_nvs_read_uint32(NVS_KEY_CONFIDENCE, &temp_u32);
            if (result == AICAM_OK) snapshot_config->confidence_threshold = temp_u32;

            result = qs_nvs_read_uint32(NVS_KEY_NMS_THRESHOLD, &temp_u32);
            if (result == AICAM_OK) snapshot_config->nms_threshold = temp_u32;
        }

        /* AI pipe dimensions: keep 0 if read fails or invalid; set after model info is known */
        result = qs_nvs_read_uint32(NVS_KEY_AI_PIPE_WIDTH, &temp_u32);
        if (result == AICAM_OK) snapshot_config->ai_pipe_width = temp_u32;

        result = qs_nvs_read_uint32(NVS_KEY_AI_PIPE_HEIGHT, &temp_u32);
        if (result == AICAM_OK) snapshot_config->ai_pipe_height = temp_u32;
    }

    if (have_cache) {
        snapshot_config->light_mode = (uint8_t)view.device_service.light_config.mode;
        snapshot_config->light_threshold = view.device_service.light_config.light_threshold;
        snapshot_config->light_brightness = view.device_service.light_config.brightness_level;
        snapshot_config->light_start_time =
            (view.device_service.light_config.start_hour * 3600U) +
            (view.device_service.light_config.start_minute * 60U);
        snapshot_config->light_end_time =
            (view.device_service.light_config.end_hour * 3600U) +
            (view.device_service.light_config.end_minute * 60U);

        aicam_bool_t hflip = view.device_service.image_config.horizontal_flip;
        aicam_bool_t vflip = view.device_service.image_config.vertical_flip;
        if (hflip && vflip) snapshot_config->mirror_flip = 3;
        else if (hflip) snapshot_config->mirror_flip = 2;
        else if (vflip) snapshot_config->mirror_flip = 1;
        else snapshot_config->mirror_flip = 0;

        snapshot_config->fast_capture_skip_frames = view.device_service.image_config.fast_capture_skip_frames;
        snapshot_config->fast_capture_resolution = view.device_service.image_config.fast_capture_resolution;
        snapshot_config->fast_capture_jpeg_quality = view.device_service.image_config.fast_capture_jpeg_quality;
        snapshot_config->capture_storage_ai = (uint8_t)view.device_service.image_config.capture_storage_ai;
        snapshot_config->isp_mode = view.device_service.image_config.isp_mode;
        snapshot_config->grayscale = (uint8_t)view.device_service.image_config.grayscale;
    } else if (legacy_allowed) {
        result = qs_nvs_read_uint8(NVS_KEY_LIGHT_MODE, &temp_u8);
        if (result == AICAM_OK) snapshot_config->light_mode = temp_u8;

        if (snapshot_config->light_mode == LIGHT_MODE_AUTO) {
            result = qs_nvs_read_uint32(NVS_KEY_LIGHT_THRESHOLD, &temp_u32);
            if (result == AICAM_OK) snapshot_config->light_threshold = temp_u32;
        }

        if (snapshot_config->light_mode != LIGHT_MODE_OFF) {
            result = qs_nvs_read_uint32(NVS_KEY_LIGHT_BRIGHTNESS, &temp_u32);
            if (result == AICAM_OK) snapshot_config->light_brightness = temp_u32;
        }

        if (snapshot_config->light_mode == LIGHT_MODE_CUSTOM) {
            /* light custom schedule: store as seconds from 00:00 */
            uint32_t sh = 0, sm = 0, eh = 0, em = 0;
            if (qs_nvs_read_uint32(NVS_KEY_LIGHT_START_HOUR, &sh) != AICAM_OK) sh = default_config.device_service.light_config.start_hour;
            if (qs_nvs_read_uint32(NVS_KEY_LIGHT_START_MIN, &sm) != AICAM_OK) sm = default_config.device_service.light_config.start_minute;
            if (qs_nvs_read_uint32(NVS_KEY_LIGHT_END_HOUR, &eh) != AICAM_OK) eh = default_config.device_service.light_config.end_hour;
            if (qs_nvs_read_uint32(NVS_KEY_LIGHT_END_MIN, &em) != AICAM_OK) em = default_config.device_service.light_config.end_minute;
            snapshot_config->light_start_time = (sh * 3600U) + (sm * 60U);
            snapshot_config->light_end_time = (eh * 3600U) + (em * 60U);
        }

        /* Mirror/flip: from boolean HFLIP/VFLIP */
        aicam_bool_t hflip = default_config.device_service.image_config.horizontal_flip;
        aicam_bool_t vflip = default_config.device_service.image_config.vertical_flip;
        (void)qs_nvs_read_bool(NVS_KEY_IMAGE_HFLIP, &hflip);
        (void)qs_nvs_read_bool(NVS_KEY_IMAGE_VFLIP, &vflip);
        if (hflip && vflip) snapshot_config->mirror_flip = 3;
        else if (hflip) snapshot_config->mirror_flip = 2;
        else if (vflip) snapshot_config->mirror_flip = 1;
        else snapshot_config->mirror_flip = 0;

        result = qs_nvs_read_uint32(NVS_KEY_IMAGE_FAST_SKIP_FRAMES, &temp_u32);
        if (result == AICAM_OK) snapshot_config->fast_capture_skip_frames = temp_u32;

        result = qs_nvs_read_uint32(NVS_KEY_IMAGE_FAST_RESOLUTION, &temp_u32);
        if (result == AICAM_OK) snapshot_config->fast_capture_resolution = temp_u32;

        result = qs_nvs_read_uint32(NVS_KEY_IMAGE_FAST_JPEG_QUALITY, &temp_u32);
        if (result == AICAM_OK) snapshot_config->fast_capture_jpeg_quality = temp_u32;

        result = qs_nvs_read_bool(NVS_KEY_CAPTURE_STORAGE_AI, &temp_bool);
        if (result == AICAM_OK) snapshot_config->capture_storage_ai = (uint8_t)temp_bool;

        result = qs_nvs_read_uint32(NVS_KEY_IMAGE_ISP_MODE, &temp_u32);
        if (result == AICAM_OK) snapshot_config->isp_mode = temp_u32;

        result = qs_nvs_read_bool(NVS_KEY_IMAGE_GRAYSCALE, &temp_bool);
        if (result == AICAM_OK) {
            snapshot_config->grayscale = (uint8_t)temp_bool;
        }
    }

    if (snapshot_config->isp_mode != QS_IMAGE_ISP_MODE_INDOOR &&
        snapshot_config->isp_mode != QS_IMAGE_ISP_MODE_OUTDOOR &&
        snapshot_config->isp_mode != QS_IMAGE_ISP_MODE_CUSTOM) {
        snapshot_config->isp_mode = default_config.device_service.image_config.isp_mode;
    }

    return AICAM_OK;
}
