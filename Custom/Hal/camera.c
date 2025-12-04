#include <assert.h>
#include "cmw_camera.h"
#include "camera.h"
#include "common_utils.h"
#include "debug.h"
#include "mem.h"
#include "stm32n6xx_hal.h"

// Constant definitions
#define CAMERA_TASK_DELAY_MS          100
#define CAMERA_BUFFER_TIMEOUT_MS      1000
#define CAMERA_MEMORY_ALIGNMENT       32
#define CAMERA_DEINIT_DELAY_MS        20
#define CAMERA_MAX_READY_BUFFERS      8

static int pipe_buffer_acquire(pipe_buffer_t *pipe_buffer, pipe_params_t *pipe_param, camera_dq_t *dq);
static int pipe_buffer_release(pipe_buffer_t *pipe_buffer, pipe_params_t *pipe_param, camera_dq_t *dq);

static camera_t g_camera = {0};
const osThreadAttr_t cameraTask_attributes = {
    .name = "cameraTask",
    .priority = (osPriority_t) osPriorityNormal,
    .stack_size = 4 * 1024
};

static int sensor_width;
static int sensor_height;

typedef enum {
    CAMERA_BUFF_INIT    =  0,
    CAMERA_BUFF_READY,
    CAMERA_BUFF_FREE,
} BUFF_STATE_E;

static const char *sensor_names[] = {
    "CMW_UNKNOWN",
    "CMW_VD66GY",
    "CMW_IMX335",
    "CMW_VD55G1",
    "CMW_OS04C10",
};


#ifdef ISP_MW_TUNING_TOOL_SUPPORT
#include "isp_api.h"
#include "isp_param_conf.h"

extern CMW_Sensor_if_t Camera_Drv;
extern DCMIPP_HandleTypeDef hcamera_dcmipp;
ISP_HandleTypeDef hIsp;
static uint8_t isp_is_init = 0, isp_is_start = 0;

/**
* @brief  Get the sensor info
* @param  camera_instance: camera instance
* @param  info: sensor info
* @retval Operation result
*/
ISP_StatusTypeDef GetSensorInfo(uint32_t camera_instance, ISP_SensorInfoTypeDef *info)
{
UNUSED(camera_instance);

if (CMW_CAMERA_GetSensorInfo(info) != CMW_ERROR_NONE)
    return ISP_ERR_SENSORINFO;

return ISP_OK;
}

/**
* @brief  Set the sensor gain
* @param  camera_instance: camera instance
* @param  gain: sensor gain to be applied
* @retval Operation result
*/
ISP_StatusTypeDef SetSensorGain(uint32_t camera_instance, int32_t gain)
{
UNUSED(camera_instance);

if (CMW_CAMERA_SetGain(gain) != CMW_ERROR_NONE)
    return ISP_ERR_SENSORGAIN;

return ISP_OK;
}

/**
* @brief  Get the sensor gain
* @param  camera_instance: camera instance
* @param  gain: current sensor gain
* @retval Operation result
*/
ISP_StatusTypeDef GetSensorGain(uint32_t camera_instance, int32_t *gain)
{
UNUSED(camera_instance);

if (CMW_CAMERA_GetGain(gain) != CMW_ERROR_NONE)
    return ISP_ERR_SENSORGAIN;

return ISP_OK;
}

/**
* @brief  Set the sensor exposure
* @param  Instance: camera instance
* @param  Gain: sensor exposure to be applied
* @retval Operation result
*/
ISP_StatusTypeDef SetSensorExposure(uint32_t camera_instance, int32_t exposure)
{
UNUSED(camera_instance);

if (CMW_CAMERA_SetExposure(exposure) != CMW_ERROR_NONE)
    return ISP_ERR_SENSOREXPOSURE;

return ISP_OK;
}

/**
* @brief  Get the sensor exposure
* @param  camera_instance: camera instance
* @param  gain: current sensor gain
* @retval Operation result
*/
ISP_StatusTypeDef GetSensorExposure(uint32_t camera_instance, int32_t *exposure)
{
UNUSED(camera_instance);

if (CMW_CAMERA_GetExposure(exposure) != CMW_ERROR_NONE)
    return ISP_ERR_SENSOREXPOSURE;

return ISP_OK;
}

/**
  * @brief  Set the sensor test pattern
  * @param  camera_instance: camera instance
  * @param  mode: sensor test pattern to be applied
  * @retval Operation result
  */
ISP_StatusTypeDef SetSensorTestPattern(uint32_t camera_instance, int32_t mode)
{
  UNUSED(camera_instance);

  if (CMW_CAMERA_SetTestPattern(mode) != CMW_ERROR_NONE)
    return ISP_ERR_EINVAL;

  return ISP_OK;
}

/**
* @brief  Helper for ISP to start camera preview
* @param  hDcmipp Pointer to the dcmipp device
* @retval ISP status operation
*/
ISP_StatusTypeDef Camera_StartPreview(void *pDcmipp)
{
    UNUSED(pDcmipp);
    // printf("Camera_StartPreview\r\n");
    return ISP_OK;
}   

/**
* @brief  Helper for ISP to stop camera preview
* @param  hDcmipp Pointer to the dcmipp device
* @retval ISP status operation
*/
ISP_StatusTypeDef Camera_StopPreview(void *pDcmipp)
{
    UNUSED(pDcmipp);
    // printf("Camera_StopPreview\r\n");
    return ISP_OK;
}

static uint8_t *isp_tool_buf = NULL;

/**
* @brief  Helper for ISP to dump a frame
* @param  hDcmipp Pointer to the dcmipp device
* @param  Pipe    Pipe where to perform the dump ('DUMP'(0) or 'ANCILLARY'(2))
* @param  Config  Dump with the current pipe config, or without downsizing with
*                 a specific pixel format.
* @param  pBuffer Pointer to the address of the dumped buffer (output parameter)
* @param  pMeta   Pointer to buffer meta data (output parameter)
* @retval ISP status operation
*/
ISP_StatusTypeDef Camera_DumpFrame(void *pDcmipp, uint32_t Pipe, ISP_DumpCfgTypeDef Config,
uint32_t **pBuffer, ISP_DumpFrameMetaTypeDef *pMeta)
{
    int ret = 0, try_times = 0, isp_tool_buf_index = 0;
    uint8_t *fb_buffer = NULL;
    DCMIPP_HandleTypeDef *pHdcmipp = (DCMIPP_HandleTypeDef*)pDcmipp;
    /* Check handle validity */
    if ((pHdcmipp == NULL) || (pBuffer == NULL) || (pMeta == NULL))
    {
        return ISP_ERR_EINVAL;
    }

    // printf("Camera_DumpFrame\r\n");
    // printf("Pipe: %d\r\n", Pipe);
    // printf("Config: %d\r\n", Config);

    if (isp_tool_buf == NULL) {
        isp_tool_buf = hal_mem_alloc_aligned(PIPE1_DEFAULT_WIDTH * PIPE1_DEFAULT_HEIGHT * PIPE1_DEFAULT_BPP, CAMERA_MEMORY_ALIGNMENT, MEM_LARGE);
        if (isp_tool_buf == NULL) {
            return ISP_ERR_DCMIPP_NOMEM;
        }
    }
    
    if (g_camera.dev == NULL) {
        return ISP_ERR_STAT_EINVAL;
    }

    if (g_camera.state.pipe1_state != PIPE_START) {
        ret = device_ioctl(g_camera.dev, CAM_CMD_SET_PIPE1_START, NULL, 0);
        if (ret != AICAM_OK) {
            return ISP_ERR_DCMIPP_STATE;
        }
    }

    do {
        ret = device_ioctl(g_camera.dev, CAM_CMD_GET_PIPE1_BUFFER, (uint8_t *)&fb_buffer, 0);
        if (ret <= 0) {
            if (++try_times > 10) {
                return ISP_ERR_DCMIPP_FRAMESIZE;
            }
            osDelay(1);
        }
    } while (ret <= 0);

    // printf("ret: %d\r\n", ret);
    if (ret > PIPE1_DEFAULT_WIDTH * PIPE1_DEFAULT_HEIGHT * PIPE1_DEFAULT_BPP) {
        device_ioctl(g_camera.dev, CAM_CMD_RETURN_PIPE1_BUFFER, fb_buffer, 0);
        return ISP_ERR_DCMIPP_FRAMESIZE;
    }

    if (PIPE1_DEFAULT_BPP == 4) {
        // ARGB8888 to RGB888
        for (int i = 0; i < ret; i += 4) {
            isp_tool_buf[isp_tool_buf_index++] = fb_buffer[i];
            isp_tool_buf[isp_tool_buf_index++] = fb_buffer[i + 1];
            isp_tool_buf[isp_tool_buf_index++] = fb_buffer[i + 2];
        }
    } else {
        memcpy(isp_tool_buf, fb_buffer, ret);
    }
    device_ioctl(g_camera.dev, CAM_CMD_RETURN_PIPE1_BUFFER, fb_buffer, 0);
    *pBuffer = (uint32_t *)isp_tool_buf;
    pMeta->width = g_camera.pipe1_param.width;
    pMeta->height = g_camera.pipe1_param.height;
    pMeta->pitch = g_camera.pipe1_param.width * 3;
    pMeta->size = pMeta->height * pMeta->pitch;
    pMeta->format = ISP_FORMAT_RGB888;
    
    // printf("Dumped frame meta: width=%d, height=%d, pitch=%d, size=%d, format=%d\r\n",
        // pMeta->width, pMeta->height, pMeta->pitch, pMeta->size, pMeta->format);
    return ISP_OK;
}

ISP_AppliHelpersTypeDef appliHelpers = {
    .GetSensorInfo = GetSensorInfo,
    .SetSensorGain = SetSensorGain,
    .GetSensorGain = GetSensorGain,
    .SetSensorExposure = SetSensorExposure,
    .GetSensorExposure = GetSensorExposure,
    .StartPreview = Camera_StartPreview,
    .StopPreview = Camera_StopPreview,
    .DumpFrame = Camera_DumpFrame,
    .SetSensorTestPattern = SetSensorTestPattern,
};
#endif

void buffer_reset(pipe_buffer_t *bufs, int nb, camera_dq_t *dq)
{
    uint32_t idx;
    for (int i = 0; i < nb; ++i) {
        bufs[i].state = BUFFER_IDLE;
        bufs[i].frame_id = 0;
    }
    while (osMessageQueueGet(dq->ready_queue, &idx, NULL, 0) == osOK);
    if (dq->idle_sem) {
        osSemaphoreDelete(dq->idle_sem);
    }
    dq->idle_sem = osSemaphoreNew(nb, nb, NULL);
}

pipe_buffer_t* buffer_acquire(pipe_buffer_t *bufs, int nb, camera_dq_t *dq)
{
    uint32_t idx = 0xFFFFFFFF;
    if (osSemaphoreAcquire(dq->idle_sem, 0) == osOK) {
        for (int i = 0; i < nb; i++) {
            if (bufs[i].state == BUFFER_IDLE) {
                bufs[i].state = BUFFER_PROCESSING;
                return &bufs[i];
            }
        }
    }
    if (osMessageQueueGet(dq->ready_queue, &idx, NULL, 0) == osOK) {
        if (idx < nb && bufs[idx].state == BUFFER_READY) {
            bufs[idx].state = BUFFER_PROCESSING;
            return &bufs[idx];
        }
    }
    return NULL;
}

pipe_buffer_t* find_processing_buffer(pipe_buffer_t *bufs, int nb)
{
    for (int i = 0; i < nb; ++i) {
        if (bufs[i].state == BUFFER_PROCESSING) {
            return &bufs[i];
        }
    }
    return NULL;
}

void buffer_set_ready_isr(pipe_buffer_t *bufs, camera_dq_t *dq, pipe_buffer_t* buf, uint32_t frame_id)
{
    buf->state = BUFFER_READY;
    buf->frame_id = frame_id;
    uint32_t idx = buf - bufs;
    osMessageQueuePut(dq->ready_queue, &idx, 0, 0);
}

void buffer_release_isr(pipe_buffer_t *buf, camera_dq_t *dq)
{
    buf->state = BUFFER_IDLE;
    osSemaphoreRelease(dq->idle_sem);
}

pipe_buffer_t* buffer_get_latest_ready(pipe_buffer_t *bufs, int nb, camera_dq_t *dq)
{
    uint32_t idxs[CAMERA_MAX_READY_BUFFERS];
    uint32_t count = 0;
    pipe_buffer_t* latest = NULL;
    uint32_t max_frame_id = 0;

    while (osMessageQueueGet(dq->ready_queue, &idxs[count], NULL, 0) == osOK) {
        uint32_t idx = idxs[count];
        if (idx < nb && bufs[idx].state == BUFFER_READY) {
            if (latest == NULL || bufs[idx].frame_id > max_frame_id) {
                latest = &bufs[idx];
                max_frame_id = bufs[idx].frame_id;
            }
        }
        count++;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (latest && (&bufs[idxs[i]] == latest))
            continue;
        // for old buffers, release them
        buffer_release_isr(&bufs[idxs[i]], dq);
    }

    if (latest) {
        latest->state = BUFFER_IN_USE;
    }
    return latest;
}


static void CAM_setSensorInfo(CMW_Sensor_Name_t sensor, camera_t *camera)
{
    switch (sensor) {
        case CMW_VD66GY_Sensor:
            camera->sensor_param.width = SENSOR_VD66GY_WIDTH;
            camera->sensor_param.height = SENSOR_VD66GY_HEIGHT;
            camera->sensor_param.mirror_flip = SENSOR_VD66GY_FLIP;
            camera->sensor_param.name = sensor_names[1];
            camera->sensor_param.fps = CAMERA_FPS;
            break;
        case CMW_IMX335_Sensor:
            camera->sensor_param.width = SENSOR_IMX335_WIDTH;
            camera->sensor_param.height = SENSOR_IMX335_HEIGHT;
            camera->sensor_param.mirror_flip = SENSOR_IMX335_FLIP;
            camera->sensor_param.name = sensor_names[2];
            camera->sensor_param.fps = CAMERA_FPS;
            break;
        case CMW_VD55G1_Sensor:
            camera->sensor_param.width = SENSOR_VD55G1_WIDTH;
            camera->sensor_param.height = SENSOR_VD55G1_HEIGHT;
            camera->sensor_param.mirror_flip = SENSOR_VD55G1_FLIP;
            camera->sensor_param.name = sensor_names[3];
            camera->sensor_param.fps = CAMERA_FPS;
            break;
        case CMW_OS04C10_Sensor:
            camera->sensor_param.width = SENSOR_OS04C10_WIDTH;
            camera->sensor_param.height = SENSOR_OS04C10_HEIGHT;
            camera->sensor_param.mirror_flip = SENSOR_OS04C10_FLIP;
            camera->sensor_param.name = sensor_names[4];
            camera->sensor_param.fps = CAMERA_FPS;
        // #ifndef ISP_MW_TUNING_TOOL_SUPPORT
            camera->sensor_param.aec = 1;
        // #endif
            break;
        default:
            break;
            
    }
    LOG_DRV_DEBUG("Detected %s \r\n", camera->sensor_param.name);
    LOG_DRV_DEBUG("Sensor Image: %dx%d, MirrorFlip: %d ",
    camera->sensor_param.width, camera->sensor_param.height, camera->sensor_param.mirror_flip);
}

/* Keep display output aspect ratio using crop area */
static void CAM_InitCropConfig(CMW_Manual_roi_area_t *roi, camera_t *camera)
{
    const float ratiox = (float)camera->sensor_param.width / camera->pipe1_param.width;
    const float ratioy = (float)camera->sensor_param.height / camera->pipe1_param.height;
    const float ratio = MIN(ratiox, ratioy);

    roi->width = (uint32_t) MIN(camera->pipe1_param.width * ratio, camera->sensor_param.width);
    roi->height = (uint32_t) MIN(camera->pipe1_param.height * ratio, camera->sensor_param.height);
    roi->offset_x = (camera->sensor_param.width - roi->width + 1) / 2;
    roi->offset_y = (camera->sensor_param.height - roi->height + 1) / 2;
}

HAL_StatusTypeDef MX_DCMIPP_ClockConfig(DCMIPP_HandleTypeDef *hdcmipp)
{
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
    HAL_StatusTypeDef ret;

    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_DCMIPP|RCC_PERIPHCLK_CSI;
    PeriphClkInitStruct.DcmippClockSelection = RCC_DCMIPPCLKSOURCE_IC17;
#if CPU_CLK_USE_400MHZ
    PeriphClkInitStruct.ICSelection[RCC_IC17].ClockSelection = RCC_ICCLKSOURCE_PLL3;
    PeriphClkInitStruct.ICSelection[RCC_IC17].ClockDivider = 1;
    PeriphClkInitStruct.ICSelection[RCC_IC18].ClockSelection = RCC_ICCLKSOURCE_PLL3;
    PeriphClkInitStruct.ICSelection[RCC_IC18].ClockDivider = 15;
#elif CPU_CLK_USE_200MHZ
    PeriphClkInitStruct.ICSelection[RCC_IC17].ClockSelection = RCC_ICCLKSOURCE_PLL3;
    PeriphClkInitStruct.ICSelection[RCC_IC17].ClockDivider = 1;
    PeriphClkInitStruct.ICSelection[RCC_IC18].ClockSelection = RCC_ICCLKSOURCE_PLL3;
    PeriphClkInitStruct.ICSelection[RCC_IC18].ClockDivider = 15;
#elif CPU_CLK_USE_HSI_800MHZ
    PeriphClkInitStruct.ICSelection[RCC_IC17].ClockSelection = RCC_ICCLKSOURCE_PLL3;
    PeriphClkInitStruct.ICSelection[RCC_IC17].ClockDivider = 3;
    PeriphClkInitStruct.ICSelection[RCC_IC18].ClockSelection = RCC_ICCLKSOURCE_PLL3;
    PeriphClkInitStruct.ICSelection[RCC_IC18].ClockDivider = 45;
#else // CPU_CLK_USE_800MHZ
    PeriphClkInitStruct.ICSelection[RCC_IC17].ClockSelection = RCC_ICCLKSOURCE_PLL3;
    PeriphClkInitStruct.ICSelection[RCC_IC17].ClockDivider = 3;
    PeriphClkInitStruct.ICSelection[RCC_IC18].ClockSelection = RCC_ICCLKSOURCE_PLL3;
    PeriphClkInitStruct.ICSelection[RCC_IC18].ClockDivider = 45;
#endif
    ret = HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct);
    if (ret)
        return ret;

    __HAL_RCC_DCMIPP_CLK_ENABLE();
    __HAL_RCC_CSI_CLK_ENABLE();
    __HAL_RCC_CSI_FORCE_RESET();
    __HAL_RCC_CSI_RELEASE_RESET();

    return HAL_OK;
}

static int DCMIPP_Pipe1Init(camera_t *camera)
{
    CMW_DCMIPP_Conf_t dcmipp_conf;
    uint32_t hw_pitch;
    int ret;

    dcmipp_conf.output_width = camera->pipe1_param.width;
    dcmipp_conf.output_height = camera->pipe1_param.height;
    dcmipp_conf.output_format = camera->pipe1_param.format;
    dcmipp_conf.output_bpp = camera->pipe1_param.bpp;
    dcmipp_conf.mode = CMW_Aspect_ratio_manual_roi;
    dcmipp_conf.enable_swap = 0;
    dcmipp_conf.enable_gamma_conversion = 0;
    CAM_InitCropConfig(&dcmipp_conf.manual_conf, camera);
    ret = CMW_CAMERA_SetPipeConfig(DCMIPP_PIPE1, &dcmipp_conf, &hw_pitch);
    if(ret != HAL_OK || hw_pitch != dcmipp_conf.output_width * dcmipp_conf.output_bpp){
        return CMW_ERROR_WRONG_PARAM;
    }
    return CMW_ERROR_NONE;
}

static int DCMIPP_Pipe2Init(camera_t *camera)
{
    CMW_DCMIPP_Conf_t dcmipp_conf;
    uint32_t hw_pitch;
    int ret;

    dcmipp_conf.output_width = camera->pipe2_param.width;
    dcmipp_conf.output_height = camera->pipe2_param.height;
    dcmipp_conf.output_format = camera->pipe2_param.format;
    dcmipp_conf.output_bpp = camera->pipe2_param.bpp;
    dcmipp_conf.mode = CMW_Aspect_ratio_manual_roi;
    dcmipp_conf.enable_swap = 1;
    dcmipp_conf.enable_gamma_conversion = 0;
    CAM_InitCropConfig(&dcmipp_conf.manual_conf, camera);
    ret = CMW_CAMERA_SetPipeConfig(DCMIPP_PIPE2, &dcmipp_conf, &hw_pitch);
    if(ret != HAL_OK || hw_pitch != dcmipp_conf.output_width * dcmipp_conf.output_bpp){
        return CMW_ERROR_WRONG_PARAM;
    }
    return CMW_ERROR_NONE;
}

static int DCMIPP_IpPlugInit(DCMIPP_HandleTypeDef *hdcmipp)
{
    DCMIPP_IPPlugConfTypeDef ipplug_conf = { 0 };
    int ret;

    ipplug_conf.MemoryPageSize = DCMIPP_MEMORY_PAGE_SIZE_256BYTES;

    ipplug_conf.Client = DCMIPP_CLIENT2; /* aux pipe */
    ipplug_conf.Traffic = DCMIPP_TRAFFIC_BURST_SIZE_128BYTES;
    ipplug_conf.MaxOutstandingTransactions = DCMIPP_OUTSTANDING_TRANSACTION_NONE;
    ipplug_conf.DPREGStart = 0;
    ipplug_conf.DPREGEnd = 559; /* (4480 bytes / one line) */
    ipplug_conf.WLRURatio = 15; /* 16 parts of BW */
    ret = HAL_DCMIPP_SetIPPlugConfig(hdcmipp, &ipplug_conf);
    if(ret != HAL_OK){
        return CMW_ERROR_WRONG_PARAM;
    }

    ipplug_conf.Client = DCMIPP_CLIENT5; /* main rgb pipe */
    ipplug_conf.Traffic = DCMIPP_TRAFFIC_BURST_SIZE_128BYTES;
    ipplug_conf.MaxOutstandingTransactions = DCMIPP_OUTSTANDING_TRANSACTION_3;
    ipplug_conf.DPREGStart = 560;
    ipplug_conf.DPREGEnd = 639;
    ipplug_conf.WLRURatio = 0; /* 1 parts of BW */
    ret = HAL_DCMIPP_SetIPPlugConfig(hdcmipp, &ipplug_conf);
    if(ret != HAL_OK){
        return CMW_ERROR_WRONG_PARAM;
    }
    return CMW_ERROR_NONE;
}

static int DCMIPP_ReduceSpurious(DCMIPP_HandleTypeDef *hdcmipp)
{
    int ret;

    ret = HAL_DCMIPP_PIPE_EnableLineEvent(hdcmipp, DCMIPP_PIPE1, DCMIPP_MULTILINE_128_LINES);
    if(ret != HAL_OK){
        return CMW_ERROR_WRONG_PARAM;
    }
    ret = HAL_DCMIPP_PIPE_DisableLineEvent(hdcmipp, DCMIPP_PIPE1);
    if(ret != HAL_OK){
        return CMW_ERROR_WRONG_PARAM;
    }
    return CMW_ERROR_NONE;
}

int CAM_Init(camera_t *camera)
{
    CMW_CameraInit_t cam_conf;
    CMW_Sensor_Name_t sensor;
    int ret;
    ret = CMW_CAMERA_Init(&cam_conf);
    if(ret != CMW_ERROR_NONE){
        return ret;
    }
    sensor_width = cam_conf.width;
    sensor_height = cam_conf.height;

    ret = CMW_CAMERA_GetSensorName(&sensor);
    if(ret != CMW_ERROR_NONE){
        return ret;
    }
    CAM_setSensorInfo(sensor, camera);

    CMW_CAMERA_SetMirrorFlip(camera->sensor_param.mirror_flip);
    DCMIPP_IpPlugInit(CMW_CAMERA_GetDCMIPPHandle());
    DCMIPP_Pipe1Init(camera);
    DCMIPP_Pipe2Init(camera);
    DCMIPP_ReduceSpurious(CMW_CAMERA_GetDCMIPPHandle());
    return CMW_ERROR_NONE;
}

static void main_pipe_frame_event()
{
    int ret;
    pipe_buffer_t *buffer,*buffer1;
    buffer1 = find_processing_buffer(g_camera.pipe1_buffer, g_camera.pipe1_param.buffer_nb);
    if(buffer1 != NULL){
        buffer_set_ready_isr(g_camera.pipe1_buffer, &g_camera.pipe1_dq, buffer1, g_camera.current_frame_id);
    }

    buffer = buffer_acquire(g_camera.pipe1_buffer, g_camera.pipe1_param.buffer_nb, &g_camera.pipe1_dq);
    if(buffer != NULL){
        ret = HAL_DCMIPP_PIPE_SetMemoryAddress(CMW_CAMERA_GetDCMIPPHandle(), DCMIPP_PIPE1,
                                                DCMIPP_MEMORY_ADDRESS_0, (uint32_t) buffer->data);
        if(ret == HAL_OK){
            osSemaphoreRelease(g_camera.sem_pipe1);
        }else{
            buffer_release_isr(buffer, &g_camera.pipe1_dq);
        }
    }else{
        ret = HAL_DCMIPP_PIPE_SetMemoryAddress(CMW_CAMERA_GetDCMIPPHandle(), DCMIPP_PIPE1,
                                                DCMIPP_MEMORY_ADDRESS_0, (uint32_t) buffer1->data);
        if(ret == HAL_OK){
            osSemaphoreRelease(g_camera.sem_pipe1);
        }
    }
}

static void ancillary_pipe_frame_event()
{
    pipe_buffer_t *buffer,*buffer1;
    int ret;

    // Use the same frame_id as PIPE1 to keep synchronization
    buffer1 = find_processing_buffer(g_camera.pipe2_buffer, g_camera.pipe2_param.buffer_nb);
    if(buffer1 != NULL){
        buffer_set_ready_isr(g_camera.pipe2_buffer, &g_camera.pipe2_dq, buffer1, g_camera.current_frame_id);
    }

    buffer = buffer_acquire(g_camera.pipe2_buffer, g_camera.pipe2_param.buffer_nb, &g_camera.pipe2_dq);
    if(buffer != NULL){
        ret = HAL_DCMIPP_PIPE_SetMemoryAddress(CMW_CAMERA_GetDCMIPPHandle(), DCMIPP_PIPE2,
                                                DCMIPP_MEMORY_ADDRESS_0, (uint32_t) buffer->data);
        if(ret == HAL_OK){
            osSemaphoreRelease(g_camera.sem_pipe2);
        }else{
            buffer_release_isr(buffer, &g_camera.pipe2_dq);
        }
    }else{
        ret = HAL_DCMIPP_PIPE_SetMemoryAddress(CMW_CAMERA_GetDCMIPPHandle(), DCMIPP_PIPE2,
                                                DCMIPP_MEMORY_ADDRESS_0, (uint32_t) buffer1->data);
        if(ret == HAL_OK){
            osSemaphoreRelease(g_camera.sem_pipe2);
        }
    }
}

static void app_main_pipe_vsync_event()
{
    osSemaphoreRelease(g_camera.sem_isp);
}

int CMW_CAMERA_PIPE_FrameEventCallback(uint32_t pipe)
{
    if (pipe == DCMIPP_PIPE1)
        main_pipe_frame_event();
    else if (pipe == DCMIPP_PIPE2){
        ancillary_pipe_frame_event();
    }

    return HAL_OK;
}

int CMW_CAMERA_PIPE_VsyncEventCallback(uint32_t pipe)
{
  if (pipe == DCMIPP_PIPE1) {
    g_camera.current_frame_id++;
    app_main_pipe_vsync_event();
#ifdef ISP_MW_TUNING_TOOL_SUPPORT
    ISP_IncMainFrameId(&hIsp);
    ISP_GatherStatistics(&hIsp);
    ISP_OutputMeta(&hIsp);
#endif
  }
    
  return HAL_OK;
}

static int camera_SetBrightness(int val)
{
// #ifdef ISP_MW_TUNING_TOOL_SUPPORT
//     (void) val;
// #else
    int exposure, gain;
    int ret;

    // printf("Set Brightness: %d\r\n", val);
    if (val <= 0) {
        val = 0;
    }

    if (val >= 100) {
        val = 100;
    }

    if (val <= 50) {
        exposure = EXPOSURE_MIN + (EXPOSURE_MAX - EXPOSURE_MIN) * val / 50;
        gain = GAIN_MIN;
    } else {
        exposure = EXPOSURE_MAX;
        gain = GAIN_MIN + (GAIN_MAX - GAIN_MIN) * (val - 50) / 50;
    }
    ret = CMW_CAMERA_SetExposure(exposure);
    if(ret != CMW_ERROR_NONE){
        return ret;
    }
    ret = CMW_CAMERA_SetGain(gain);
    if(ret != CMW_ERROR_NONE){
        return ret;
    }
// #endif
    return CMW_ERROR_NONE;
}

static int camera_sensor_set(sensor_params_t *sensor_param)
{
// #ifndef ISP_MW_TUNING_TOOL_SUPPORT
    int ret;

    // printf("AEC: %d, Brightness: %d, Contrast: %d\r\n", sensor_param->aec, sensor_param->brightness, sensor_param->contrast);
    if(sensor_param->aec == 0){
        ret = CMW_CAMERA_SetAEC(sensor_param->aec);
        if(ret != CMW_ERROR_NONE){
            return ret;
        }
        ret = camera_SetBrightness(sensor_param->brightness);
        if(ret != CMW_ERROR_NONE){
            return ret;
        } 
    }

    if(sensor_param->contrast != 0){
        ret = CMW_CAMERA_SetContrast(sensor_param->contrast);
        if(ret != CMW_ERROR_NONE){
            return ret;
        }
    }
// #endif

    return CMW_ERROR_NONE;
}
static int pipe_start_common(camera_t *camera, uint32_t pipe_id, pipe_buffer_t **pipe_buffer, 
                            pipe_params_t *pipe_param, camera_dq_t *dq, PIPE_STATE_E *pipe_state)
{
    LOG_DRV_DEBUG("camera pipe%lu start", pipe_id);
    int32_t ret = CMW_ERROR_NONE;
    pipe_buffer_t *buffer;
    
    if(!camera->is_init)
        return AICAM_ERROR_NOT_FOUND;
        
    if(*pipe_state == PIPE_STOP){
        *pipe_buffer = hal_mem_alloc_fast(sizeof(pipe_buffer_t) * pipe_param->buffer_nb);
        if(*pipe_buffer == NULL){
            return AICAM_ERROR_NO_MEMORY;
        }
        
        ret = pipe_buffer_acquire(*pipe_buffer, pipe_param, dq);
        if(ret != 0){
            LOG_DRV_ERROR("pipe%lu buffer acquire failed \r\n", pipe_id);
            pipe_buffer_release(*pipe_buffer, pipe_param, dq);
            hal_mem_free(*pipe_buffer);
            *pipe_buffer = NULL;
            return AICAM_ERROR_NO_MEMORY;
        }
        
        buffer = buffer_acquire(*pipe_buffer, pipe_param->buffer_nb, dq);
        if(buffer != NULL){
#ifdef ISP_MW_TUNING_TOOL_SUPPORT
            if(!isp_is_init){
                (void) ISP_IQParamCacheInit; /* unused */
                ret = ISP_Init(&hIsp, &hcamera_dcmipp, 0, &appliHelpers, &ISP_IQParamCacheInit_OS04C10);
                if (ret) LOG_DRV_ERROR("ISP_Init error: %d\r\n", ret);
                isp_is_init = 1;
            }
            if(!isp_is_start){
                ret = ISP_Start(&hIsp);
                if (ret) LOG_DRV_ERROR("ISP start failed: %d\r\n", ret);
                isp_is_start = 1;
            }
#endif
            ret = CMW_CAMERA_Start(pipe_id, buffer->data, CMW_MODE_CONTINUOUS);
            if(ret == CMW_ERROR_NONE){
                *pipe_state = PIPE_START;
                return AICAM_OK;
            }else{
                // Clean up resources on startup failure
                buffer_release_isr(buffer, dq);
                pipe_buffer_release(*pipe_buffer, pipe_param, dq);
                hal_mem_free(*pipe_buffer);
                *pipe_buffer = NULL;
                LOG_DRV_ERROR("pipe%lu start failed: %d\r\n", pipe_id, ret);
                return AICAM_ERROR;
            }
        }else{
            // Clean up resources when buffer cannot be acquired
            pipe_buffer_release(*pipe_buffer, pipe_param, dq);
            hal_mem_free(*pipe_buffer);
            *pipe_buffer = NULL;
            return AICAM_ERROR_BUSY;
        }
    }
    LOG_DRV_DEBUG("pipe%lu already start \r\n", pipe_id);
    return AICAM_OK;
}

static int pipe1_start(camera_t *camera)
{
    return pipe_start_common(camera, DCMIPP_PIPE1, &camera->pipe1_buffer, 
                           &camera->pipe1_param, &camera->pipe1_dq, &camera->state.pipe1_state);
}

// Common pipe stop function
static int pipe_stop_common(camera_t *camera, uint32_t pipe_id, pipe_buffer_t **pipe_buffer, 
                           pipe_params_t *pipe_param, camera_dq_t *dq, PIPE_STATE_E *pipe_state)
{
    LOG_DRV_DEBUG("camera pipe%lu stop", pipe_id);
    int32_t ret = HAL_OK;
    
    if(!camera->is_init)
        return AICAM_ERROR_NOT_FOUND;
        
    if(*pipe_state == PIPE_START){
        ret = HAL_DCMIPP_CSI_PIPE_Stop(CMW_CAMERA_GetDCMIPPHandle(), pipe_id, DCMIPP_VIRTUAL_CHANNEL0);
        if(ret == HAL_OK){
            buffer_reset(*pipe_buffer, pipe_param->buffer_nb, dq);
            *pipe_state = PIPE_STOP;
            pipe_buffer_release(*pipe_buffer, pipe_param, dq);
            hal_mem_free(*pipe_buffer);
            *pipe_buffer = NULL;
            return AICAM_OK;
        }else{
            LOG_DRV_ERROR("pipe%lu stop failed: %d\r\n", pipe_id, ret);
            return AICAM_ERROR;
        }
    }
    LOG_DRV_DEBUG("pipe%lu already stop \r\n", pipe_id);
    return AICAM_OK;
}

static int pipe1_stop(camera_t *camera)
{
    return pipe_stop_common(camera, DCMIPP_PIPE1, &camera->pipe1_buffer, 
                          &camera->pipe1_param, &camera->pipe1_dq, &camera->state.pipe1_state);
}

static int pipe2_start(camera_t *camera)
{
    return pipe_start_common(camera, DCMIPP_PIPE2, &camera->pipe2_buffer, 
                           &camera->pipe2_param, &camera->pipe2_dq, &camera->state.pipe2_state);
}

static int pipe2_stop(camera_t *camera)
{
    return pipe_stop_common(camera, DCMIPP_PIPE2, &camera->pipe2_buffer, 
                          &camera->pipe2_param, &camera->pipe2_dq, &camera->state.pipe2_state);
}

static int camera_start(void *priv)
{
    LOG_DRV_DEBUG("camera_start \r\n");
    camera_t *camera = (camera_t *)priv;
    int ret;
    if(!camera->is_init){
        if (osSemaphoreAcquire(camera->sem_init, 2000) != osOK || !camera->is_init) {
            return AICAM_ERROR_NOT_FOUND;
        }
    }

    osMutexAcquire(camera->mtx_id, osWaitForever);
    if(camera->state.camera_state == CAMERA_START){
        LOG_DRV_DEBUG("camera already start \r\n");
        osMutexRelease(camera->mtx_id);
        return AICAM_OK;
    }

    if((camera->device_ctrl_pipe & CAMERA_CTRL_PIPE1_BIT) != 0){
        ret = pipe1_start(camera);
        if(ret != AICAM_OK){
            osMutexRelease(camera->mtx_id);
            return ret;
        }
    }

    if((camera->device_ctrl_pipe & CAMERA_CTRL_PIPE2_BIT) != 0){
        ret = pipe2_start(camera);
        if(ret != AICAM_OK){
            osMutexRelease(camera->mtx_id);
            return ret;
        }
    }

    if(camera_sensor_set(&camera->sensor_param) != CMW_ERROR_NONE){
        osMutexRelease(camera->mtx_id);
        return AICAM_ERROR;
    }

    camera->state.camera_state = CAMERA_START;
    osMutexRelease(camera->mtx_id);
    return AICAM_OK;
}

static int camera_stop(void *priv)
{
    LOG_DRV_DEBUG("camera_stop \r\n");
    camera_t *camera = (camera_t *)priv;
    if(!camera->is_init)
        return AICAM_ERROR_NOT_FOUND;

    osMutexAcquire(camera->mtx_id, osWaitForever);
    if(camera->state.camera_state == CAMERA_STOP){
        LOG_DRV_DEBUG("camera already stop \r\n");
        osMutexRelease(camera->mtx_id);
        return AICAM_OK;
    }

    if((camera->device_ctrl_pipe & CAMERA_CTRL_PIPE1_BIT) != 0){
        pipe1_stop(camera);
    }

    if((camera->device_ctrl_pipe & CAMERA_CTRL_PIPE2_BIT) != 0){
        pipe2_stop(camera);
    }

    if(CMW_CAMERA_Stop() != CMW_ERROR_NONE){
        osMutexRelease(camera->mtx_id);
        LOG_DRV_DEBUG("camera stop failed \r\n");
        return AICAM_ERROR_BUSY;
    }
    camera->state.camera_state = CAMERA_STOP;
    osMutexRelease(camera->mtx_id);
    return AICAM_OK;
}

static void cameraProcess(void *argument)
{
    camera_t *camera = (camera_t *)argument;
    int ret;
    LOG_DRV_DEBUG("cameraProcess start");
    osDelay(CAMERA_TASK_DELAY_MS);
    ret = CAM_Init(camera);
    if(ret != CMW_ERROR_NONE){
        pwr_manager_release(camera->pwr_handle);
        LOG_DRV_ERROR("camera init failed \r\n");
        osThreadExit();
        return;
    }
    camera->is_init = true;
    osSemaphoreRelease(camera->sem_init);
    while (camera->is_init) {
        if (osSemaphoreAcquire(camera->sem_isp, CAMERA_TASK_DELAY_MS) == osOK) {
#ifdef ISP_MW_TUNING_TOOL_SUPPORT
            if (isp_is_start) {
                ret = ISP_BackgroundProcess(&hIsp);
                if (ret != ISP_OK) {
                    LOG_DRV_ERROR("ISP background process failed: %d\r\n", ret);
                }
            }
#else
            // osMutexAcquire(camera->mtx_id, osWaitForever);
            CMW_CAMERA_Run();
            // osMutexRelease(camera->mtx_id);
#endif
        }
    }
    osThreadExit();
}

static int camera_ioctl(void *priv, unsigned int cmd, unsigned char* ubuf, unsigned long arg)
{
    camera_t *camera = (camera_t *)priv;
    CAM_CMD_E cam_cmd = (CAM_CMD_E)cmd;
    pipe_buffer_t *buffer = NULL;
    int ret = AICAM_OK;

    if(!camera->is_init){
        if (osSemaphoreAcquire(camera->sem_init, 2000) != osOK || !camera->is_init) {
            return AICAM_ERROR_NOT_FOUND;
        }
    }
    osMutexAcquire(camera->mtx_id, osWaitForever);
    switch (cam_cmd)
    {
        case CAM_CMD_SET_SENSOR_PARAM:
// #ifndef ISP_MW_TUNING_TOOL_SUPPORT
            if(ubuf == NULL || arg != sizeof(sensor_params_t)){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            sensor_params_t temp_param;
            memcpy(&temp_param, ubuf, sizeof(sensor_params_t));
            // printf("[CAM_CMD_SET_SENSOR_PARAM] brightness: %d, contrast: %d, mirror_flip: %d, aec: %d\r\n",
            //         temp_param.brightness, temp_param.contrast, temp_param.mirror_flip, temp_param.aec);
            if(temp_param.mirror_flip != camera->sensor_param.mirror_flip){
                if(CMW_CAMERA_SetMirrorFlip(temp_param.mirror_flip) != CMW_ERROR_NONE){
                    ret = AICAM_ERROR_INVALID_PARAM;
                    break;
                }else{
                    camera->sensor_param.mirror_flip = temp_param.mirror_flip;
                }
            }
            // Save old AEC state to detect transition
            uint32_t old_aec = camera->sensor_param.aec;
            
            if(temp_param.aec != camera->sensor_param.aec){
                if(CMW_CAMERA_SetAEC(temp_param.aec) != CMW_ERROR_NONE){
                    ret = AICAM_ERROR_INVALID_PARAM;
                    break;
                }else{
                    camera->sensor_param.aec = temp_param.aec;
                }
            }

            // Handle brightness setting
            if(camera->sensor_param.aec == 0){
                // AEC disabled (1->0), always apply brightness to ensure hardware values are correct
                if((old_aec == 1 && temp_param.aec == 0) || 
                   (temp_param.brightness != camera->sensor_param.brightness)){
                    if(camera_SetBrightness(temp_param.brightness) != CMW_ERROR_NONE){
                        ret = AICAM_ERROR_INVALID_PARAM;
                        break;
                    }else{
                        camera->sensor_param.brightness = temp_param.brightness;
                    }
                }
            }


            if(temp_param.contrast != camera->sensor_param.contrast){
                if(CMW_CAMERA_SetContrast(temp_param.contrast) != CMW_ERROR_NONE){
                    ret = AICAM_ERROR_INVALID_PARAM;
                    break;
                }else{
                    camera->sensor_param.contrast = temp_param.contrast;
                }
            }
// #endif
            ret = AICAM_OK;
            break;
        
        case CAM_CMD_GET_SENSOR_PARAM:
            if(ubuf == NULL || arg != sizeof(sensor_params_t)){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            memcpy(ubuf, &camera->sensor_param, sizeof(sensor_params_t));
            ret = AICAM_OK;
            break;

        case CAM_CMD_SET_PIPE_CTRL:
            if(camera->state.camera_state == CAMERA_START){
                ret = AICAM_ERROR_BUSY;
                break;
            }
            camera->device_ctrl_pipe = *ubuf & (CAMERA_CTRL_PIPE1_BIT | CAMERA_CTRL_PIPE2_BIT);
            ret = AICAM_OK;
            break;
        case CAM_CMD_SET_PIPE1_PARAM:
            if(camera->state.pipe1_state != PIPE_STOP){
                ret = AICAM_ERROR_BUSY;
                break;
            }
            if(ubuf == NULL || arg != sizeof(pipe_params_t)){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            memcpy(&camera->pipe1_param, ubuf, sizeof(pipe_params_t));
            ret = DCMIPP_Pipe1Init(camera);
            printf("[CAM_CMD_SET_PIPE1_PARAM] h: %d, w: %d, bpp: %d, type: %d\r\n", camera->pipe1_param.height, camera->pipe1_param.width, camera->pipe1_param.bpp, camera->pipe1_param.format);
            break;

        case CAM_CMD_SET_PIPE2_PARAM:
            if(camera->state.pipe2_state != PIPE_STOP){
                ret = AICAM_ERROR_BUSY;
                break;
            }
            if(ubuf == NULL || arg != sizeof(pipe_params_t)){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            memcpy(&camera->pipe2_param, ubuf, sizeof(pipe_params_t));
            ret = DCMIPP_Pipe2Init(camera);
            break;

        case CAM_CMD_GET_PIPE1_PARAM:
            if(ubuf == NULL || arg != sizeof(pipe_params_t)){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            memcpy(ubuf, &camera->pipe1_param, sizeof(pipe_params_t));
            ret = AICAM_OK;
            break;

        case CAM_CMD_GET_PIPE2_PARAM:
            if(ubuf == NULL || arg != sizeof(pipe_params_t)){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            memcpy(ubuf, &camera->pipe2_param, sizeof(pipe_params_t));
            ret = AICAM_OK;
            break;

        case CAM_CMD_SET_PIPE1_START:
            if(camera->state.camera_state == CAMERA_START){
                ret = AICAM_ERROR_BUSY;
                break;
            }
            ret = pipe1_start(camera);
            break;

        case CAM_CMD_SET_PIPE1_STOP:
            if(camera->state.camera_state == CAMERA_STOP){
                ret = AICAM_ERROR_BUSY;
                break;
            }
            ret = pipe1_stop(camera);
            break;

        case CAM_CMD_SET_PIPE2_START:
            if(camera->state.camera_state == CAMERA_START){
                ret = AICAM_ERROR_BUSY;
                break;
            }
            ret = pipe2_start(camera);
            break;

        case CAM_CMD_SET_PIPE2_STOP:
            if(camera->state.camera_state == CAMERA_STOP){
                ret = AICAM_ERROR_BUSY;
                break;
            }
            ret = pipe2_stop(camera);
            break;

        case CAM_CMD_GET_PIPE1_BUFFER:
            if(camera->state.pipe1_state != PIPE_START){
                ret = AICAM_ERROR_NOT_SUPPORTED;
                break;
            }
            osMutexRelease(camera->mtx_id);
            if (osSemaphoreAcquire(camera->sem_pipe1, CAMERA_BUFFER_TIMEOUT_MS) == osOK){
                osMutexAcquire(camera->mtx_id, osWaitForever);
                buffer = buffer_get_latest_ready(camera->pipe1_buffer, camera->pipe1_param.buffer_nb, &camera->pipe1_dq);
                if(buffer != NULL){
                    *((unsigned char **)ubuf) = buffer->data;
                    ret = camera->pipe1_param.width * camera->pipe1_param.height * camera->pipe1_param.bpp;
                }else{
                    ret = AICAM_ERROR_NOT_FOUND;
                }
            }else{
                osMutexAcquire(camera->mtx_id, osWaitForever);
                ret = AICAM_ERROR_BUSY;
            }
            break;
        
        case CAM_CMD_GET_PIPE2_BUFFER:
            if(camera->state.pipe2_state != PIPE_START){
                ret = AICAM_ERROR_NOT_SUPPORTED;
                break;
            }
            osMutexRelease(camera->mtx_id);
            if (osSemaphoreAcquire(camera->sem_pipe2, CAMERA_BUFFER_TIMEOUT_MS) == osOK){
                osMutexAcquire(camera->mtx_id, osWaitForever);
                if(camera->pipe2_param.extbuffer_flag == 1){
                    if(camera->pipe2_param.extbuffer != NULL){
                        *((unsigned char **)ubuf) = camera->pipe2_param.extbuffer;
                        ret = camera->pipe2_param.width * camera->pipe2_param.height * camera->pipe2_param.bpp;
                    }else{
                        ret = AICAM_ERROR_NOT_FOUND;
                    }
                }else{
                    buffer = buffer_get_latest_ready(camera->pipe2_buffer, camera->pipe2_param.buffer_nb, &camera->pipe2_dq);
                    if(buffer != NULL){
                        *((unsigned char **)ubuf) = buffer->data;
                        ret = camera->pipe2_param.width * camera->pipe2_param.height * camera->pipe2_param.bpp;
                    }else{
                        ret = AICAM_ERROR_NOT_FOUND;
                    }
                }
            }else{
                osMutexAcquire(camera->mtx_id, osWaitForever);
                ret = AICAM_ERROR_BUSY;
            }
            break;

        case CAM_CMD_GET_PIPE1_BUFFER_WITH_FRAME_ID:
            if(camera->state.pipe1_state != PIPE_START){
                ret = AICAM_ERROR_NOT_SUPPORTED;
                break;
            }
            osMutexRelease(camera->mtx_id);
            if (osSemaphoreAcquire(camera->sem_pipe1, CAMERA_BUFFER_TIMEOUT_MS) == osOK){
                osMutexAcquire(camera->mtx_id, osWaitForever);
                buffer = buffer_get_latest_ready(camera->pipe1_buffer, camera->pipe1_param.buffer_nb, &camera->pipe1_dq);
                if(buffer != NULL){
                    camera_buffer_with_frame_id_t *result = (camera_buffer_with_frame_id_t *)ubuf;
                    result->buffer = buffer->data;
                    result->frame_id = buffer->frame_id;
                    result->size = camera->pipe1_param.width * camera->pipe1_param.height * camera->pipe1_param.bpp;
                    ret = AICAM_OK;
                }else{
                    ret = AICAM_ERROR_NOT_FOUND;
                }
            }else{
                osMutexAcquire(camera->mtx_id, osWaitForever);
                ret = AICAM_ERROR_BUSY;
            }
            break;

        case CAM_CMD_GET_PIPE2_BUFFER_WITH_FRAME_ID:
            if(camera->state.pipe2_state != PIPE_START){
                ret = AICAM_ERROR_NOT_SUPPORTED;
                break;
            }
            osMutexRelease(camera->mtx_id);
            if (osSemaphoreAcquire(camera->sem_pipe2, CAMERA_BUFFER_TIMEOUT_MS) == osOK){
                osMutexAcquire(camera->mtx_id, osWaitForever);
                if(camera->pipe2_param.extbuffer_flag == 1){
                    if(camera->pipe2_param.extbuffer != NULL){
                        camera_buffer_with_frame_id_t *result = (camera_buffer_with_frame_id_t *)ubuf;
                        result->buffer = camera->pipe2_param.extbuffer;
                        result->frame_id = camera->current_frame_id;  // Use current_frame_id for extbuffer
                        result->size = camera->pipe2_param.width * camera->pipe2_param.height * camera->pipe2_param.bpp;
                        ret = AICAM_OK;
                    }else{
                        ret = AICAM_ERROR_NOT_FOUND;
                    }
                }else{
                    buffer = buffer_get_latest_ready(camera->pipe2_buffer, camera->pipe2_param.buffer_nb, &camera->pipe2_dq);
                    if(buffer != NULL){
                        camera_buffer_with_frame_id_t *result = (camera_buffer_with_frame_id_t *)ubuf;
                        result->buffer = buffer->data;
                        result->frame_id = buffer->frame_id;
                        result->size = camera->pipe2_param.width * camera->pipe2_param.height * camera->pipe2_param.bpp;
                        ret = AICAM_OK;
                    }else{
                        ret = AICAM_ERROR_NOT_FOUND;
                    }
                }
            }else{
                osMutexAcquire(camera->mtx_id, osWaitForever);
                ret = AICAM_ERROR_BUSY;
            }
            break;

        case CAM_CMD_RETURN_PIPE1_BUFFER:
            if(camera->state.pipe1_state != PIPE_START){
                ret = AICAM_ERROR_NOT_FOUND;
                break;
            }
            for (int i = 0; i < camera->pipe1_param.buffer_nb; ++i) {
                if (camera->pipe1_buffer[i].data == ubuf) {
                    buffer_release_isr(&camera->pipe1_buffer[i], &camera->pipe1_dq);
                    ret = AICAM_OK;
                    break;
                }
            }
            break;

        case CAM_CMD_RETURN_PIPE2_BUFFER:
            if(camera->state.pipe2_state != PIPE_START){
                ret = AICAM_ERROR_NOT_FOUND;
                break;
            }
            for (int i = 0; i < camera->pipe2_param.buffer_nb; ++i) {
                if (camera->pipe2_buffer[i].data == ubuf) {
                    buffer_release_isr(&camera->pipe2_buffer[i], &camera->pipe2_dq);
                    ret = AICAM_OK;
                    break;
                }
            }
            break;

        case CAM_CMD_SET_PIPE2_BUFFER_ADDR:
            if(camera->state.pipe2_state == PIPE_START){
                ret = AICAM_ERROR_BUSY;
                break;
            }
            if(camera->pipe2_buffer != NULL){
                pipe_buffer_release(camera->pipe2_buffer, &camera->pipe2_param, &camera->pipe2_dq);
                hal_mem_free(camera->pipe2_buffer);
                camera->pipe2_buffer = NULL;
            }

            camera->pipe2_param.buffer_nb = 1;
            camera->pipe2_param.extbuffer_flag = 1;
            camera->pipe2_param.extbuffer = (unsigned char *)ubuf;
            ret = AICAM_OK;
            break;

        default:
            ret = AICAM_ERROR_NOT_SUPPORTED;
            break;
    }
    osMutexRelease(camera->mtx_id);
    return ret;
}
static int pipe_buffer_acquire(pipe_buffer_t *pipe_buffer, pipe_params_t *pipe_param, camera_dq_t *dq)
{
    for (int i = 0; i < pipe_param->buffer_nb; i++) {
        pipe_buffer[i].state = BUFFER_IDLE;
        pipe_buffer[i].frame_id = 0;
        if(pipe_param->extbuffer_flag == 1){
            pipe_buffer[i].data = pipe_param->extbuffer;
        }else{
            pipe_buffer[i].data = hal_mem_alloc_aligned(pipe_param->width * pipe_param->height * pipe_param->bpp, CAMERA_MEMORY_ALIGNMENT, MEM_LARGE);
            LOG_DRV_DEBUG("pipe buffer alloc address 0x%x size %d\r\n", pipe_buffer[i].data, pipe_param->width * pipe_param->height * pipe_param->bpp);
        }
        if(pipe_buffer[i].data == NULL){
            LOG_DRV_ERROR("pipe buffer alloc failed \r\n");
            return -1;
        }
    }
    dq->ready_queue = osMessageQueueNew(pipe_param->buffer_nb, sizeof(uint32_t), NULL);
    dq->idle_sem = osSemaphoreNew(pipe_param->buffer_nb, pipe_param->buffer_nb, NULL);

    return 0;
}


static int pipe_buffer_release(pipe_buffer_t *pipe_buffer, pipe_params_t *pipe_param, camera_dq_t *dq)
{
    
    if (dq->ready_queue != NULL) {
        osMessageQueueDelete(dq->ready_queue);
        dq->ready_queue = NULL;
    }

    if (dq->idle_sem != NULL) {
        osSemaphoreDelete(dq->idle_sem);
        dq->idle_sem = NULL;
    }

    if(pipe_param->extbuffer_flag == 1){
        return 0;
    }
    for (int i = 0; i < pipe_param->buffer_nb; i++) {
        if (pipe_buffer[i].data) {
            LOG_DRV_DEBUG("pipe buffer release address 0x%x \r\n", pipe_buffer[i].data);
            hal_mem_free(pipe_buffer[i].data);
        }
    }

    return 0;
}
static int camera_init(void *priv)
{
    LOG_DRV_DEBUG("camera_init \r\n");
    camera_t *camera = (camera_t *)priv;
    camera->pwr_handle = pwr_manager_get_handle(PWR_SENSOR_NAME);
    pwr_manager_acquire(camera->pwr_handle);

    camera->pipe1_param.width = PIPE1_DEFAULT_WIDTH;
    camera->pipe1_param.height = PIPE1_DEFAULT_HEIGHT;
    camera->pipe1_param.format = PIPE1_DEFAULT_FORMAT;
    camera->pipe1_param.bpp = PIPE1_DEFAULT_BPP;
    camera->pipe1_param.fps = CAMERA_FPS;
    camera->pipe1_param.buffer_nb = CAPTURE_BUFFER_NB;

    camera->pipe2_param.width = PIPE2_DEFAULT_WIDTH;
    camera->pipe2_param.height = PIPE2_DEFAULT_HEIGHT;
    camera->pipe2_param.format = PIPE2_DEFAULT_FORMAT;
    camera->pipe2_param.bpp = PIPE2_DEFAULT_BPP;
    camera->pipe2_param.fps = CAMERA_FPS;
    camera->pipe2_param.buffer_nb = NN_BUFFER_NB;


    camera->mtx_id = osMutexNew(NULL);
    camera->current_frame_id = 0;
    camera->sem_init = osSemaphoreNew(1, 0, NULL);
    camera->sem_isp = osSemaphoreNew(1, 0, NULL);
    camera->sem_pipe1 = osSemaphoreNew(1, 0, NULL);
    camera->sem_pipe2 = osSemaphoreNew(1, 0, NULL);
    camera->state.camera_state = CAMERA_STOP;
    camera->state.pipe1_state = PIPE_STOP;
    camera->state.pipe2_state = PIPE_STOP;

    camera->device_ctrl_pipe = CAMERA_CTRL_PIPE1_BIT | CAMERA_CTRL_PIPE2_BIT;
    camera->camera_processId = osThreadNew(cameraProcess, camera, &cameraTask_attributes);
    return 0;
}

static int camera_deinit(void *priv)
{
    camera_t *camera = (camera_t *)priv;

    CMW_CAMERA_DeInit();
    camera->is_init = false;
    pwr_manager_release(camera->pwr_handle);
    osSemaphoreRelease(camera->sem_isp);
    osDelay(CAMERA_DEINIT_DELAY_MS);
    if (camera->camera_processId != NULL) {
        osThreadTerminate(camera->camera_processId);
        camera->camera_processId = NULL;
    }

    if (camera->pwr_handle != 0) {
        pwr_manager_release(camera->pwr_handle);
        camera->pwr_handle = 0;
    }

    if (camera->sem_init != NULL) {
        osSemaphoreRelease(camera->sem_init);
        osSemaphoreDelete(camera->sem_init);
        camera->sem_init = NULL;
    }

    if (camera->sem_isp != NULL) {
        osSemaphoreRelease(camera->sem_isp);
        osSemaphoreDelete(camera->sem_isp);
        camera->sem_isp = NULL;
    }

    if (camera->sem_pipe1 != NULL) {
        osSemaphoreDelete(camera->sem_pipe1);
        camera->sem_pipe1 = NULL;
    }

    if (camera->sem_pipe2 != NULL) {
        osSemaphoreDelete(camera->sem_pipe2);
        camera->sem_pipe2 = NULL;
    }

    if (camera->mtx_id != NULL) {
        osMutexDelete(camera->mtx_id);
        camera->mtx_id = NULL;
    }

    pipe_buffer_release(camera->pipe1_buffer, &camera->pipe1_param, &camera->pipe1_dq);
    pipe_buffer_release(camera->pipe2_buffer, &camera->pipe2_param, &camera->pipe2_dq);

    if (camera->pipe1_buffer) {
        hal_mem_free(camera->pipe1_buffer);
        camera->pipe1_buffer = NULL;
    }
    if (camera->pipe2_buffer) {
        hal_mem_free(camera->pipe2_buffer);
        camera->pipe2_buffer = NULL;
    }

    return 0;
}

int camera_register(void)
{
    static dev_ops_t camera_ops ={
        .init = camera_init, 
        .deinit = camera_deinit, 
        .start = camera_start,
        .stop = camera_stop,
        .ioctl = camera_ioctl
    };

    if(g_camera.is_init == true){
        return AICAM_OK;
    }
    int ret;
    device_t *dev = hal_mem_alloc_fast(sizeof(device_t));
    g_camera.dev = dev;
    strcpy(dev->name, CAMERA_DEVICE_NAME);
    dev->type = DEV_TYPE_VIDEO;
    dev->ops = &camera_ops;
    dev->priv_data = &g_camera;

    ret = device_register(g_camera.dev);
    if(ret != 0){
        hal_mem_free(g_camera.dev);
        g_camera.dev = NULL;
        return AICAM_ERROR;
    }
    return AICAM_OK;
}

int camera_unregister(void)
{
    if (g_camera.dev) {
        device_unregister(g_camera.dev);
        hal_mem_free(g_camera.dev);
        g_camera.dev = NULL;
    }
    return AICAM_OK;
}