/**
 * @file nn.c
 * @brief AI Neural Network Module Implementation
 * @details Implementation of AI neural network module based on STM32N6 NPU
 */

#include "common_utils.h"
#include "generic_math.h"
#include "debug.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "ll_aton_runtime.h"
#include "ll_aton_reloc_network.h"
#include "pp.h"
#include "nn.h"
#include "cJSON.h"
#include "storage.h"
#include "mpool.h"
#include "camera.h"
#include "mem_map.h"

/* ==================== internal data structure ==================== */

// global AI neural network module instance
static nn_t g_nn = {0};
// thread attribute configuration
const osThreadAttr_t nnTask_attributes = {
    .name = "nnTask",
    .priority = (osPriority_t) osPriorityHigh,  // AI task priority is high
    .stack_size = 2 * 1024,
};

const osThreadAttr_t nnCameraTask_attributes = {
    .name = "nnCameraTask",
    .priority = (osPriority_t) osPriorityHigh,  // AI task priority is high
    .stack_size = 2 * 1024,
};
/* ==================== internal function declaration ==================== */
static int model_run(nn_t *nn, nn_result_t *result, bool is_callback);
/* ==================== main process thread ==================== */

static void invalidate_output_cache(nn_t *nn)
{
    for (uint32_t i = 0; i < nn->output_buffer_count; i++) {
        SCB_InvalidateDCache_by_Addr(nn->output_buffer[i], nn->output_buffer_size[i]);
    }
}

static void flush_input_cache(nn_t *nn)
{
    for (uint32_t i = 0; i < nn->input_buffer_count; i++) {
        SCB_CleanInvalidateDCache_by_Addr(nn->input_buffer[i], nn->input_buffer_size[i]);
    }
}

static void nnProcess(void *argument)
{
    nn_t *nn = (nn_t *)argument;
    nn_result_t result;
    LOG_DRV_INFO("nnProcess start\r\r\n");

    nn->is_init = true;

    while (nn->is_init) {
        // check inference state
        osMutexAcquire(nn->mtx_id, osWaitForever);
        if (nn->state == NN_STATE_RUNNING) {
            model_run(nn, &result, true);
            osMutexRelease(nn->mtx_id);
        } else {
            osMutexRelease(nn->mtx_id);
            osDelay(30);
        }
    }

    LOG_DRV_ERROR("nnProcess exit\r\r\n");

    nn->nn_processId = NULL;
    osThreadExit();
}

static int nn_init(void *priv)
{
    LOG_DRV_DEBUG("nn_init\r\r\n");

    nn_t *nn = (nn_t *)priv;

    // create mutex and semaphore
    nn->mtx_id = osMutexNew(NULL);
    nn->sem_id = osSemaphoreNew(1, 0, NULL);

    if (!nn->mtx_id || !nn->sem_id) {
        LOG_DRV_ERROR("Failed to create RTOS objects\r\r\n");
        return -1;
    }

    // create process thread
    nn->nn_processId = osThreadNew(nnProcess, nn, &nnTask_attributes);
    if (!nn->nn_processId) {
        LOG_DRV_ERROR("Failed to create NN process thread\r\r\n");
        return -1;
    }

    // set initial state
    nn->state = NN_STATE_INIT;
    nn->is_init = true;
    LOG_DRV_INFO("NN module initialized successfully\r\r\n");
    return 0;
}

static int nn_deinit(void *priv)
{
    nn_t *nn = (nn_t *)priv;

    LOG_DRV_DEBUG("nn_deinit\r\r\n");

    // stop process thread
    nn->is_init = false;
    if (nn->sem_id) {
        osSemaphoreRelease(nn->sem_id);
    }

    if (nn->nn_processId && osThreadGetId() != nn->nn_processId) {
        osThreadTerminate(nn->nn_processId);
        nn->nn_processId = NULL;
    }

    // clean up RTOS objects
    if (nn->sem_id) {
        osSemaphoreDelete(nn->sem_id);
        nn->sem_id = NULL;
    }

    if (nn->mtx_id) {
        osMutexDelete(nn->mtx_id);
        nn->mtx_id = NULL;
    }

    // reset state
    nn->state = NN_STATE_UNINIT;

    LOG_DRV_INFO("NN module deinitialized\r\r\n");
    return 0;
}

static int nn_start(nn_t *nn)
{
    LOG_DRV_DEBUG("nn_start\r\r\n");

    if (nn->state != NN_STATE_READY) {
        LOG_DRV_WARN("NN not ready, current state: %d\r\r\n", nn->state);
        return -1;
    }

    nn->state = NN_STATE_RUNNING;

    LOG_DRV_INFO("NN inference started\r\r\n");
    return 0;
}

static int nn_stop(nn_t *nn)
{
    LOG_DRV_DEBUG("nn_stop\r\r\n");

    if (nn->state != NN_STATE_RUNNING) {
        LOG_DRV_WARN("NN not running, current state: %d\r\r\n", nn->state);
        return -1;
    }

    nn->state = NN_STATE_READY;

    LOG_DRV_INFO("NN inference stopped\r\r\n");
    return 0;
}

/* ==================== internal auxiliary function implementation ==================== */

static int load_info(const uintptr_t file_ptr, nn_model_info_t *info)
{
    if (!file_ptr || !info) {
        return -1;
    }

    nn_package_header_t *header = (nn_package_header_t *)file_ptr;
    info->metadata_ptr = file_ptr + header->metadata_offset;
    /* Model configuration pointer */
    info->config_ptr = file_ptr + header->model_config_offset;
    /* Model pointer */
    info->model_ptr = file_ptr + header->relocatable_model_offset;
    info->model_size = header->relocatable_model_size;
    /* Model configuration */
    cJSON *root = cJSON_Parse((const char *)info->config_ptr);
    if (root == NULL) {
        LOG_DRV_ERROR("load_info: JSON parse failed\r\r\n");
        return -1;
    }

    /* Model name */
    cJSON *json = cJSON_GetObjectItemCaseSensitive(root, "model_info");
    if (cJSON_IsObject(json)) {
        cJSON *name = cJSON_GetObjectItemCaseSensitive(json, "name");
        if (cJSON_IsString(name)) {
            strncpy(info->name, name->valuestring, sizeof(info->name) - 1);
        }
        cJSON *version = cJSON_GetObjectItemCaseSensitive(json, "version");
        if (cJSON_IsString(version)) {
            strncpy(info->version, version->valuestring, sizeof(info->version) - 1);
        }
        cJSON *description = cJSON_GetObjectItemCaseSensitive(json, "description");
        if (cJSON_IsString(description)) {
            strncpy(info->description, description->valuestring, sizeof(info->description) - 1);
        }
        cJSON *author = cJSON_GetObjectItemCaseSensitive(json, "author");
        if (cJSON_IsString(author)) {
            strncpy(info->author, author->valuestring, sizeof(info->author) - 1);
        }
    }

    /* Model input specification */
    json = cJSON_GetObjectItemCaseSensitive(root, "input_spec");
    if (cJSON_IsObject(json)) {
        cJSON *width = cJSON_GetObjectItemCaseSensitive(json, "width");
        if (cJSON_IsNumber(width)) {
            info->input_width = (int)width->valuedouble;
        }
        cJSON *height = cJSON_GetObjectItemCaseSensitive(json, "height");
        if (cJSON_IsNumber(height)) {
            info->input_height = (int)height->valuedouble;
        }
        cJSON *channels = cJSON_GetObjectItemCaseSensitive(json, "channels");
        if (cJSON_IsNumber(channels)) {
            info->input_channels = (int)channels->valuedouble;
        }
        cJSON *data_type = cJSON_GetObjectItemCaseSensitive(json, "data_type");
        if (cJSON_IsString(data_type)) {
            strncpy(info->input_data_type, data_type->valuestring, sizeof(info->input_data_type) - 1);
        }
        cJSON *color_format = cJSON_GetObjectItemCaseSensitive(json, "color_format");
        if (cJSON_IsString(color_format)) {
            strncpy(info->color_format, color_format->valuestring, sizeof(info->color_format) - 1);
        }
    }

    /* Model output specification */
    json = cJSON_GetObjectItemCaseSensitive(root, "output_spec");
    if (cJSON_IsObject(json)) {
        cJSON *outputs = cJSON_GetObjectItemCaseSensitive(json, "outputs");
        if (cJSON_IsArray(outputs)) {
            cJSON *output = cJSON_GetArrayItem(outputs, 0);
            if (cJSON_IsObject(output)) {
                cJSON *data_type = cJSON_GetObjectItemCaseSensitive(output, "data_type");
                if (cJSON_IsString(data_type)) {
                    strncpy(info->output_data_type, data_type->valuestring, sizeof(info->output_data_type) - 1);
                }
            }
        }
    }

    /* Post-processing type */
    json = cJSON_GetObjectItemCaseSensitive(root, "postprocess_type");
    if (cJSON_IsString(json)) {
        strncpy(info->postprocess_type, json->valuestring, sizeof(info->postprocess_type) - 1);
    }

    cJSON_Delete(root);
    
    /* Metadata */
    root = cJSON_Parse((const char *)info->metadata_ptr);
    if (root == NULL) {
        LOG_DRV_ERROR("load_info: JSON parse failed\r\r\n");
        return -1;
    }
    /* Creation time */
    json = cJSON_GetObjectItemCaseSensitive(root, "created_at");
    if (cJSON_IsString(json)) {
        strncpy(info->created_at, json->valuestring, sizeof(info->created_at) - 1);
    }

    cJSON_Delete(root);

    return 0;
}

static void update_inference_stats(uint32_t inference_time)
{
    g_nn.inference_count++;
    g_nn.total_inference_time += inference_time;
}

static int model_init(const uintptr_t model_ptr, nn_t *nn)
{
    if (!model_ptr) {
        return -1;
    }

    /* Print model information */
    ll_aton_reloc_log_info(model_ptr);

    /* Get model information */
    ll_aton_reloc_info rt;
    int res = ll_aton_reloc_get_info(model_ptr, &rt);
    if (res != 0) {
        LOG_DRV_ERROR("ll_aton_reloc_get_info failed %d\r\r\n", res);
        return -1;
    }

    /* Initialize LL_ATON runtime */
    LL_ATON_RT_RuntimeInit();
    /* Create and install an instance of the relocatable model */
    ll_aton_reloc_config config;

    nn->exec_ram_addr = hal_mem_alloc_large(rt.rt_ram_copy);
    nn->ext_ram_addr = hal_mem_alloc_large(rt.ext_ram_sz);
    if (nn->exec_ram_addr == NULL || nn->ext_ram_addr == NULL) {
        LOG_DRV_ERROR("model_init: OOM\r\r\n");
        return -1;
    }
    config.exec_ram_addr = (uintptr_t)nn->exec_ram_addr;
    config.exec_ram_size = rt.rt_ram_copy;
    config.ext_ram_addr = (uintptr_t)nn->ext_ram_addr;
    config.ext_ram_size = rt.ext_ram_sz;
    config.ext_param_addr = 0;  /* or @ of the weights/params if split mode is used */
    config.mode = AI_RELOC_RT_LOAD_MODE_COPY; // | AI_RELOC_RT_LOAD_MODE_CLEAR;

    LOG_DRV_INFO("Installing relocatable model...\r\r\n");
    LOG_DRV_INFO("  Model file: 0x%08lX\r\r\n", (uint32_t)model_ptr);
    LOG_DRV_INFO("  Exec RAM: 0x%08lX (size: %lu)\r\r\n", (uint32_t)config.exec_ram_addr, config.exec_ram_size);
    LOG_DRV_INFO("  Ext RAM: 0x%08lX (size: %u)\r\r\n", (uint32_t)config.ext_ram_addr, config.ext_ram_size);
    LOG_DRV_INFO("  Mode: 0x%08lX\r\r\n", config.mode);

    nn->nn_inst = (NN_Instance_TypeDef *)hal_mem_alloc_any(sizeof(NN_Instance_TypeDef));
    if (nn->nn_inst == NULL) {
        LOG_DRV_ERROR("model_init: OOM\r\r\n");
        return -1;
    }

    res = ll_aton_reloc_install(model_ptr, &config, nn->nn_inst);
    if (res != 0) {
        LOG_DRV_ERROR("ll_aton_reloc_install failed %d\r\r\n", res);
        hal_mem_free(nn->nn_inst);
        nn->nn_inst = NULL;
        return -1;
    }

    const LL_Buffer_InfoTypeDef *ll_buffer = NULL;
    while (nn->input_buffer_count < NN_MAX_INPUT_BUFFER) {
        ll_buffer = ll_aton_reloc_get_input_buffers_info(nn->nn_inst, nn->input_buffer_count);
        if (ll_buffer == NULL || ll_buffer->name == NULL) {
            break;
        }
        nn->input_buffer[nn->input_buffer_count] = (void *)LL_Buffer_addr_start(ll_buffer);
        nn->input_buffer_size[nn->input_buffer_count] = LL_Buffer_len(ll_buffer);
        LOG_DRV_DEBUG("input_buffer[%d]: 0x%08lX (size: %lu)\r\r\n", nn->input_buffer_count,
                      (uint32_t)nn->input_buffer[nn->input_buffer_count], nn->input_buffer_size[nn->input_buffer_count]);
        nn->input_buffer_count++;
    }

    while (nn->output_buffer_count < NN_MAX_OUTPUT_BUFFER) {
        ll_buffer = ll_aton_reloc_get_output_buffers_info(nn->nn_inst, nn->output_buffer_count);
        if (ll_buffer == NULL || ll_buffer->name == NULL) {
            break;
        }
        nn->output_buffer[nn->output_buffer_count] = (void *)LL_Buffer_addr_start(ll_buffer);
        nn->output_buffer_size[nn->output_buffer_count] = LL_Buffer_len(ll_buffer);
        LOG_DRV_DEBUG("output_buffer[%d]: 0x%08lX (size: %lu)\r\r\n", nn->output_buffer_count,
                      (uint32_t)nn->output_buffer[nn->output_buffer_count], nn->output_buffer_size[nn->output_buffer_count]);
        nn->output_buffer_count++;
    }

    LL_ATON_RT_Init_Network(nn->nn_inst);

    return 0;

}

static int model_deinit(nn_t *nn)
{
    if (nn->nn_inst) {
        LL_ATON_RT_DeInit_Network(nn->nn_inst);
        LL_ATON_RT_RuntimeDeInit();
        hal_mem_free(nn->nn_inst);
        nn->nn_inst = NULL;
    }
    /* reset IO buffer pointers, sizes and counts */
    if (nn->input_buffer_count) {
        for (uint32_t i = 0; i < nn->input_buffer_count; i++) {
            nn->input_buffer[i] = NULL;
            nn->input_buffer_size[i] = 0;
        }
        nn->input_buffer_count = 0;
    }
    if (nn->output_buffer_count) {
        for (uint32_t i = 0; i < nn->output_buffer_count; i++) {
            nn->output_buffer[i] = NULL;
            nn->output_buffer_size[i] = 0;
        }
        nn->output_buffer_count = 0;
    }
    if (nn->exec_ram_addr) {
        hal_mem_free(nn->exec_ram_addr);
        nn->exec_ram_addr = NULL;
    }
    if (nn->ext_ram_addr) {
        hal_mem_free(nn->ext_ram_addr);
        nn->ext_ram_addr = NULL;
    }
    return 0;
}

static int model_run(nn_t *nn, nn_result_t *result, bool is_callback)
{
    if (nn->nn_inst) {
        /* flush input cache */
        flush_input_cache(nn);
        /* start time */
        uint32_t start_time = osKernelGetTickCount();
        /* Run inference using LL_ATON */
        LL_ATON_RT_RetValues_t ll_aton_ret;
        do {
            ll_aton_ret = LL_ATON_RT_RunEpochBlock(nn->nn_inst);
            if (ll_aton_ret == LL_ATON_RT_WFE) {
                LL_ATON_OSAL_WFE();
            }
        } while (ll_aton_ret != LL_ATON_RT_DONE);
        /* reset network */
        LL_ATON_RT_Reset_Network(nn->nn_inst);
        /* invalidate output cache before CPU reads NPU results */
        invalidate_output_cache(nn);
        /* postprocess */
        if (nn->pp_vt && nn->pp_vt->run) {
            if (nn->pp_vt->run((void **)nn->output_buffer, nn->output_buffer_count, result, nn->pp_params, nn->nn_inst) != 0) {
                LOG_DRV_ERROR("model_run: postprocess run failed\r\r\n");
                return -1;
            }
            /* end time */
            update_inference_stats(osKernelGetTickCount() - start_time);
            if (is_callback && nn->callback) {
                nn->callback(result, nn->callback_user_data);
            }
        }
        return 0;
    }
    return -1;
}

static int load_model(const uintptr_t file_ptr)
{
    if (!file_ptr) {
        return -1;
    }

    if (g_nn.state != NN_STATE_INIT) {
        LOG_DRV_ERROR("load_model: model not unloaded\r\r\n");
        return -1;
    }

    LOG_DRV_INFO("Loading model: 0x%lx\r\r\n", file_ptr);

    /* ensure a clean IO buffer state before initializing a new model */
    g_nn.input_buffer_count = 0;
    g_nn.output_buffer_count = 0;
    for (uint32_t i = 0; i < NN_MAX_INPUT_BUFFER; i++) {
        g_nn.input_buffer[i] = NULL;
        g_nn.input_buffer_size[i] = 0;
    }
    for (uint32_t i = 0; i < NN_MAX_OUTPUT_BUFFER; i++) {
        g_nn.output_buffer[i] = NULL;
        g_nn.output_buffer_size[i] = 0;
    }

    /* load model information */
    if (load_info(file_ptr, &g_nn.model) != 0) {
        LOG_DRV_ERROR("load_model: load model info failed\r\r\n");
        return -1;
    }

    /* initialize model */
    if (model_init(g_nn.model.model_ptr, &g_nn) != 0) {
        LOG_DRV_ERROR("load_model: model init failed\r\r\n");
        return -1;
    }
    
    /* load postprocess */
    const pp_vtable_t *pp_vt = pp_find((const char *)g_nn.model.postprocess_type);
    if (pp_vt == NULL) {
        LOG_DRV_ERROR("load_model: postprocess type[%s] not found\r\r\n", g_nn.model.postprocess_type);
        return -1;
    }

    /* initialize postprocess */
    if (pp_vt->init && pp_vt->init((const char *)g_nn.model.config_ptr, &g_nn.pp_params, g_nn.nn_inst) != 0) {
        LOG_DRV_ERROR("load_model: postprocess init failed\r\r\n");
        return -1;
    }

    g_nn.pp_vt = pp_vt;
    // update state
    g_nn.state = NN_STATE_READY;

    LOG_DRV_INFO("Model loaded successfully\r\r\n");
    return 0;
}

static int unload_model(void)
{
    if (g_nn.state != NN_STATE_READY) {
        LOG_DRV_ERROR("unload_model: model has not been loaded\r\r\n");
        return -1;
    }

    LOG_DRV_INFO("Unloading model\r\r\n");

    // deinit postprocess
    if (g_nn.pp_vt && g_nn.pp_vt->deinit) {
        g_nn.pp_vt->deinit(g_nn.pp_params);
    }
    g_nn.pp_vt = NULL;
    g_nn.pp_params = NULL;
    // unload model
    model_deinit(&g_nn);
    // clear model information
    memset(&g_nn.model, 0, sizeof(nn_model_info_t));

    // update state
    g_nn.state = NN_STATE_INIT;

    LOG_DRV_INFO("Model unloaded successfully\r\r\n");
    return 0;
}

static int validate_model(const uintptr_t file_ptr)
{
    if (!file_ptr) {
        return -1;
    }

    nn_package_header_t *header = (nn_package_header_t *)file_ptr;

    /* Check magic number */
    if (header->magic != MODEL_PACKAGE_MAGIC) {
        LOG_DRV_ERROR("Invalid package magic number\r\r\n");
        return NN_ERROR_INVALID_PACKAGE;
    }

    /* Check version */
    if (header->version > MODEL_PACKAGE_VERSION) {
        LOG_DRV_ERROR("Incompatible package version 0X%lx\r\r\n", header->version);
        return NN_ERROR_INCOMPATIBLE;
    }

    /* Quick size validation */
    if (header->package_size == 0 || header->relocatable_model_size == 0) {
        LOG_DRV_ERROR("Invalid package size\r\r\n");
        return NN_ERROR_INVALID_PACKAGE;
    }

    /* Validate relocatable model magic */
    const uint32_t *model_magic = (const uint32_t *)(file_ptr + header->relocatable_model_offset);
    if (*model_magic != MODEL_RELOCATABLE_MAGIC) {
        LOG_DRV_ERROR("Invalid relocatable model magic number\r\r\n");
        return NN_ERROR_INVALID_MODEL;
    }

    /* Validate header checksum */
    uint32_t checksum = generic_crc32((const uint8_t *)header, offsetof(nn_package_header_t, header_checksum));
    if (checksum != header->header_checksum) {
        LOG_DRV_ERROR("Invalid header checksum\r\r\n");
        return NN_ERROR_INVALID_CHECKSUM;
    }

    /* Validate model checksum */
    checksum = generic_crc32((const uint8_t *)(file_ptr + header->relocatable_model_offset),
                             header->relocatable_model_size);
    if (checksum != header->model_checksum) {
        LOG_DRV_ERROR("Invalid relocatable model checksum\r\r\n");
        return NN_ERROR_INVALID_CHECKSUM;
    }

    /* Validate config checksum */
    checksum = generic_crc32((const uint8_t *)(file_ptr + header->model_config_offset), header->model_config_size);
    if (checksum != header->config_checksum) {
        LOG_DRV_ERROR("Invalid config checksum\r\r\n");
        return NN_ERROR_INVALID_CHECKSUM;
    }

    return NN_ERROR_OK;
}

static void nn_camera_process(void *argument)
{
    uint8_t *task_state = (uint8_t *)argument;
    int fb_len = 0;
    uint8_t *fb = NULL;
    nn_result_t result;
    device_t *camera_dev = device_find_pattern(CAMERA_DEVICE_NAME, DEV_TYPE_VIDEO);

    LOG_DRV_INFO("nn_camera_process start\r\r\n");

    while (*task_state == 1) {
        fb_len = device_ioctl(camera_dev, CAM_CMD_GET_PIPE2_BUFFER, (uint8_t *)&fb, 0);
        if (fb_len > 0) {
            if (nn_inference_frame(fb, fb_len, &result) == 0) {
                if (result.type == PP_TYPE_OD && result.is_valid) {
                    LOG_SIMPLE("---------------start-----------------\r\n");
                    LOG_SIMPLE("result.od.nb_detect: %d\r\n", result.od.nb_detect);
                    for (size_t i = 0; i < result.od.nb_detect; i++) {
                        LOG_SIMPLE("result.od.index: %d\r\n", i);
                        LOG_SIMPLE("result.od.class_name: %s\r\n", result.od.detects[i].class_name);
                        LOG_SIMPLE("result.od.confidence: %f\r\n", result.od.detects[i].conf);
                        LOG_SIMPLE("result.od.bbox.x: %f\r\n", result.od.detects[i].x);
                        LOG_SIMPLE("result.od.bbox.y: %f\r\n", result.od.detects[i].y);
                        LOG_SIMPLE("result.od.bbox.width: %f\r\n", result.od.detects[i].width);
                        LOG_SIMPLE("result.od.bbox.height: %f\r\n", result.od.detects[i].height);
                    }
                    LOG_SIMPLE("---------------end-----------------\r\n");
                } else if (result.type == PP_TYPE_MPE && result.is_valid) {
                    LOG_SIMPLE("---------------start-----------------\r\n");
                    LOG_SIMPLE("result.mpe.nb_detect: %d\r\n", result.mpe.nb_detect);
                    for (size_t i = 0; i < result.mpe.nb_detect; i++) {
                        LOG_SIMPLE("result.mpe.index: %d\r\n", i);
                        LOG_SIMPLE("result.mpe.class_name: %s\r\n", result.mpe.detects[i].class_name);
                        LOG_SIMPLE("result.mpe.confidence: %f\r\n", result.mpe.detects[i].conf);
                        LOG_SIMPLE("result.mpe.bbox.x: %f\r\n", result.mpe.detects[i].x);
                        LOG_SIMPLE("result.mpe.bbox.y: %f\r\n", result.mpe.detects[i].y);
                        LOG_SIMPLE("result.mpe.bbox.width: %f\r\n", result.mpe.detects[i].width);
                        LOG_SIMPLE("result.mpe.bbox.height: %f\r\n", result.mpe.detects[i].height);
                        LOG_SIMPLE("result.mpe.nb_keypoints: %d\r\n", result.mpe.detects[i].nb_keypoints);
                        LOG_SIMPLE("result.mpe.num_connections: %d\r\n", result.mpe.detects[i].num_connections);
                        for (size_t j = 0; j < result.mpe.detects[i].nb_keypoints; j++) {
                            LOG_SIMPLE("result.mpe.keypoint_names: %s\r\n", result.mpe.detects[i].keypoint_names[j]);
                            LOG_SIMPLE("result.mpe.keypoints[%d].x: %f\r\n", j, result.mpe.detects[i].keypoints[j].x);
                            LOG_SIMPLE("result.mpe.keypoints[%d].y: %f\r\n", j, result.mpe.detects[i].keypoints[j].y);
                            LOG_SIMPLE("result.mpe.keypoints[%d].confidence: %f\r\n", j, result.mpe.detects[i].keypoints[j].conf);
                        }
                    }
                    LOG_SIMPLE("---------------end-----------------\r\n");
                }
            }
            device_ioctl(camera_dev, CAM_CMD_RETURN_PIPE2_BUFFER, fb, 0);
        }
        osDelay(1);
    }
    
    LOG_DRV_INFO("nn_camera_process exit\r\r\n");
    *task_state = 2;
    osThreadExit();
}

static uint8_t task_stat = 0; // 0: stop, 1: start, 2:exit
static osThreadId_t nnCameraTask_id = NULL;
static int nn_camera_start(void)
{
    int ret;
    pipe_params_t pipe_param = {0};
    nn_model_info_t model_info = {0};
    device_t *camera_dev = device_find_pattern(CAMERA_DEVICE_NAME, DEV_TYPE_VIDEO);

    if (camera_dev == NULL) {
        LOG_SIMPLE("camera device not found\r\n");
        return -1;
    }

    uint8_t camera_ctrl_pipe = CAMERA_CTRL_PIPE1_BIT | CAMERA_CTRL_PIPE2_BIT;
    // uint8_t camera_ctrl_pipe = CAMERA_CTRL_PIPE2_BIT;
    ret = device_ioctl(camera_dev, CAM_CMD_SET_PIPE_CTRL, &camera_ctrl_pipe, 0);//set pipe ctrl
    if (ret != 0) {
        LOG_SIMPLE("PIPE ctrl failed :%d\r\n", ret);
        return -1;
    }

    ret = nn_load_model(AI_DEFAULT_BASE); //test model
    if (ret != 0) {
        LOG_SIMPLE("nn load model failed :%d\r\n", ret);
        return -1;
    }

    nn_get_model_info(&model_info);

    device_ioctl(camera_dev, CAM_CMD_GET_PIPE2_PARAM, (uint8_t *)&pipe_param, sizeof(pipe_params_t));
    //if need to change pipe param
    pipe_param.width = model_info.input_width;
    pipe_param.height = model_info.input_height;
    pipe_param.fps = 30;
    pipe_param.bpp = 3;
    pipe_param.format = DCMIPP_PIXEL_PACKER_FORMAT_RGB888_YUV444_1;
    device_ioctl(camera_dev, CAM_CMD_SET_PIPE2_PARAM, (uint8_t *)&pipe_param, sizeof(pipe_params_t));
    ret = device_start(camera_dev);
    if (ret != 0) {
        LOG_SIMPLE("camera start failed :%d\r\n", ret);
        return -1;
    }

    task_stat = 1;
    nnCameraTask_id = osThreadNew(nn_camera_process, &task_stat, &nnCameraTask_attributes);
    if (nnCameraTask_id == NULL) {
        LOG_SIMPLE("nn camera task create failed\r\n");
        return -1;
    }

    return 0;
}

static int nn_camera_stop(void)
{
    int ret;
    device_t *camera_dev = device_find_pattern(CAMERA_DEVICE_NAME, DEV_TYPE_VIDEO);
    if (camera_dev == NULL) {
        LOG_SIMPLE("camera device not found\r\n");
        return -1;
    }

    if (nnCameraTask_id) {
        task_stat = 0;
        while (task_stat != 2) { osDelay(1);}
        // osDelay(100);
        ret = device_stop(camera_dev);
        if (ret != 0) {
            LOG_SIMPLE("camera stop failed :%d\r\n", ret);
        }
        osThreadTerminate(nnCameraTask_id);
        nnCameraTask_id = NULL;
    }   
    nn_unload_model();
    return 0;
}


/* ==================== JSON creation functions ==================== */
/**
 * @brief Create detection result JSON
 */
static cJSON* create_detection_json(const od_detect_t* detection, int index) {
    cJSON* detection_json = cJSON_CreateObject();
    if (!detection_json) return NULL;
    
    cJSON_AddNumberToObject(detection_json, "index", index);
    cJSON_AddStringToObject(detection_json, "class_name", detection->class_name);
    cJSON_AddNumberToObject(detection_json, "confidence", detection->conf);
    cJSON_AddNumberToObject(detection_json, "x", detection->x);
    cJSON_AddNumberToObject(detection_json, "y", detection->y);
    cJSON_AddNumberToObject(detection_json, "width", detection->width);
    cJSON_AddNumberToObject(detection_json, "height", detection->height);
    
    return detection_json;
}

/**
 * @brief Create keypoint JSON
 */
static cJSON* create_keypoint_json(const keypoint_t* keypoint, int index) {
    cJSON* keypoint_json = cJSON_CreateObject();
    if (!keypoint_json) return NULL;
    
    cJSON_AddNumberToObject(keypoint_json, "index", index);
    cJSON_AddNumberToObject(keypoint_json, "x", keypoint->x);
    cJSON_AddNumberToObject(keypoint_json, "y", keypoint->y);
    cJSON_AddNumberToObject(keypoint_json, "confidence", keypoint->conf);
    
    return keypoint_json;
}

/**
 * @brief Create MPE detection result JSON
 */
static cJSON* create_mpe_detection_json(const mpe_detect_t* detection, int index) {
    cJSON* detection_json = cJSON_CreateObject();
    if (!detection_json) return NULL;
    
    cJSON_AddNumberToObject(detection_json, "index", index);
    cJSON_AddStringToObject(detection_json, "class_name", detection->class_name ? detection->class_name : "person");
    cJSON_AddNumberToObject(detection_json, "confidence", detection->conf);
    cJSON_AddNumberToObject(detection_json, "x", detection->x);
    cJSON_AddNumberToObject(detection_json, "y", detection->y);
    cJSON_AddNumberToObject(detection_json, "width", detection->width);
    cJSON_AddNumberToObject(detection_json, "height", detection->height);
    
    // Add keypoints array
    cJSON* keypoints_array = cJSON_CreateArray();
    if (keypoints_array) {
        for (uint32_t i = 0; i < detection->nb_keypoints && i < 33; i++) {
            cJSON* keypoint_json = create_keypoint_json(&detection->keypoints[i], i);
            if (keypoint_json) {
                // Add keypoint name if available
                if (detection->keypoint_names && detection->keypoint_names[i]) {
                    cJSON_AddStringToObject(keypoint_json, "name", detection->keypoint_names[i]);
                }
                cJSON_AddItemToArray(keypoints_array, keypoint_json);
            }
        }
        cJSON_AddItemToObject(detection_json, "keypoints", keypoints_array);
    }
    cJSON_AddNumberToObject(detection_json, "keypoint_count", detection->nb_keypoints);
    
    // Add connections array if available
    if (detection->keypoint_connections && detection->num_connections > 0) {
        cJSON* connections_array = cJSON_CreateArray();
        if (connections_array) {
            for (uint8_t i = 0; i < detection->num_connections; i += 2) {
                cJSON* connection_json = cJSON_CreateObject();
                if (connection_json) {
                    cJSON_AddNumberToObject(connection_json, "from", detection->keypoint_connections[i]);
                    cJSON_AddNumberToObject(connection_json, "to", detection->keypoint_connections[i + 1]);
                    cJSON_AddItemToArray(connections_array, connection_json);
                }
            }
            cJSON_AddItemToObject(detection_json, "connections", connections_array);
        }
        cJSON_AddNumberToObject(detection_json, "connection_count", detection->num_connections / 2);
    }
    
    return detection_json;
}

/**
 * @brief Create AI result JSON
 */
cJSON* nn_create_ai_result_json(const nn_result_t* ai_result) {
    cJSON* result_json = cJSON_CreateObject();
    if (!result_json) return NULL;
    
    cJSON_AddNumberToObject(result_json, "type", ai_result->type);
    
    if (ai_result->type == PP_TYPE_OD && ai_result->od.nb_detect > 0) {
        // Object Detection results
        cJSON* detections_array = cJSON_CreateArray();
        if (detections_array) {
            for (int i = 0; i < ai_result->od.nb_detect; i++) {
                cJSON* detection_json = create_detection_json(&ai_result->od.detects[i], i);
                if (detection_json) {
                    cJSON_AddItemToArray(detections_array, detection_json);
                }
            }
            cJSON_AddItemToObject(result_json, "detections", detections_array);
        }
        cJSON_AddNumberToObject(result_json, "detection_count", ai_result->od.nb_detect);
        
        // Add empty pose results for consistency
        cJSON_AddItemToObject(result_json, "poses", cJSON_CreateArray());
        cJSON_AddNumberToObject(result_json, "pose_count", 0);
        
    } else if (ai_result->type == PP_TYPE_MPE && ai_result->mpe.nb_detect > 0) {
        // Multi-Pose Estimation results
        cJSON* poses_array = cJSON_CreateArray();
        if (poses_array) {
            for (int i = 0; i < ai_result->mpe.nb_detect; i++) {
                cJSON* pose_json = create_mpe_detection_json(&ai_result->mpe.detects[i], i);
                if (pose_json) {
                    cJSON_AddItemToArray(poses_array, pose_json);
                }
            }
            cJSON_AddItemToObject(result_json, "poses", poses_array);
        }
        cJSON_AddNumberToObject(result_json, "pose_count", ai_result->mpe.nb_detect);
        
        // Add empty detection results for consistency
        cJSON_AddItemToObject(result_json, "detections", cJSON_CreateArray());
        cJSON_AddNumberToObject(result_json, "detection_count", 0);
        
    } else {
        // No results or unsupported type
        cJSON_AddItemToObject(result_json, "detections", cJSON_CreateArray());
        cJSON_AddNumberToObject(result_json, "detection_count", 0);
        cJSON_AddItemToObject(result_json, "poses", cJSON_CreateArray());
        cJSON_AddNumberToObject(result_json, "pose_count", 0);
    }
    
    // Add type name for better frontend understanding
    const char* type_name = "unknown";
    switch (ai_result->type) {
        case PP_TYPE_OD:
            type_name = "object_detection";
            break;
        case PP_TYPE_MPE:
            type_name = "multi_pose_estimation";
            break;
        case PP_TYPE_SEG:
            type_name = "segmentation";
            break;
        case PP_TYPE_CLASS:
            type_name = "classification";
            break;
        case PP_TYPE_PD:
            type_name = "person_detection";
            break;
        case PP_TYPE_SPE:
            type_name = "single_pose_estimation";
            break;
        case PP_TYPE_ISEG:
            type_name = "instance_segmentation";
            break;
        case PP_TYPE_SSEG:
            type_name = "semantic_segmentation";
            break;
        default:
            type_name = "unknown";
            break;
    }
    cJSON_AddStringToObject(result_json, "type_name", type_name);
    
    return result_json;
}

/* ==================== command processing function ==================== */

static int nn_cmd(int argc, char *argv[])
{
    if (argc < 2) {
        LOG_SIMPLE("Usage: nn <command> [args...]\r\n");
        LOG_SIMPLE("Commands:\r\n");
        LOG_SIMPLE("  status          - Show NN status\r\n");
        LOG_SIMPLE("  load <path>     - Load model from path\r\n");
        LOG_SIMPLE("  unload          - Unload current model\r\n");
        LOG_SIMPLE("  start           - Start inference\r\n");
        LOG_SIMPLE("  stop            - Stop inference\r\n");
        LOG_SIMPLE("  stats           - Show inference statistics\r\n");
        LOG_SIMPLE("  validate        - Validate model file\r\n");
        LOG_SIMPLE("  camera          - sample : camera inference\r\n");
        return 0;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "status") == 0) {
        // show state
        nn_state_t state = nn_get_state();
        const char *state_str[] = {"UNINIT", "INIT", "READY", "RUNNING", "ERROR"};
        LOG_SIMPLE("NN Status: %s\r\n", state_str[state]);

        if (g_nn.model.name[0] != '\0') {
            LOG_SIMPLE("Current Model: %s (v%s)\r\n", g_nn.model.name, g_nn.model.version);
            LOG_SIMPLE("Model Description: %s\r\n", g_nn.model.description);
            LOG_SIMPLE("Model Author: %s\r\n", g_nn.model.author);
            LOG_SIMPLE("Model Created At: %s\r\n", g_nn.model.created_at);
            LOG_SIMPLE("Model Color Format: %s\r\n", g_nn.model.color_format);
            LOG_SIMPLE("Model Input Data Type: %s\r\n", g_nn.model.input_data_type);
            LOG_SIMPLE("Model Output Data Type: %s\r\n", g_nn.model.output_data_type);
            LOG_SIMPLE("Input: %lux%lux%lu\r\n",
                       g_nn.model.input_width, g_nn.model.input_height,
                       g_nn.model.input_channels);
        }

        LOG_SIMPLE("Inference Count: %ld, Total Time: %ld ms, Average Time: %ld ms\r\n",
                   g_nn.inference_count, g_nn.total_inference_time,
                   g_nn.inference_count > 0 ? g_nn.total_inference_time / g_nn.inference_count : 0);

    } else if (strcmp(cmd, "load") == 0) {
        // load model
        if (argc < 2) {
            LOG_SIMPLE("Error: Please specify model path\r\n");
            return -1;
        }
        uintptr_t model_ptr = strtoul(argv[2], NULL, 16);
        if (model_ptr < AI_DEFAULT_BASE || model_ptr > AI_3_END) {
            LOG_SIMPLE("Error: model path is not in [0x%lx, 0x%lx]\r\n", AI_DEFAULT_BASE, AI_3_END);
            return -1;
        }
        int ret = nn_load_model(model_ptr);
        if (ret == 0) {
            LOG_SIMPLE("Model loaded successfully: %s\r\n", argv[1]);
        } else {
            LOG_SIMPLE("Failed to load model: %d\r\n", ret);
        }

    } else if (strcmp(cmd, "unload") == 0) {
        // unload model
        int ret = nn_unload_model();
        if (ret == 0) {
            LOG_SIMPLE("Model unloaded successfully\r\n");
        } else {
            LOG_SIMPLE("Failed to unload model: %d\r\n", ret);
        }

    } else if (strcmp(cmd, "start") == 0) {
        // start inference
        int ret = nn_start_inference();
        if (ret == 0) {
            LOG_SIMPLE("Inference started\r\n");
        } else {
            LOG_SIMPLE("Failed to start inference: %d\r\n", ret);
        }

    } else if (strcmp(cmd, "stop") == 0) {
        // stop inference
        int ret = nn_stop_inference();
        if (ret == 0) {
            LOG_SIMPLE("Inference stopped\r\n");
        } else {
            LOG_SIMPLE("Failed to stop inference: %d\r\n", ret);
        }

    } else if (strcmp(cmd, "set") == 0) {
        // set configuration
        if (argc < 3) {
            LOG_SIMPLE("Error: Please specify key and value\r\n");
            return -1;
        }

        const char *key = argv[2];
        const char *value = argv[3];

        if (strcmp(key, "confidence") == 0) {
            nn_set_confidence_threshold(atof(value));
        } else if (strcmp(key, "nms") == 0) {
            nn_set_nms_threshold(atof(value));
        } else {
            LOG_SIMPLE("Unknown configuration key: %s\r\n", key);
            return -1;
        }

    } else if (strcmp(cmd, "stats") == 0) {
        // show statistics
        uint32_t count, total_time;
        if (nn_get_inference_stats(&count, &total_time) == 0) {
            LOG_SIMPLE("Inference Statistics:\r\n");
            LOG_SIMPLE("  Total Inferences: %lu\r\n", count);
            LOG_SIMPLE("  Total Time: %lu ms\r\n", total_time);
            if (count > 0) {
                LOG_SIMPLE("  Average Time: %.2f ms\r\n", (float)total_time / count);
            }
        }

    } else if (strcmp(cmd, "validate") == 0) {
        // validate model file
        if (argc < 3) {
            LOG_SIMPLE("Error: Please specify model path\r\n");
            return -1;
        }
        uintptr_t model_ptr = strtoul(argv[2], NULL, 16);
        int ret = validate_model(model_ptr);
        if (ret == 0) {
            LOG_SIMPLE("Model file is valid\r\n");
        } else {
            LOG_SIMPLE("Model file is invalid: %d\r\n", ret);
        }

    } else if (strcmp(cmd, "camera") == 0) {
        // camera inference
        if (argc < 3) {
            LOG_SIMPLE("Error: Please specify start or stop\r\n");
            return -1;
        }
        if (strcmp(argv[2], "start") == 0) {
            nn_camera_start();
        } else if (strcmp(argv[2], "stop") == 0) {
            nn_camera_stop();
        }
    }

    return 0;
}

// command registration table
debug_cmd_reg_t nn_cmd_table[] = {
    {"nn", "Neural Network control", nn_cmd},
};

// command registration function
static void nn_cmd_register(void)
{
    debug_cmdline_register(nn_cmd_table, sizeof(nn_cmd_table) / sizeof(nn_cmd_table[0]));
}

/* ==================== public API function implementation ==================== */

void nn_register(void)
{
    static dev_ops_t nn_ops = {
        .init = nn_init,
        .deinit = nn_deinit,
        .start = NULL,
        .stop = NULL,
        .ioctl = NULL
    };

    memset(&g_nn, 0, sizeof(g_nn));

    // create device instance
    device_t *dev = hal_mem_alloc_fast(sizeof(device_t));
    g_nn.dev = dev;

    // set device information
    strcpy(dev->name, "nn");
    dev->type = DEV_TYPE_AI;  // use AI device type
    dev->ops = &nn_ops;
    dev->priv_data = &g_nn;

    // register device
    device_register(g_nn.dev);

    // register command
    driver_cmd_register_callback("nn", nn_cmd_register);

    LOG_DRV_INFO("NN module registered successfully\r\r\n");
}

void nn_unregister(void)
{
    if (g_nn.dev) {
        device_unregister(g_nn.dev);
        hal_mem_free(g_nn.dev);
        g_nn.dev = NULL;
    }

    LOG_DRV_INFO("NN module unregistered\r\r\n");
}

int nn_load_model(const uintptr_t file_ptr)
{
    if (!g_nn.is_init) {
        return -1;
    }

    osMutexAcquire(g_nn.mtx_id, osWaitForever);
    int ret = load_model(file_ptr);
    osMutexRelease(g_nn.mtx_id);

    return ret;
}

int nn_unload_model(void)
{
    if (!g_nn.is_init) {
        return -1;
    }

    osMutexAcquire(g_nn.mtx_id, osWaitForever);
    int ret = unload_model();
    osMutexRelease(g_nn.mtx_id);

    return ret;
}

int nn_get_model_info(nn_model_info_t *info)
{
    if (!g_nn.is_init || !info) {
        return -1;
    }

    osMutexAcquire(g_nn.mtx_id, osWaitForever);
    memcpy(info, &g_nn.model, sizeof(nn_model_info_t));
    osMutexRelease(g_nn.mtx_id);

    return 0;
}

int nn_get_model_input_buffer(uint8_t **buffer, uint32_t *size)
{
    if (!g_nn.is_init || !buffer || !size) {
        return -1;
    }
    osMutexAcquire(g_nn.mtx_id, osWaitForever);
    *buffer = (uint8_t *)g_nn.input_buffer[0];
    *size = g_nn.input_buffer_size[0];
    osMutexRelease(g_nn.mtx_id);

    return 0;
}

int nn_start_inference(void)
{
    if (!g_nn.is_init) {
        return -1;
    }
    osMutexAcquire(g_nn.mtx_id, osWaitForever);
    int ret = nn_start(&g_nn);
    osMutexRelease(g_nn.mtx_id);
    return ret;
}

int nn_stop_inference(void)
{
    if (!g_nn.is_init) {
        return -1;
    }
    osMutexAcquire(g_nn.mtx_id, osWaitForever);
    int ret = nn_stop(&g_nn);
    osMutexRelease(g_nn.mtx_id);
    return ret;
}

int nn_inference_frame(uint8_t *input_data, uint32_t input_size, nn_result_t *result)
{
    if (!g_nn.is_init || !input_data || !result) {
        return -1;
    }

    osMutexAcquire(g_nn.mtx_id, osWaitForever);
    // process inference
    if (g_nn.input_buffer_size[0] != input_size) {
        LOG_DRV_ERROR("input_buffer_size[0] != input_size\r\r\n");
        osMutexRelease(g_nn.mtx_id);
        return -1;
    }
    memcpy(g_nn.input_buffer[0], input_data, input_size);
    int ret = 0;
    ret = model_run(&g_nn, result, false);
    osMutexRelease(g_nn.mtx_id);

    return ret;
}

int nn_set_confidence_threshold(float threshold)
{

    if (g_nn.state != NN_STATE_RUNNING && g_nn.state != NN_STATE_READY) {
        LOG_DRV_ERROR("NN not running or ready\r\r\n");
        return -1;
    }
    osMutexAcquire(g_nn.mtx_id, osWaitForever);
    if (g_nn.pp_vt && g_nn.pp_vt->set_confidence_threshold) {
        g_nn.pp_vt->set_confidence_threshold(g_nn.pp_params, threshold);
    }
    osMutexRelease(g_nn.mtx_id);

    return 0;
}

int nn_get_confidence_threshold(float *threshold)
{
    if (g_nn.state != NN_STATE_RUNNING && g_nn.state != NN_STATE_READY) {
        LOG_DRV_ERROR("NN not running or ready\r\r\n");
        return -1;
    }
    osMutexAcquire(g_nn.mtx_id, osWaitForever);
    if (g_nn.pp_vt && g_nn.pp_vt->get_confidence_threshold) {
        g_nn.pp_vt->get_confidence_threshold(g_nn.pp_params, threshold);
    }
    osMutexRelease(g_nn.mtx_id);

    return 0;
}

int nn_set_nms_threshold(float threshold)
{

    if (g_nn.state != NN_STATE_RUNNING && g_nn.state != NN_STATE_READY) {
        LOG_DRV_ERROR("NN not running or ready\r\r\n");
        return -1;
    }
    osMutexAcquire(g_nn.mtx_id, osWaitForever);
    if (g_nn.pp_vt && g_nn.pp_vt->set_nms_threshold) {
        g_nn.pp_vt->set_nms_threshold(g_nn.pp_params, threshold);
    }
    osMutexRelease(g_nn.mtx_id);

    return 0;

}

int nn_get_nms_threshold(float *threshold)
{
    if (g_nn.state != NN_STATE_RUNNING && g_nn.state != NN_STATE_READY) {
        LOG_DRV_ERROR("NN not running or ready\r\r\n");
        return -1;
    }
    osMutexAcquire(g_nn.mtx_id, osWaitForever);
    if (g_nn.pp_vt && g_nn.pp_vt->get_nms_threshold) {
        g_nn.pp_vt->get_nms_threshold(g_nn.pp_params, threshold);
    }
    osMutexRelease(g_nn.mtx_id);

    return 0;
}

nn_state_t nn_get_state(void)
{
    return g_nn.state;
}

int nn_get_inference_stats(uint32_t *count, uint32_t *total_time)
{
    if (!count || !total_time) {
        return -1;
    }

    *count = g_nn.inference_count;
    *total_time = g_nn.total_inference_time;

    return 0;
}


int nn_set_callback(nn_callback_t callback, void *user_data)
{
    g_nn.callback = callback;
    g_nn.callback_user_data = user_data;
    return 0;
}

int nn_update_model(const uintptr_t file_ptr)
{
    if (!file_ptr) {
        return -1;
    }

    LOG_DRV_INFO("Updating model to: 0x%lx\r\r\n", file_ptr);

    // validate new model
    if (validate_model(file_ptr) != 0) {
        LOG_DRV_ERROR("New model validation failed\r\r\n");
        return -1;
    }

    // stop current inference
    nn_state_t state = g_nn.state;
    if (state == NN_STATE_RUNNING) {
        nn_stop_inference();
    }

    // unload current model
    int ret = nn_unload_model();
    if (ret != 0) {
        LOG_DRV_ERROR("Failed to unload current model\r\r\n");
        return ret;
    }

    // load new model
    ret = nn_load_model(file_ptr);
    if (ret != 0) {
        LOG_DRV_ERROR("Failed to load new model\r\r\n");
        return ret;
    }

    // start inference
    if (state == NN_STATE_RUNNING) {
        nn_start_inference();
    }

    LOG_DRV_INFO("Model updated successfully\r\r\n");
    return 0;
}

int nn_validate_model(const uintptr_t file_ptr)
{
    return validate_model(file_ptr);
}
