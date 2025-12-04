#ifndef _WDG_H
#define _WDG_H

#include <stddef.h>
#include <stdint.h>
#include "cmsis_os2.h"
#include "dev_manager.h"

#define WDG_IS_USE_IWDG    1

typedef struct {
    bool is_init;
    device_t *dev;
    osThreadId_t wdg_processId;
} wdg_t;

void wdg_register(void);
void wdg_task_change_priority(osPriority_t priority);

#endif