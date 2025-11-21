/**
******************************************************************************
* @file    pp_od_yolo_v8_ui.c
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
#include <math.h>
#include "cJSON.h"
#include "mem.h"
#include "ll_aton_runtime.h"
#include "ll_aton_reloc_network.h"
#include "od_yolov8_pp_if.h"

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

static od_pp_outBuffer_t *od_pp_buffer = NULL;
static od_detect_t *od_detect_buffer = NULL;
static od_yolov8_pp_static_param_t static_params = {0};
static char **static_class_names = NULL;
static int8_t *scratch_buffer = NULL;

/*
Example JSON configuration for int8 quantized model:
"postprocess_params": {
  "num_classes": 10,
  "class_names": ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9"],
  "confidence_threshold": 0.5,
  "iou_threshold": 0.45,
  "max_detections": 100,
  "total_boxes": 1344,
  "raw_output_scale": 0.003921569,
  "raw_output_zero_point": -128
}
*/
static int32_t init(const char *json_str, void **pp_params, void *nn_inst)
{
    od_yolov8_pp_static_param_t *params = (od_yolov8_pp_static_param_t *)&static_params;
    
    NN_Instance_TypeDef *NN_Instance = (NN_Instance_TypeDef *)nn_inst;
    const LL_Buffer_InfoTypeDef *buffers_info = ll_aton_reloc_get_output_buffers_info(NN_Instance, 0);
    params->raw_output_scale = *(buffers_info->scale);
    params->raw_output_zero_point = *(buffers_info->offset);
    params->nb_classes = 80;
    params->nb_total_boxes = 1344;
    params->max_boxes_limit = 100;
    params->conf_threshold = 0.5f;
    params->iou_threshold = 0.45f;
    params->nb_detect = 0;

    // Parse JSON if provided
    if (json_str != NULL) {
        cJSON *root = cJSON_Parse(json_str);
        if (root != NULL) {
            cJSON *pp = cJSON_GetObjectItemCaseSensitive(root, "postprocess_params");
            if (pp == NULL) {
                // Compatibility: input is postprocess_params object directly
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

                cJSON *total_boxes = cJSON_GetObjectItemCaseSensitive(pp, "total_boxes");
                if (cJSON_IsNumber(total_boxes)) {
                    params->nb_total_boxes = (int32_t)total_boxes->valuedouble;
                }
            }

            cJSON_Delete(root);
        }
    }
    
    // Allocate output buffers (similar to app_postprocess pattern)
    od_pp_buffer = (od_pp_outBuffer_t *)hal_mem_alloc_large(sizeof(od_pp_outBuffer_t) * params->nb_total_boxes);
    od_detect_buffer = (od_detect_t *)hal_mem_alloc_large(sizeof(od_detect_t) * params->max_boxes_limit);
    
    // Allocate scratch buffer for int8 processing (6 values per box: x,y,w,h,conf,class)
    scratch_buffer = (int8_t *)hal_mem_alloc_large(sizeof(int8_t) * params->nb_total_boxes * 6);
    
    assert(od_pp_buffer != NULL && od_detect_buffer != NULL && scratch_buffer != NULL);
    
    // Set scratch buffer pointer (required for int8 processing)
    params->pScratchBuff = scratch_buffer;
    
    od_yolov8_pp_reset(params);
    *pp_params = (void *)params;
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static int32_t deinit(void *pp_params)
{
    od_yolov8_pp_static_param_t *params = (od_yolov8_pp_static_param_t *)pp_params;
    
    if (od_pp_buffer != NULL) {
        hal_mem_free(od_pp_buffer);
        od_pp_buffer = NULL;
    }
    
    if (od_detect_buffer != NULL) {
        hal_mem_free(od_detect_buffer);
        od_detect_buffer = NULL;
    }
    
    if (scratch_buffer != NULL) {
        hal_mem_free(scratch_buffer);
        scratch_buffer = NULL;
    }
    
    if (static_class_names != NULL) {
        for (int i = 0; i < params->nb_classes; i++) {
            if (static_class_names[i] != NULL) {
                hal_mem_free(static_class_names[i]);
            }
        }
        hal_mem_free(static_class_names);
        static_class_names = NULL;
    }
    
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static void od_pp_out_t_to_pp_result_t(od_pp_out_t *pObjDetOutput, pp_result_t *result)
{
    result->type = PP_TYPE_OD;
    result->is_valid = pObjDetOutput->nb_detect > 0;
    result->od.nb_detect = pObjDetOutput->nb_detect;
    result->od.detects = od_detect_buffer;
    
    // Convert detection format
    for (int i = 0; i < pObjDetOutput->nb_detect; i++) {
        // YOLOv8 outputs pixel coordinates, need to normalize to [0,1] by dividing by input size (256)
        float x_center_norm = pObjDetOutput->pOutBuff[i].x_center;
        float y_center_norm = pObjDetOutput->pOutBuff[i].y_center ;
        float width_norm = pObjDetOutput->pOutBuff[i].width;
        float height_norm = pObjDetOutput->pOutBuff[i].height;
        
        // Convert from center format to corner format and ensure coordinates are within bounds
        result->od.detects[i].x = MAX(0.0f, MIN(1.0f, x_center_norm - width_norm / 2.0f));
        result->od.detects[i].y = MAX(0.0f, MIN(1.0f, y_center_norm - height_norm / 2.0f));
        result->od.detects[i].width = MAX(0.0f, MIN(1.0f, width_norm));
        result->od.detects[i].height = MAX(0.0f, MIN(1.0f, height_norm));
        
        // Handle confidence - int8 models typically output already processed confidence
        result->od.detects[i].conf = MAX(0.0f, MIN(1.0f, pObjDetOutput->pOutBuff[i].conf));
        
        // Ensure class index is valid
        if (pObjDetOutput->pOutBuff[i].class_index >= 0 && pObjDetOutput->pOutBuff[i].class_index < static_params.nb_classes) {
            result->od.detects[i].class_name = static_class_names[pObjDetOutput->pOutBuff[i].class_index];
        } else {
            result->od.detects[i].class_name = "unknown";
        }
    }
}

static int32_t run(void *pInput[], uint32_t nb_input, void *pResult, void *pp_params, void *nn_inst)
{
    assert(nb_input == 1);
    int32_t error = AI_OD_POSTPROCESS_ERROR_NO;
    od_yolov8_pp_static_param_t *params = (od_yolov8_pp_static_param_t *)pp_params;
    
    params->nb_detect = 0;
    memset(pResult, 0, sizeof(pp_result_t));

    od_pp_out_t od_pp_out;
    od_pp_out.pOutBuff = od_pp_buffer;
    
    od_yolov8_pp_in_centroid_t pp_input = {
        .pRaw_detections = pInput[0],  // int8 data
    };
    
    // Use int8 processing function (similar to app_postprocess_od_yolov8_ui.c)
    error = od_yolov8_pp_process_int8(&pp_input, &od_pp_out, params);
    
    if (error == AI_OD_POSTPROCESS_ERROR_NO) {
        od_pp_out_t_to_pp_result_t(&od_pp_out, (pp_result_t *)pResult);
    }
    
    return error;
}

static int32_t set_confidence_threshold(void *params, float threshold)
{
    ((od_yolov8_pp_static_param_t *)params)->conf_threshold = threshold;
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static int32_t set_nms_threshold(void *params, float threshold)
{
    ((od_yolov8_pp_static_param_t *)params)->iou_threshold = threshold;
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static int32_t get_confidence_threshold(void *params, float *threshold)
{
    *threshold = ((od_yolov8_pp_static_param_t *)params)->conf_threshold;
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static int32_t get_nms_threshold(void *params, float *threshold)
{
    *threshold = ((od_yolov8_pp_static_param_t *)params)->iou_threshold;
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
const pp_entry_t pp_entry_od_yolo_v8_ui = {
    .name = "pp_od_yolo_v8_ui",
    .vt = &vt
};
