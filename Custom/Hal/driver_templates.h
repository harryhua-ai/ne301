#ifndef _TEMPLATES_H
#define _TEMPLATES_H

#include <stddef.h>
#include <stdint.h>
#include "cmsis_os2.h"
#include "dev_manager.h"
#include "pwr.h"

typedef struct {
    bool is_init;
    device_t *dev;
    osMutexId_t mtx_id;
    osSemaphoreId_t sem_id;
    osThreadId_t templates_processId;
    PowerHandle  pwr_handle;
} templates_t;


void templates_register(void);

#endif