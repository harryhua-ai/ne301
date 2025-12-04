/**
******************************************************************************
* @file    pp_spe_movenet_uf.c
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

#include "spe_movenet_pp_if.h"

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

static spe_pp_outBuffer_t *spe_pp_buffer = NULL;
static spe_keypoint_t *spe_keypoints_buffer = NULL;
static spe_movenet_pp_static_param_t static_params = {0};
static char **static_kp_names = NULL;
static uint8_t *keypoint_connections = NULL;
static uint8_t num_connections = 0;

/*
Example JSON configuration:
"postprocess_params": {
  "num_keypoints": 17,
  "heatmap_width": 64,
  "heatmap_height": 64,
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
    spe_movenet_pp_static_param_t *params = (spe_movenet_pp_static_param_t *)&static_params;

    params->heatmap_width = 64;
    params->heatmap_height = 64;
    params->nb_keypoints = 17;

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
                cJSON *num_kp = cJSON_GetObjectItemCaseSensitive(pp, "num_keypoints");
                if (cJSON_IsNumber(num_kp)) {
                    params->nb_keypoints = (uint32_t)num_kp->valuedouble;
                }

                cJSON *hm_w = cJSON_GetObjectItemCaseSensitive(pp, "heatmap_width");
                if (cJSON_IsNumber(hm_w)) {
                    params->heatmap_width = (int32_t)hm_w->valuedouble;
                }

                cJSON *hm_h = cJSON_GetObjectItemCaseSensitive(pp, "heatmap_height");
                if (cJSON_IsNumber(hm_h)) {
                    params->heatmap_height = (int32_t)hm_h->valuedouble;
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
    spe_pp_buffer = (spe_pp_outBuffer_t *)hal_mem_alloc_large(sizeof(spe_pp_outBuffer_t) * params->nb_keypoints);
    spe_keypoints_buffer = (spe_keypoint_t *)hal_mem_alloc_large(sizeof(spe_keypoint_t) * params->nb_keypoints);
    assert(spe_pp_buffer != NULL && spe_keypoints_buffer != NULL);
    
    spe_movenet_pp_reset(params);
    *pp_params = (void *)params;
    return AI_SPE_POSTPROCESS_ERROR_NO;
}

static int32_t deinit(void *pp_params)
{
    (void)pp_params;  // Unused parameter
    
    if (spe_pp_buffer != NULL) {
        hal_mem_free(spe_pp_buffer);
        spe_pp_buffer = NULL;
    }
    
    if (spe_keypoints_buffer != NULL) {
        hal_mem_free(spe_keypoints_buffer);
        spe_keypoints_buffer = NULL;
    }
    
    if (static_kp_names != NULL) {
        for (int i = 0; i < static_params.nb_keypoints; i++) {
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
    
    return AI_SPE_POSTPROCESS_ERROR_NO;
}

static void spe_pp_out_t_to_pp_result_t(spe_pp_out_t *pSpeOutput, pp_result_t *result)
{
    result->type = PP_TYPE_SPE;
    result->is_valid = (pSpeOutput->pOutBuff != NULL);
    result->spe.nb_keypoints = static_params.nb_keypoints;
    result->spe.keypoints = spe_keypoints_buffer;
    result->spe.keypoint_names = static_kp_names;
    result->spe.num_connections = num_connections;
    result->spe.keypoint_connections = keypoint_connections;
    
    // Convert keypoint format
    for (int i = 0; i < static_params.nb_keypoints; i++) {
        result->spe.keypoints[i].x = MAX(0.0f, MIN(1.0f, pSpeOutput->pOutBuff[i].x_center));
        result->spe.keypoints[i].y = MAX(0.0f, MIN(1.0f, pSpeOutput->pOutBuff[i].y_center));
        result->spe.keypoints[i].conf = MAX(0.0f, MIN(1.0f, pSpeOutput->pOutBuff[i].proba));
    }
}

static int32_t run(void *pInput[], uint32_t nb_input, void *pResult, void *pp_params, void *nn_inst)
{
    assert(nb_input == 1);
    int32_t error = AI_SPE_POSTPROCESS_ERROR_NO;
    spe_movenet_pp_static_param_t *params = (spe_movenet_pp_static_param_t *)pp_params;

    memset(pResult, 0, sizeof(pp_result_t));

    spe_pp_out_t spe_pp_out;
    spe_pp_out.pOutBuff = spe_pp_buffer;
    
    spe_movenet_pp_in_t pp_input = {
        .inBuff = (float32_t *)pInput[0],
    };
    
    error = spe_movenet_pp_process(&pp_input, &spe_pp_out, params);
    if (error == AI_SPE_POSTPROCESS_ERROR_NO) {
        spe_pp_out_t_to_pp_result_t(&spe_pp_out, (pp_result_t *)pResult);
    }
    return error;
}

static int32_t set_confidence_threshold(void *params, float threshold)
{
    // MoveNet doesn't use confidence threshold in the same way
    // This is a no-op but kept for interface compatibility
    (void)params;
    (void)threshold;
    return AI_SPE_POSTPROCESS_ERROR_NO;
}

static int32_t set_nms_threshold(void *params, float threshold)
{
    // MoveNet doesn't use NMS threshold
    // This is a no-op but kept for interface compatibility
    (void)params;
    (void)threshold;
    return AI_SPE_POSTPROCESS_ERROR_NO;
}

static int32_t get_confidence_threshold(void *params, float *threshold)
{
    // MoveNet doesn't use confidence threshold
    // This is a no-op but kept for interface compatibility
    (void)params;
    *threshold = 0.0f;
    return AI_SPE_POSTPROCESS_ERROR_NO;
}

static int32_t get_nms_threshold(void *params, float *threshold)
{
    // MoveNet doesn't use NMS threshold
    // This is a no-op but kept for interface compatibility
    (void)params;
    *threshold = 0.0f;
    return AI_SPE_POSTPROCESS_ERROR_NO;
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
const pp_entry_t pp_entry_spe_movenet_uf = {
    .name = "pp_spe_movenet_uf",
    .vt = &vt
};

