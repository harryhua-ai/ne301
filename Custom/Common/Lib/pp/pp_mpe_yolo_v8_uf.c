/**
******************************************************************************
* @file    pp_mpe_yolo_v8_uf.c
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

#include "mpe_yolov8_pp_if.h"

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

static mpe_pp_outBuffer_t *mpe_pp_buffer = NULL;
static mpe_pp_keyPoints_t *mpe_keypoints_buffer = NULL;
static mpe_detect_t *mpe_detect_buffer = NULL;
static mpe_yolov8_pp_static_param_t static_params = {0};
static char **static_class_names = NULL;
static uint8_t *keypoint_connections = NULL; /* flattened pairs: [from0, to0, from1, to1, ...] */
static uint8_t num_connections = 0;
static char **static_kp_names = NULL; /* array of C strings (may be NULL entries) */

/*
Example JSON configuration:
"postprocess_params": {
  "num_classes": 1,
  "class_names": ["person"],
  "confidence_threshold": 0.6,
  "iou_threshold": 0.5,
  "max_detections": 10,
  "num_keypoints": 17,
  "total_boxes": 1344,
  "raw_output_scale": 0.003921569,
  "raw_output_zero_point": 0,
  "keypoint_names": [
    "nose", "left_eye", "right_eye", "left_ear", "right_ear",
    "left_shoulder", "right_shoulder", "left_elbow", "right_elbow",
    "left_wrist", "right_wrist", "left_hip", "right_hip",
    "left_knee", "right_knee", "left_ankle", "right_ankle"
  ],
  "keypoint_connections": [
    [0, 1], [0, 2], [1, 3], [2, 4], [1, 2], [3, 5], [4, 6],
    [5, 6], [5, 7], [7, 9], [6, 8], [8, 10],
    [5, 11], [6, 12], [11, 12],
    [11, 13], [13, 15], [12, 14], [14, 16]
  ]
}
*/
static int32_t init(const char *json_str, void **pp_params, void *nn_inst)
{
    mpe_yolov8_pp_static_param_t *params = (mpe_yolov8_pp_static_param_t *)&static_params;
    
    // Set default values
    params->nb_classes = 2;
    params->nb_total_boxes = 1344;
    params->max_boxes_limit = 10;
    params->conf_threshold = 0.6f;
    params->iou_threshold = 0.5f;
    params->nb_keypoints = 17;
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

                cJSON *num_keypoints = cJSON_GetObjectItemCaseSensitive(pp, "num_keypoints");
                if (cJSON_IsNumber(num_keypoints)) {
                    params->nb_keypoints = (uint32_t)num_keypoints->valuedouble;
                }

                // Parse keypoint names
                cJSON *kp_names = cJSON_GetObjectItemCaseSensitive(pp, "keypoint_names");
                if (cJSON_IsArray(kp_names)) {
                    if (params->nb_keypoints > 0) {
                        static_kp_names = (char **)hal_mem_alloc_any(sizeof(char *) * params->nb_keypoints);
                        for (int i = 0; i < params->nb_keypoints; i++) {
                            cJSON *name = cJSON_GetArrayItem(kp_names, i);
                            if (cJSON_IsString(name)) {
                                uint8_t len = strlen(name->valuestring) + 1;
                                static_kp_names[i] = (char *)hal_mem_alloc_any(len);
                                memcpy(static_kp_names[i], name->valuestring, len);
                            } else {
                                static_kp_names[i] = NULL;
                            }
                        }
                    }
                }

                cJSON *total_boxes = cJSON_GetObjectItemCaseSensitive(pp, "total_boxes");
                if (cJSON_IsNumber(total_boxes)) {
                    params->nb_total_boxes = (int32_t)total_boxes->valuedouble;
                }

                // Parse keypoint connections
                cJSON *connections = cJSON_GetObjectItemCaseSensitive(pp, "keypoint_connections");
                if (cJSON_IsArray(connections)) {
                    num_connections = cJSON_GetArraySize(connections);
                    if (num_connections > 0) {
                        keypoint_connections = (uint8_t *)hal_mem_alloc_any(sizeof(uint8_t) * num_connections * 2);
                        for (int i = 0; i < num_connections; i++) {
                            cJSON *connection = cJSON_GetArrayItem(connections, i);
                            if (cJSON_IsArray(connection) && cJSON_GetArraySize(connection) == 2) {
                                keypoint_connections[i * 2 + 0] = (uint8_t)cJSON_GetArrayItem(connection, 0)->valuedouble;
                                keypoint_connections[i * 2 + 1] = (uint8_t)cJSON_GetArrayItem(connection, 1)->valuedouble;
                            }
                        }
                    }
                }
            }

            cJSON_Delete(root);
        }
    }
    
    // Allocate output buffers
    mpe_pp_buffer = (mpe_pp_outBuffer_t *)hal_mem_alloc_large(sizeof(mpe_pp_outBuffer_t) * params->nb_total_boxes);
    mpe_keypoints_buffer = (mpe_pp_keyPoints_t *)hal_mem_alloc_large(sizeof(mpe_pp_keyPoints_t) * params->nb_total_boxes * params->nb_keypoints);
    mpe_detect_buffer = (mpe_detect_t *)hal_mem_alloc_large(sizeof(mpe_detect_t) * params->max_boxes_limit);
    
    assert(mpe_pp_buffer != NULL && mpe_keypoints_buffer != NULL && mpe_detect_buffer != NULL);
    
    // Initialize keypoints pointers
    for (size_t i = 0; i < params->nb_total_boxes; i++) {
        mpe_pp_buffer[i].pKeyPoints = &mpe_keypoints_buffer[i * params->nb_keypoints];
    }
    
    mpe_yolov8_pp_reset(params);
    *pp_params = (void *)params;
    return AI_MPE_PP_ERROR_NO;
}

static int32_t deinit(void *pp_params)
{
    mpe_yolov8_pp_static_param_t *params = (mpe_yolov8_pp_static_param_t *)pp_params;
    
    if (mpe_pp_buffer != NULL) {
        hal_mem_free(mpe_pp_buffer);
        mpe_pp_buffer = NULL;
    }
    
    if (mpe_keypoints_buffer != NULL) {
        hal_mem_free(mpe_keypoints_buffer);
        mpe_keypoints_buffer = NULL;
    }
    
    if (mpe_detect_buffer != NULL) {
        hal_mem_free(mpe_detect_buffer);
        mpe_detect_buffer = NULL;
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
    
    if (static_kp_names != NULL) {
        for (int i = 0; i < params->nb_keypoints; i++) {
            if (static_kp_names[i] != NULL) {
                hal_mem_free(static_kp_names[i]);
            }
        }
        hal_mem_free(static_kp_names);
        static_kp_names = NULL;
    }

    if (keypoint_connections != NULL) {
        hal_mem_free(keypoint_connections);
        keypoint_connections = NULL;
        num_connections = 0;
    }
    
    return AI_MPE_PP_ERROR_NO;
}

static void mpe_pp_out_t_to_pp_result_t(mpe_pp_out_t *pMpeOutput, pp_result_t *result)
{
    result->type = PP_TYPE_MPE;
    result->is_valid = pMpeOutput->nb_detect > 0;
    result->mpe.nb_detect = pMpeOutput->nb_detect;
    result->mpe.detects = mpe_detect_buffer;
    // Convert detection format if needed
    for (int i = 0; i < pMpeOutput->nb_detect; i++) {
        // Ensure coordinates are within bounds
        result->mpe.detects[i].x = MAX(0.0f, pMpeOutput->pOutBuff[i].x_center - pMpeOutput->pOutBuff[i].width / 2.0f);
        result->mpe.detects[i].y = MAX(0.0f, pMpeOutput->pOutBuff[i].y_center - pMpeOutput->pOutBuff[i].height / 2.0f);
        result->mpe.detects[i].width = MIN(1.0f, pMpeOutput->pOutBuff[i].width);
        result->mpe.detects[i].height = MIN(1.0f, pMpeOutput->pOutBuff[i].height);
        result->mpe.detects[i].conf = pMpeOutput->pOutBuff[i].conf;
        result->mpe.detects[i].class_name = static_class_names[pMpeOutput->pOutBuff[i].class_index];
        for (int j = 0; j < static_params.nb_keypoints; j++) {
            result->mpe.detects[i].keypoints[j].x = MAX(0.0f, pMpeOutput->pOutBuff[i].pKeyPoints[j].x);
            result->mpe.detects[i].keypoints[j].y = MAX(0.0f, pMpeOutput->pOutBuff[i].pKeyPoints[j].y);
            result->mpe.detects[i].keypoints[j].conf = MIN(1.0f, pMpeOutput->pOutBuff[i].pKeyPoints[j].conf);
        }
        result->mpe.detects[i].nb_keypoints = static_params.nb_keypoints;
        result->mpe.detects[i].num_connections = num_connections;
        result->mpe.detects[i].keypoint_connections = keypoint_connections;
        result->mpe.detects[i].keypoint_names = static_kp_names;
    }
}

static int32_t run(void *pInput[], uint32_t nb_input, void *pResult, void *pp_params, void *nn_inst)
{
    assert(nb_input == 1);
    int32_t error = AI_MPE_PP_ERROR_NO;
    mpe_yolov8_pp_static_param_t *params = (mpe_yolov8_pp_static_param_t *)pp_params;
    
    params->nb_detect = 0;
    memset(pResult, 0, sizeof(pp_result_t));

    mpe_pp_out_t mpe_pp_out;
    mpe_pp_out.pOutBuff = mpe_pp_buffer;
    
    mpe_yolov8_pp_in_centroid_t pp_input = {
        .pRaw_detections = pInput[0],
    };
    
    error = mpe_yolov8_pp_process(&pp_input, &mpe_pp_out, params);
    if (error == AI_MPE_PP_ERROR_NO) {
        mpe_pp_out_t_to_pp_result_t(&mpe_pp_out, (pp_result_t *)pResult);
    }
    
    return error;
}

static int32_t set_confidence_threshold(void *params, float threshold)
{
    ((mpe_yolov8_pp_static_param_t *)params)->conf_threshold = threshold;
    return AI_MPE_PP_ERROR_NO;
}

static int32_t set_nms_threshold(void *params, float threshold)
{
    ((mpe_yolov8_pp_static_param_t *)params)->iou_threshold = threshold;
    return AI_MPE_PP_ERROR_NO;
}

static int32_t get_confidence_threshold(void *params, float *threshold)
{
    *threshold = ((mpe_yolov8_pp_static_param_t *)params)->conf_threshold;
    return AI_MPE_PP_ERROR_NO;
}

static int32_t get_nms_threshold(void *params, float *threshold)
{
    *threshold = ((mpe_yolov8_pp_static_param_t *)params)->iou_threshold;
    return AI_MPE_PP_ERROR_NO;
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
const pp_entry_t pp_entry_mpe_yolo_v8_uf = {
    .name = "pp_mpe_yolo_v8_uf",
    .vt = &vt
};
