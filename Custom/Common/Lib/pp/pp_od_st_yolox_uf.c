/**
******************************************************************************
* @file    app_postprocess_od_st_yolox_uf.c
* @author  GPM Application Team
*
******************************************************************************
* @attention
*
* Copyright (c) 2023 STMicroelectronics.
* All rights reserved.
*
* This software is licensed under terms that can be found in the LICENSE file
* in the root directory of this software component.
* If no LICENSE file comes with this software, it is provided AS-IS.
*
******************************************************************************
*/

#include "pp.h"
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include "cJSON.h"
#include "mem.h"

// #include "od_yolov2_pp_if.h"
// #include "od_yolov5_pp_if.h"
// #include "od_fd_blazeface_pp_if.h"
// #include "od_yolov8_pp_if.h"
#include "od_st_yolox_pp_if.h"
// #include "od_centernet_pp_if.h"
// #include "od_ssd_pp_if.h"
// #include "od_ssd_st_pp_if.h"
// #include "od_pp_output_if.h"
// #include "mpe_yolov8_pp_if.h"
// #include "mpe_pp_output_if.h"
// #include "pd_model_pp_if.h"
// #include "pd_pp_output_if.h"
// #include "spe_movenet_pp_if.h"
// #include "spe_pp_output_if.h"
// #include "iseg_yolov8_pp_if.h"
// #include "iseg_pp_output_if.h"
// #include "sseg_deeplabv3_pp_if.h"
// #include "sseg_pp_output_if.h"

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

static od_pp_outBuffer_t *od_pp_buffer = NULL;
static od_detect_t *od_detect_buffer = NULL;
static od_st_yolox_pp_static_param_t static_params = {0};
static float32_t *anchors_L = NULL;
static float32_t *anchors_M = NULL;
static float32_t *anchors_S = NULL;
static char **static_class_names = NULL;
/*
"postprocess_params": {
  "num_classes": 1,
  "class_names": ["person"],
  "confidence_threshold": 0.6,
  "iou_threshold": 0.5,
  "max_detections": 100,
  "scales": {
    "large": {
      "grid_width": 60,
      "grid_height": 60,
      "anchors": [30.0, 30.0, 4.2, 15.0, 13.8, 42.0]
    },
    "medium": {
      "grid_width": 30,
      "grid_height": 30,
      "anchors": [15.0, 15.0, 2.1, 7.5, 6.9, 21.0]
    },
    "small": {
      "grid_width": 15,
      "grid_height": 15,
      "anchors": [7.5, 7.5, 1.05, 3.75, 3.45, 10.5]
    }
  }
},
*/
static int32_t init(const char *json_str, void **pp_params, void *nn_inst)
{
    od_st_yolox_pp_static_param_t *params = (od_st_yolox_pp_static_param_t *)&static_params;

    params->nb_classes = 1;
    params->max_boxes_limit = 100;
    params->conf_threshold = 0.6f;
    params->iou_threshold = 0.5f;
    params->nb_detect = 0;

    // If JSON is provided, parse and override parameters
    if (json_str != NULL) {
        cJSON *root = cJSON_Parse(json_str);
        if (root != NULL) {
            cJSON *pp = cJSON_GetObjectItemCaseSensitive(root, "postprocess_params");
            if (pp == NULL) {
                // Compatibility: the input is already a postprocess_params object
                pp = root;
            }

            if (cJSON_IsObject(pp)) {
                cJSON *num_classes = cJSON_GetObjectItemCaseSensitive(pp, "num_classes");
                if (cJSON_IsNumber(num_classes)) {
                    params->nb_classes = (int32_t)num_classes->valuedouble;
                }

                cJSON *class_names = cJSON_GetObjectItemCaseSensitive(pp, "class_names");
                if (cJSON_IsArray(class_names)) {
                    static_class_names = (char **)hal_mem_alloc_any(sizeof(char *) * params->nb_classes);
                    for (int i = 0; i < params->nb_classes; i++) {
                        cJSON *name = cJSON_GetArrayItem(class_names, i);
                        if (cJSON_IsString(name)) {
                            uint8_t len = strlen(name->valuestring) + 1;
                            static_class_names[i] = (char *)hal_mem_alloc_any(sizeof(char) * len);
                            memcpy(static_class_names[i], name->valuestring, len);
                        }
                    }
                }

                cJSON *conf_th = cJSON_GetObjectItemCaseSensitive(pp, "confidence_threshold");
                if (cJSON_IsNumber(conf_th)) {
                    params->conf_threshold = (float32_t)conf_th->valuedouble;
                }

                cJSON *iou_th = cJSON_GetObjectItemCaseSensitive(pp, "iou_threshold");
                if (cJSON_IsNumber(iou_th)) {
                    params->iou_threshold = (float32_t)iou_th->valuedouble;
                }

                cJSON *max_det = cJSON_GetObjectItemCaseSensitive(pp, "max_detections");
                if (cJSON_IsNumber(max_det)) {
                    params->max_boxes_limit = (int32_t)max_det->valuedouble;
                }

                cJSON *scales = cJSON_GetObjectItemCaseSensitive(pp, "scales");
                if (cJSON_IsObject(scales)) {
                    // Parse each scale
                    struct scale_def {
                        const char *name;
                        int32_t *gw;
                        int32_t *gh;
                        float32_t **anch_ptr;
                        const float32_t **param_ptr;
                    } defs[] = {
                        { "large",  &params->grid_width_L, &params->grid_height_L, &anchors_L, &params->pAnchors_L },
                        { "medium", &params->grid_width_M, &params->grid_height_M, &anchors_M, &params->pAnchors_M },
                        { "small",  &params->grid_width_S, &params->grid_height_S, &anchors_S, &params->pAnchors_S },
                    };

                    for (size_t i = 0; i < (sizeof(defs) / sizeof(defs[0])); ++i) {
                        cJSON *sc = cJSON_GetObjectItemCaseSensitive(scales, defs[i].name);
                        if (!cJSON_IsObject(sc)) {
                            continue;
                        }

                        cJSON *gw = cJSON_GetObjectItemCaseSensitive(sc, "grid_width");
                        cJSON *gh = cJSON_GetObjectItemCaseSensitive(sc, "grid_height");
                        if (cJSON_IsNumber(gw)) {
                            *(defs[i].gw) = (int32_t)gw->valuedouble;
                        }
                        if (cJSON_IsNumber(gh)) {
                            *(defs[i].gh) = (int32_t)gh->valuedouble;
                        }

                        cJSON *anchors = cJSON_GetObjectItemCaseSensitive(sc, "anchors");
                        if (cJSON_IsArray(anchors)) {
                            int count = cJSON_GetArraySize(anchors);
                            if (count > 0) {
                                // Free old memory
                                if (*(defs[i].anch_ptr) != NULL) {
                                    hal_mem_free(*(defs[i].anch_ptr));
                                    *(defs[i].anch_ptr) = NULL;
                                }
                                *(defs[i].anch_ptr) = (float32_t *)hal_mem_alloc_any(sizeof(float32_t) * (size_t)count);
                                if (*(defs[i].anch_ptr) != NULL) {
                                    for (int k = 0; k < count; ++k) {
                                        cJSON *v = cJSON_GetArrayItem(anchors, k);
                                        (*(defs[i].anch_ptr))[k] = cJSON_IsNumber(v) ? (float32_t)v->valuedouble : 0.0f;
                                    }
                                    *(defs[i].param_ptr) = *(defs[i].anch_ptr);
                                    // Each pair (w,h) is one anchor
                                    params->nb_anchors = (count >= 2) ? (count / 2) : params->nb_anchors;
                                }
                            }
                        }
                    }
                }

                // Calculate nb_input_boxes (if available)
                if (params->nb_anchors > 0) {
                    int32_t cells = params->grid_width_L * params->grid_height_L +
                                    params->grid_width_M * params->grid_height_M +
                                    params->grid_width_S * params->grid_height_S;
                    params->nb_input_boxes = cells * params->nb_anchors;
                }
            }

            cJSON_Delete(root);
        }
    }
    size_t boxes_limit = MAX(params->max_boxes_limit,
                         params->grid_width_L * params->grid_height_L +
                         params->grid_width_M * params->grid_height_M +
                         params->grid_width_S * params->grid_height_S);
    // Allocate output buffer based on latest parameters
    od_pp_buffer = (od_pp_outBuffer_t *)hal_mem_alloc_large(sizeof(od_pp_outBuffer_t) * boxes_limit);
    od_detect_buffer = (od_detect_t *)hal_mem_alloc_large(sizeof(od_detect_t) * boxes_limit);
    assert(od_pp_buffer != NULL && od_detect_buffer != NULL);
    od_st_yolox_pp_reset(params);
    *pp_params = (void *)params;
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static int32_t deinit(void *pp_params)
{
    od_st_yolox_pp_static_param_t *params = (od_st_yolox_pp_static_param_t *)pp_params;
    if (od_pp_buffer != NULL) {
        hal_mem_free(od_pp_buffer);
        od_pp_buffer = NULL;
    }
    if (od_detect_buffer != NULL) {
        hal_mem_free(od_detect_buffer);
        od_detect_buffer = NULL;
    }
    if (static_class_names != NULL) {
        for (int i = 0; i < params->nb_classes; i++) {
            hal_mem_free(static_class_names[i]);
        }
        hal_mem_free(static_class_names);
        static_class_names = NULL;
    }
    if (anchors_L != NULL) {
        hal_mem_free(anchors_L);
        anchors_L = NULL;
    }
    if (anchors_M != NULL) {
        hal_mem_free(anchors_M);
        anchors_M = NULL;
    }
    if (anchors_S != NULL) {
        hal_mem_free(anchors_S);
        anchors_S = NULL;
    }
    return AI_OD_POSTPROCESS_ERROR_NO;
}


static void od_pp_out_t_to_pp_result_t(od_pp_out_t *pObjDetOutput, pp_result_t *result)
{
    result->type = PP_TYPE_OD;
    result->is_valid = pObjDetOutput->nb_detect > 0;
    result->od.nb_detect = pObjDetOutput->nb_detect;
    result->od.detects = od_detect_buffer;
    for (int i = 0; i < pObjDetOutput->nb_detect; i++) {
        result->od.detects[i].x = MAX(0, pObjDetOutput->pOutBuff[i].x_center - pObjDetOutput->pOutBuff[i].width / 2.0f);
        result->od.detects[i].y = MAX(0, pObjDetOutput->pOutBuff[i].y_center - pObjDetOutput->pOutBuff[i].height / 2.0f);
        result->od.detects[i].width = MIN(1.0f, pObjDetOutput->pOutBuff[i].width);
        result->od.detects[i].height = MIN(1.0f, pObjDetOutput->pOutBuff[i].height);
        result->od.detects[i].conf = pObjDetOutput->pOutBuff[i].conf;
        result->od.detects[i].class_name = static_class_names[pObjDetOutput->pOutBuff[i].class_index];
    }
}

static int32_t run(void *pInput[], uint32_t nb_input, void *pResult, void *pp_params, void *nn_inst)
{
    assert(nb_input == 3);
    int32_t error = AI_OD_POSTPROCESS_ERROR_NO;
    ((od_st_yolox_pp_static_param_t *)pp_params)->nb_detect = 0;

    memset(pResult, 0, sizeof(pp_result_t));

    od_pp_out_t od_pp_out;
    od_pp_out.pOutBuff = od_pp_buffer;
    od_st_yolox_pp_in_t pp_input = {
        .pRaw_detections_S = (float32_t *)pInput[0],
        .pRaw_detections_L = (float32_t *)pInput[1],
        .pRaw_detections_M = (float32_t *)pInput[2],
    };
    error = od_st_yolox_pp_process(&pp_input, &od_pp_out, (od_st_yolox_pp_static_param_t *)pp_params);
    if (error == AI_OD_POSTPROCESS_ERROR_NO) {
        od_pp_out_t_to_pp_result_t(&od_pp_out, (pp_result_t *)pResult);
    }
    return error;
}

static int32_t set_confidence_threshold(void *params, float threshold)
{
    ((od_st_yolox_pp_static_param_t *)params)->conf_threshold = threshold;
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static int32_t set_nms_threshold(void *params, float threshold)
{
    ((od_st_yolox_pp_static_param_t *)params)->iou_threshold = threshold;
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static int32_t get_confidence_threshold(void *params, float *threshold)
{
    *threshold = ((od_st_yolox_pp_static_param_t *)params)->conf_threshold;
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static int32_t get_nms_threshold(void *params, float *threshold)
{
    *threshold = ((od_st_yolox_pp_static_param_t *)params)->iou_threshold;
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static const pp_vtable_t vt = {
    .init = init,
    .run = run,
    .deinit = deinit,
    .set_confidence_threshold = set_confidence_threshold,
    .get_confidence_threshold = get_confidence_threshold,
    .set_nms_threshold = set_nms_threshold,
    .get_nms_threshold = get_nms_threshold,
};

// Define static registration entry
const pp_entry_t pp_entry_od_st_yolox_uf = {
    .name = "pp_od_st_yolox_uf",
    .vt = &vt
};