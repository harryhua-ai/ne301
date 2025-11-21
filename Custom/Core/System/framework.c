#include "framework.h"
#include "dev_manager.h"

static framework_t g_framework;

static void framwork_lock(void)
{
    osMutexAcquire(g_framework.MtxId, osWaitForever);
}

static void framwork_unlock(void)
{
    osMutexRelease(g_framework.MtxId);
}

bool framework_init(void)
{
    
    g_framework.MtxId = osMutexNew(NULL);
    g_framework.SemId = osSemaphoreNew(1, 0, NULL);
    device_manager_init(framwork_lock, framwork_unlock);

    return 0;
}
