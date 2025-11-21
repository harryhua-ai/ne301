#include "usb_host_video.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "usb_otg.h"
#include "usbx_host.h"
#include "common_utils.h"
#include "debug.h"
#include "mem.h"

ULONG max_payload_size;
UCHAR *frame_buffer;
UINT start_of_image = 0;
UCHAR *image_buffer;
UINT image_buffer_size;
UINT image_number;

static usbvideo_t g_usbvideo = {0};
static uint8_t usbvideo_tread_stack[1024 * 4] ALIGN_32;

const osThreadAttr_t usbvideoTask_attributes = {
    .name = "usbvideoTask",
    .priority = (osPriority_t) osPriorityNormal,
    .stack_mem = usbvideo_tread_stack,
    .stack_size = sizeof(usbvideo_tread_stack),
};

static int usbvideo_cmd(int argc, char* argv[])
{
    return 0;
}

debug_cmd_reg_t usbvideo_cmd_table[] = {
    {"usbvideo", "USB Video", usbvideo_cmd},
};

// 
static void usbvideo_cmd_register(void)
{
    debug_cmdline_register(usbvideo_cmd_table, sizeof(usbvideo_cmd_table) / sizeof(usbvideo_cmd_table[0]));
}

void ux_host_error_callback(UINT system_level, UINT system_context, UINT error_code)
{
    switch (error_code)
    {
        case UX_DEVICE_ENUMERATION_FAILURE:
            printf("USB Device Enumeration Failure\r\n ");
            break;

        case  UX_NO_DEVICE_CONNECTED:
            printf("USB Device disconnected\r\n ");
            break;

        default:

            break;
    }

}

UINT ux_host_event_callback(ULONG event, UX_HOST_CLASS *current_class, void *current_instance)
{
    UINT status = UX_SUCCESS;

    printf("ux_host_event_callback event:0x%lx\r\n", (unsigned long)event);
    switch (event)
    {
        case UX_DEVICE_INSERTION:

            if (current_class->ux_host_class_entry_function == ux_host_class_video_entry)
            {
                if (g_usbvideo.video == UX_NULL)
                {
                    /* get current video instance */
                    g_usbvideo.video = (UX_HOST_CLASS_VIDEO*)current_instance;

                    printf("USB Video Device Inserted\r\n");
                }
            }

            break;

        case UX_DEVICE_REMOVAL:

            /* USER CODE BEGIN UX_DEVICE_REMOVAL */
            if ((void*)g_usbvideo.video == current_instance)
            {
                g_usbvideo.video_streaming_state = VIDEO_IDLE;
                if(frame_buffer != NULL){
                    hal_mem_free(frame_buffer);
                    frame_buffer = NULL;
                }
                    
                if(image_buffer != NULL){
                    hal_mem_free(image_buffer);
                    image_buffer = NULL;
                }
                current_instance = UX_NULL;
                printf("USB Video Device Removal\r\n");
                osSemaphoreRelease(g_usbvideo.video_transfer_sem);
            }
            /* USER CODE END UX_DEVICE_REMOVAL */

            break;

        case UX_DEVICE_CONNECTION:

        /* USER CODE BEGIN UX_DEVICE_CONNECTION */
            if (g_usbvideo.video != UX_NULL)
            {
                printf("PID: %#x \r\n",(UINT)_ux_system_host->ux_system_host_device_array->ux_device_descriptor.idProduct);
                printf("VID: %#x \r\n", (UINT)_ux_system_host->ux_system_host_device_array->ux_device_descriptor.idVendor);
                printf("USB Video Host App...\r\n");
                printf("Video Device is ready...\r\n");
            }

            break;

        case UX_DEVICE_DISCONNECTION:

            break;

        default:

            break;
    }

    return status;
}


void video_transfer_request_completion(UX_HOST_CLASS_VIDEO_TRANSFER_REQUEST *video_transfer_request)
{
    osSemaphoreRelease(g_usbvideo.video_transfer_sem);
    printf("video_transfer_request_completion\r\n");
    for ( UINT i=0 ; i <max_payload_size ; i++)
    {
        SCB_InvalidateDCache_by_Addr(frame_buffer, max_payload_size);
        /* The structure of each frame is SOI at 0xFFD8 and EOI at 0xFFD9*/
        if (!start_of_image && frame_buffer[i] == 0xFF && frame_buffer[i+1] == 0xD8)
        {
            start_of_image = 1;
            image_buffer_size = 0;
            image_number++;
            printf("frame number %d \r\n ", image_number);
        }
        if (start_of_image )
        {
            /* overflow check*/
            if (image_buffer_size < max_payload_size * 10U)
            {
                image_buffer[image_buffer_size++] = frame_buffer[i];

                /*end of image*/
                if (frame_buffer[i] == 0xFF && frame_buffer[i+1] == 0xD9)
                {
                image_buffer[image_buffer_size++] = frame_buffer[i+1];
                printf("frame size = %d \r\n ", image_buffer_size);
                start_of_image = 0;
                }
            }
            else{
                image_buffer_size = 0;
            }
        }
    }
}

static ux_host_config_t usbx_host_config = {
    .is_uninit_memory = false,
    .event_callback = ux_host_event_callback,
    .error_callback = ux_host_error_callback,
    .class_name = _ux_system_host_class_video_name,
    .class_entry_function = ux_host_class_video_entry,
    .hcd_name = _ux_system_host_hcd_stm32_name,
    .hcd_init_function = _ux_hcd_stm32_initialize,
};
static void usbvideoProcess(void *argument)
{
    usbvideo_t *usbvideo = (usbvideo_t *)argument;
    device_t *uvc_dev;
    int ret;
    printf("usbvideoProcess start \r\n");
    
    uvc_dev = device_find_pattern("uvc", DEV_TYPE_VIDEO);
    if (uvc_dev != NULL && uvc_dev->priv_data != NULL) {
        usbx_host_config.is_uninit_memory = true;
    }

    ret = USBX_Host_Init(&usbx_host_config);
    if (ret != 0) goto USB_EXIT;
    usbvideo->is_init = true;
    osDelay(1000);
    UINT status = UX_SUCCESS;

    ULONG format = 0;
    ULONG frameWidth = 0;
    ULONG frameHeight = 0;
    ULONG frameInterval = 0;
    ULONG video_buffer_size;
    UX_HOST_CLASS_VIDEO_PARAMETER_FRAME_DATA frame_parameter;
    UX_HOST_CLASS_VIDEO_PARAMETER_FORMAT_DATA format_parameter;
    format_parameter.ux_host_class_video_parameter_format_requested = 1;

    printf("usbvideoProcess init end \r\n");
    while (usbvideo->is_init) {
        if ((usbvideo->video != NULL) && (usbvideo->video->ux_host_class_video_state == UX_HOST_CLASS_INSTANCE_LIVE))
        {
            printf("Video Streaming State: %d\r\n", usbvideo->video_streaming_state);
            switch (usbvideo->video_streaming_state)
            {
                case VIDEO_IDLE :

                    /* Get first format data */
                    status = _ux_host_class_video_format_data_get(usbvideo->video, &format_parameter);
                    if (status != UX_SUCCESS)
                    {
                        LOG_DRV_ERROR("ux_host_class_video_format_data_get first error status:0x%x\r\n", status);
                        break;
                    }

                    /* Get the frame data */
                    frame_parameter.ux_host_class_video_parameter_frame_requested = 1;
                    frame_parameter.ux_host_class_video_parameter_frame_subtype = format_parameter.ux_host_class_video_parameter_format_subtype;
                    status = _ux_host_class_video_frame_data_get(usbvideo->video, &frame_parameter);
                    if (status != UX_SUCCESS)
                    {
                        LOG_DRV_ERROR("ux_host_class_video_frame_data_get error status:0x%x\r\n", status);
                        break;
                    }

                    /* Use the retrieved parameters values */
                    format = format_parameter.ux_host_class_video_parameter_format_subtype;
                    frameWidth = frame_parameter.ux_host_class_video_parameter_frame_width;
                    frameHeight = frame_parameter.ux_host_class_video_parameter_frame_height;
                    frameInterval = frame_parameter.ux_host_class_video_parameter_default_frame_interval;
                    LOG_DRV_INFO("format:%d, frameWidth:%d, frameHeight:%d, frameInterval:%d\r\n", format, frameWidth, frameHeight, frameInterval);
                    /* set parameters to Retrieve supported format data and frame data */
                    status = ux_host_class_video_frame_parameters_set(usbvideo->video, format, frameWidth, frameHeight, frameInterval);
                    if (status != UX_SUCCESS)
                    {
                        LOG_DRV_ERROR("ux_host_class_video_frame_parameters_set error\r\n");
                        break;
                    }

                    /* Get video max payload size */
                    max_payload_size = ux_host_class_video_max_payload_get(usbvideo->video);

                    frame_buffer = hal_mem_alloc_aligned(max_payload_size, 32, MEM_LARGE);

                    video_buffer_size = max_payload_size * 10U;

                    /* Allocate buffer for format data */
                    image_buffer = hal_mem_alloc_aligned(video_buffer_size, 32, MEM_LARGE);
                    image_buffer_size = 0;

                    /*reset frame buffer memory*/
                    memset(frame_buffer, 0, max_payload_size);
                    memset(image_buffer, 0, video_buffer_size);

                    /* Update video demo state machine */
                    usbvideo->video_streaming_state = VIDEO_START;

                    break;

                case VIDEO_START :

                    /* start the video */

                    if (ux_host_class_video_start(usbvideo->video) != UX_SUCCESS)
                    {
                        /* try next format */
                        format_parameter.ux_host_class_video_parameter_format_requested++;
                        if(frame_buffer != NULL){
                            hal_mem_free(frame_buffer);
                            frame_buffer = NULL;
                        }
                        if(image_buffer != NULL){
                            hal_mem_free(image_buffer);
                            image_buffer = NULL;
                        }
                        usbvideo->video_streaming_state = VIDEO_IDLE;
                        break;
                    }

                    /* Update video demo state machine */
                    usbvideo->video_streaming_state = VIDEO_READ;

                break;

                case VIDEO_READ :

                    /* Prepare the video transfer_request */
                    usbvideo->video_transfer_request.ux_host_class_video_transfer_request_data_pointer = frame_buffer;
                    usbvideo->video_transfer_request.ux_host_class_video_transfer_request_requested_length = max_payload_size;
                    usbvideo->video_transfer_request.ux_host_class_video_transfer_request_class_instance = usbvideo->video;
                    usbvideo->video_transfer_request.ux_host_class_video_transfer_request_completion_function = video_transfer_request_completion;

                    /* read the transfert request */
                    UINT read_status = ux_host_class_video_read(usbvideo->video, &usbvideo->video_transfer_request);
                    LOG_DRV_INFO("ux_host_class_video_read status: %x\r\n", read_status);
                    osSemaphoreAcquire(usbvideo->video_transfer_sem, osWaitForever);
                    break;

                case VIDEO_WAIT :

                default:

                    /* Sleep thread for 10 ms */
                    osDelay(10);

                    break;
            }
        }else{
            osDelay(10);
        }
    }
USB_EXIT:
    LOG_DRV_ERROR("usbvideoProcess exit \r\n");
    device_unregister(usbvideo->dev);
    usbvideo->usbvideo_processId = NULL;  // Thread cleans up its own ID
    osThreadExit();
}

static int usbvideo_ioctl(void *priv, unsigned int cmd, unsigned char* ubuf, unsigned long arg)
{
    usbvideo_t *usbvideo = (usbvideo_t *)priv;
    if(!usbvideo->is_init)
        return -1;
    osMutexAcquire(usbvideo->mtx_id, osWaitForever);

    osMutexRelease(usbvideo->mtx_id);
    return 0;
}

static int usbvideo_init(void *priv)
{
    printf("usbvideo_init \r\n");
    usbvideo_t *usbvideo = (usbvideo_t *)priv;
    usbvideo->mtx_id = osMutexNew(NULL);
    usbvideo->sem_id = osSemaphoreNew(1, 0, NULL);
    usbvideo->video_transfer_sem = osSemaphoreNew(1, 0, NULL);
    usbvideo->pwr_handle = pwr_manager_get_handle(PWR_USB_NAME);
    pwr_manager_acquire(usbvideo->pwr_handle);
    osDelay(10);

    usbvideo->usbvideo_processId = osThreadNew(usbvideoProcess, usbvideo, &usbvideoTask_attributes);
    return 0;
}

static int usbvideo_deinit(void *priv)
{
    usbvideo_t *usbvideo = (usbvideo_t *)priv;

    usbvideo->is_init = false;
    osSemaphoreRelease(usbvideo->sem_id);
    osDelay(100);
    if (usbvideo->usbvideo_processId != NULL && osThreadGetId() != usbvideo->usbvideo_processId) {
        osThreadTerminate(usbvideo->usbvideo_processId);
        usbvideo->usbvideo_processId = NULL;
    }

    if (usbvideo->sem_id != NULL) {
        osSemaphoreDelete(usbvideo->sem_id);
        usbvideo->sem_id = NULL;
    }

    if (usbvideo->mtx_id != NULL) {
        osMutexDelete(usbvideo->mtx_id);
        usbvideo->mtx_id = NULL;
    }

    if (usbvideo->pwr_handle != 0) {
        pwr_manager_release(usbvideo->pwr_handle);
        usbvideo->pwr_handle = 0;
    }

    USBX_Host_Deinit(&usbx_host_config);
    return 0;
}

void usbvideo_register(void)
{
    static dev_ops_t usbvideo_ops = {
        .init = usbvideo_init, 
        .deinit = usbvideo_deinit, 
        .ioctl = usbvideo_ioctl
    };
    device_t *dev = hal_mem_alloc_fast(sizeof(device_t));
    g_usbvideo.dev = dev;
    strcpy(dev->name, "usbvideo");
    dev->type = DEV_TYPE_VIDEO;
    dev->ops = &usbvideo_ops;
    dev->priv_data = &g_usbvideo;

    device_register(g_usbvideo.dev);
    
    
    driver_cmd_register_callback("usbvideo", usbvideo_cmd_register);
}