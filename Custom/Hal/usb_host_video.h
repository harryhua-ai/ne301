#ifndef USB_HOST_VIDEO_H
#define USB_HOST_VIDEO_H

#include <stddef.h>
#include <stdint.h>
#include "cmsis_os2.h"
#include "dev_manager.h"
#include "pwr.h"
#include "ux_api.h"
#include "ux_host_class_video.h"
#include "ux_system.h"
#include "ux_utility.h"
#include "ux_hcd_stm32.h"

typedef enum
{
  STOP_USB_HOST = 1,
  START_USB_HOST,
} USB_MODE_STATE;

typedef enum {
  VIDEO_IDLE = 0,
  VIDEO_START,
  VIDEO_READ,
  VIDEO_WAIT,
}video_streaming_stateTypeDef;

typedef struct {
    bool is_init;
    device_t *dev;
    osMutexId_t mtx_id;
    osSemaphoreId_t sem_id;
    osSemaphoreId_t video_transfer_sem;
    osThreadId_t usbvideo_processId;
    PowerHandle  pwr_handle;
    UX_HOST_CLASS_VIDEO     *video;
    video_streaming_stateTypeDef video_streaming_state;
    UX_HOST_CLASS_VIDEO_TRANSFER_REQUEST video_transfer_request;
} usbvideo_t;

void usbvideo_register(void);

#endif