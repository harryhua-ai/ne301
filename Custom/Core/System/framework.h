#ifndef _FRAMEWORK_H
#define _FRAMEWORK_H

#include <stdbool.h>
#include "cmsis_os2.h"

typedef struct {
    osMutexId_t MtxId;
    osSemaphoreId_t SemId;
    osThreadId_t frameworkProcessId;
} framework_t;

bool framework_init(void);

#endif