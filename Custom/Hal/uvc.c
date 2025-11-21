#include "uvc.h"
#include "camera.h"
#include "debug.h"
#include "enc.h"
#include "common_utils.h"

static uvc_t g_uvc = {0};

static uint8_t uvc_in_buffers[UVC_BUFFER_SIZE] ALIGN_32 IN_PSRAM;

static uint8_t uvc_tread_stack[1024 * 4] ALIGN_32 IN_PSRAM;
const osThreadAttr_t uvcTask_attributes = {
    .name = "uvcTask",
    .priority = (osPriority_t) osPriorityRealtime,
    .stack_mem = uvc_tread_stack,
    .stack_size = sizeof(uvc_tread_stack),
};

void app_uvc_streaming_active(struct uvcl_callbacks *cbs)
{
    g_uvc.is_active = 1;
    osSemaphoreRelease(g_uvc.sem_send);
}

void app_uvc_streaming_inactive(struct uvcl_callbacks *cbs)
{
    g_uvc.is_active = 0;
    osSemaphoreRelease(g_uvc.sem_send);
}

static void app_uvc_frame_release(struct uvcl_callbacks *cbs, void *frame)
{
    assert(g_uvc.buffer_flying);
    g_uvc.buffer_flying = 0;
    osSemaphoreRelease(g_uvc.sem_send);
}


static void uvcProcess(void *argument)
{
    uvc_t *uvc = (uvc_t *)argument;
    LOG_DRV_DEBUG("uvcProcess \r\n");
    osDelay(1000);
    UVCL_Init(USB1_OTG_HS, &uvc->conf, &uvc->cbs);
    // LOG_DRV_DEBUG("UVCL_Init ret:%d \r\n", ret);
    g_uvc.buffer_flying = 0;
    uvc->is_init = true;
    for(;;){
        osSemaphoreAcquire(uvc->sem_isp, osWaitForever);
        if(uvc->irq_flag){
            uvc->irq_flag = false;
            UVCL_IRQHandler();
            HAL_NVIC_EnableIRQ(USB1_OTG_HS_IRQn);
        }
    }
}

static int uvc_init(void *priv)
{
    LOG_DRV_DEBUG("uvc_init \r\n");
    uvc_t *uvc = (uvc_t *)priv;
    uvc->mtx_id = osMutexNew(NULL);
    uvc->sem_isp = osSemaphoreNew(1, 0, NULL);
    uvc->sem_send = osSemaphoreNew(1, 0, NULL);
    uvc->conf.width = VENC_DEFAULT_WIDTH;
    uvc->conf.height = VENC_DEFAULT_HEIGHT;
    uvc->conf.fps = VENC_DEFAULT_FPS;
    uvc->conf.payload_type = UVCL_PAYLOAD_FB_H264;
    uvc->conf.is_immediate_mode = 1;
    uvc->cbs.streaming_active = app_uvc_streaming_active;
    uvc->cbs.streaming_inactive = app_uvc_streaming_inactive;
    uvc->cbs.frame_release = app_uvc_frame_release;
    uvc->uvc_processId = osThreadNew(uvcProcess, uvc, &uvcTask_attributes);
    return 0;
}

void UVC_IRQHandler(void)
{
    HAL_NVIC_DisableIRQ(USB1_OTG_HS_IRQn);
    g_uvc.irq_flag = true;
    osSemaphoreRelease(g_uvc.sem_isp);
}

// int send_uvc_frame(uint8_t *buffer, encode_frame_func_t encode_frame, int len)
// {
//     int ret = 0;
//     uint8_t *frame_buf = NULL;
//     int frame_len;
//     static int force_intra = 1;
//     static int uvc_is_active_prev = 0;
//     int flag;
//     static int cnt = 0;

//     if(buffer == NULL)
//         return -1;

//     osSemaphoreAcquire(g_uvc.sem_send, osWaitForever);
//     flag = uvc_is_active_prev;
//     uvc_is_active_prev = g_uvc.is_active;
//     if(!g_uvc.is_active)
//         return -1;

//     if(encode_frame != NULL){
//         frame_buf = encode_frame(buffer, &frame_len, !flag || force_intra);
//         if(frame_len > 0 && g_uvc.buffer_flying != 0){
//             force_intra = 1;
//             LOG_DRV_WARN("uvc is lagging, drop frame and force next to be an Intra \r\n");
//             return -1;
//         }else if(frame_len <= 0){
//             return -1;
//         }
//         // LOG_DRV_WARN("encode_frame frame_len= %d \r\n",frame_len);
//         force_intra = 0;
//     }else{
//         LOG_DRV_WARN(" send_uvc_frame is NULL \r\n");
//         frame_buf = buffer;
//         frame_len = len;
//     }
    
//     memcpy(uvc_in_buffers, frame_buf, frame_len);

//     osMutexAcquire(g_uvc.mtx_id, osWaitForever);
//     g_uvc.buffer_flying = 1;
//     ret = UVCL_ShowFrame(uvc_in_buffers, frame_len);
//     if (ret != 0){
//         g_uvc.buffer_flying = 0;
//     }
//     osMutexRelease(g_uvc.mtx_id);
//     if(cnt++ % 30 == 0){
//         LOG_DRV_WARN(" send_uvc_frame cnt:%d \r\n",cnt);
//     }
//     // LOG_DRV_WARN(" send_uvc_frame \r\n");
//     return ret;
// }

int send_uvc_frame(uint8_t *buffer, int len)
{
    int ret = 0;
    static int cnt = 0;

    if(buffer == NULL || len <= 0)
        return -1;

    osSemaphoreAcquire(g_uvc.sem_send, osWaitForever);

    if(!g_uvc.is_active)
        return -1;
    
    memcpy(uvc_in_buffers, buffer, len);

    osMutexAcquire(g_uvc.mtx_id, osWaitForever);
    g_uvc.buffer_flying = 1;
    ret = UVCL_ShowFrame(uvc_in_buffers, len);
    if (ret != 0){
        g_uvc.buffer_flying = 0;
    }
    osMutexRelease(g_uvc.mtx_id);
    if(cnt++ % 300 == 0){
        LOG_DRV_WARN(" send_uvc_frame cnt:%d \r\n",cnt);
    }
    return ret;
}

void uvc_register(void)
{
    static dev_ops_t uvc_ops = {
        .init = uvc_init
    };

    if(g_uvc.is_init == true){
        return;
    }
    device_t *dev = hal_mem_alloc_fast(sizeof(device_t));
    g_uvc.dev = dev;
    strcpy(dev->name, UVC_DEVICE_NAME);
    dev->type = DEV_TYPE_VIDEO;
    dev->ops = &uvc_ops;
    dev->priv_data = &g_uvc;

    device_register(g_uvc.dev);
}

