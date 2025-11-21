#ifndef _UVC_H
#define _UVC_H

//#include "FreeRTOS.h"
//#include "task.h"
#include "cmsis_os2.h"
#include "generic_cmdline.h"
#include "dev_manager.h"
#include "uvcl.h"


#define UVC_BUFFER_SIZE (400 * 1024)

typedef uint8_t * (*encode_frame_func_t)(uint8_t *p_in, int *out_len, int is_intra_force);

typedef struct {
    bool is_init;
    device_t *dev;
    osMutexId_t mtx_id;
    osSemaphoreId_t sem_isp;
    osSemaphoreId_t sem_send;
    osThreadId_t uvc_processId;
    UVCL_Conf_t conf;
    UVCL_Callbacks_t cbs;
    int is_active;
    int buffer_flying;
    bool irq_flag;
} uvc_t;

void UVC_IRQHandler(void);
int send_uvc_frame(uint8_t *buffer, int len);
void uvc_register(void);

#endif