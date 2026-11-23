/**
******************************************************************************
* @file    pp_doc_presence.c
* @author  CamThink
*
* @brief   Post-process for DocAligner lcnet050 presence-only cut network
*          (document auto-capture trigger).
 *          Model output: single f32 logit [1,1,1,1] = has_obj head Gemm output
 *          (pre-sigmoid; the transformer corner head was cut at export).
 *          Emits one full-frame OBJECT_DETECTION box, conf = sigmoid(logit),
*          valid when conf >= confidence_threshold (official DocAligner gate:
*          has_obj > 0.5).
*          PC reference semantics: capybara_docsaid preprocess = plain resize
*          256x256, BGR order, /255 (input conversion handled by video_ai_node
*          f32 path for float32-input models).
******************************************************************************
* @attention
*
* Copyright (c) 2026 CamThink.
* All rights reserved.
*
******************************************************************************
*/

#include "pp.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "cJSON.h"
#include "mem.h"
#include "debug.h"

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

typedef struct {
    float conf_threshold;   /* presence: sigmoid(logit) >= this */
    char class_name[16];
} pp_doc_presence_params_t;

static int32_t init(const char *json_str, void **pp_params, void *nn_inst)
{
    (void)nn_inst;
    pp_doc_presence_params_t *pp = (pp_doc_presence_params_t *)hal_mem_alloc_any(sizeof(pp_doc_presence_params_t));
    if (!pp) {
        return -1;
    }
    memset(pp, 0, sizeof(pp_doc_presence_params_t));

    pp->conf_threshold = 0.5f;
    strncpy(pp->class_name, "document", sizeof(pp->class_name) - 1);
    if (json_str != NULL) {
        cJSON *root = cJSON_Parse(json_str);
        if (root != NULL) {
            cJSON *obj = cJSON_GetObjectItemCaseSensitive(root, "postprocess_params");
            if (obj == NULL) {
                obj = root;
            }
            if (cJSON_IsObject(obj)) {
                cJSON *j = cJSON_GetObjectItemCaseSensitive(obj, "confidence_threshold");
                if (cJSON_IsNumber(j)) pp->conf_threshold = (float)j->valuedouble;
                j = cJSON_GetObjectItemCaseSensitive(obj, "class_names");
                if (cJSON_IsArray(j) && cJSON_IsString(cJSON_GetArrayItem(j, 0))) {
                    strncpy(pp->class_name, cJSON_GetArrayItem(j, 0)->valuestring,
                            sizeof(pp->class_name) - 1);
                }
            }
            cJSON_Delete(root);
        }
    }
    *pp_params = (void *)pp;
    return 0;
}

static int32_t deinit(void *pp_params)
{
    pp_doc_presence_params_t *pp = (pp_doc_presence_params_t *)pp_params;
    if (pp) {
        hal_mem_free(pp);
    }
    return 0;
}

static int32_t run(void *pInput[], uint32_t nb_input, void *pResult, void *pp_params, void *nn_inst)
{
    (void)nn_inst;
    pp_doc_presence_params_t *pp = (pp_doc_presence_params_t *)pp_params;
    pp_result_t *result = (pp_result_t *)pResult;

    if (nb_input < 1 || !pInput[0]) {
        return -1;
    }

    /* [1,1,1,1] f32 pre-sigmoid logit (has_obj Gemm head) */
    const float logit = ((const float *)pInput[0])[0];
    float conf = 1.0f / (1.0f + expf(-logit));

    memset(result, 0, sizeof(pp_result_t));
    result->type = PP_TYPE_OD;

    LOG_DRV_DEBUG("docp: logit=%.4f conf=%.4f th=%.2f\r\n", logit, conf, pp->conf_threshold);

    if (conf < pp->conf_threshold) {
        result->is_valid = 0;
        return 0;
    }

    static od_detect_t detect;
    detect.x = 0.0f;
    detect.y = 0.0f;
    detect.width = 1.0f;
    detect.height = 1.0f;
    detect.conf = MAX(0.0f, MIN(1.0f, conf));
    detect.class_name = pp->class_name;

    result->is_valid = 1;
    result->od.detects = &detect;
    result->od.nb_detect = 1;
    return 0;
}

static int32_t set_confidence_threshold(void *params, float threshold)
{
    pp_doc_presence_params_t *pp = (pp_doc_presence_params_t *)params;
    pp->conf_threshold = MAX(0.001f, MIN(0.999f, threshold));
    return 0;
}

static int32_t get_confidence_threshold(void *params, float *threshold)
{
    pp_doc_presence_params_t *pp = (pp_doc_presence_params_t *)params;
    *threshold = pp->conf_threshold;
    return 0;
}

static int32_t set_nms_threshold(void *params, float threshold)
{
    /* no boxes; keep conf threshold so the web UI slider stays meaningful */
    return set_confidence_threshold(params, threshold);
}

static int32_t get_nms_threshold(void *params, float *threshold)
{
    pp_doc_presence_params_t *pp = (pp_doc_presence_params_t *)params;
    *threshold = pp->conf_threshold;
    return 0;
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

const pp_entry_t pp_entry_doc_presence = {
    .name = "pp_doc_presence",
    .vt = &vt
};
