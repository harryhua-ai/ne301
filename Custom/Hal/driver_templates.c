#include "driver_templates.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "common_utils.h"
#include "debug.h"

static templates_t g_templates = {0};

const osThreadAttr_t templatesTask_attributes = {
    .name = "templatesTask",
    .priority = (osPriority_t) osPriorityNormal,
    .stack_size = 8 * 1024
};

static int templates_cmd(int argc, char* argv[])
{

    return 0;
}

debug_cmd_reg_t templates_cmd_table[] = {
    {"templates", "templates", templates_cmd},
};

// 
static void templates_cmd_register(void)
{
    debug_cmdline_register(templates_cmd_table, sizeof(templates_cmd_table) / sizeof(templates_cmd_table[0]));
}

static void templatesProcess(void *argument)
{
    templates_t *templates = (templates_t *)argument;
    int ret;
    LOG_DRV_INFO("templatesProcess start \r\n");

    templates->is_init = true;
    while (templates->is_init) {
        
        osDelay(1000);
    }
TEMPLATES_EXIT:
    LOG_DRV_ERROR("templatesProcess exit \r\n");
    device_unregister(templates->dev);
    templates->templates_processId = NULL;  // Thread cleans up its own ID
    osThreadExit();
}

static int templates_ioctl(void *priv, unsigned int cmd, unsigned char* ubuf, unsigned long arg)
{
    templates_t *templates = (templates_t *)priv;
    if(!templates->is_init)
        return -1;
    osMutexAcquire(templates->mtx_id, osWaitForever);

    osMutexRelease(templates->mtx_id);
    return 0;
}

static int templates_init(void *priv)
{
    LOG_DRV_DEBUG("templates_init \r\n");
    templates_t *templates = (templates_t *)priv;
    templates->mtx_id = osMutexNew(NULL);
    templates->sem_id = osSemaphoreNew(1, 0, NULL);
    templates->pwr_handle = pwr_manager_get_handle(PWR_USB_NAME);
    pwr_manager_acquire(templates->pwr_handle);
    osDelay(10);

    templates->templates_processId = osThreadNew(templatesProcess, templates, &templatesTask_attributes);
    return 0;
}

static int templates_deinit(void *priv)
{
    templates_t *templates = (templates_t *)priv;

    templates->is_init = false;
    osSemaphoreRelease(templates->sem_id);
    osDelay(100);
    if (templates->templates_processId != NULL && osThreadGetId() != templates->templates_processId) {
        osThreadTerminate(templates->templates_processId);
        templates->templates_processId = NULL;
    }

    if (templates->sem_id != NULL) {
        osSemaphoreDelete(templates->sem_id);
        templates->sem_id = NULL;
    }

    if (templates->mtx_id != NULL) {
        osMutexDelete(templates->mtx_id);
        templates->mtx_id = NULL;
    }

    if (templates->pwr_handle != 0) {
        pwr_manager_release(templates->pwr_handle);
        templates->pwr_handle = 0;
    }

    return 0;
}

void templates_register(void)
{
    static dev_ops_t templates_ops = {
        .init = templates_init, 
        .deinit = templates_deinit, 
        .ioctl = templates_ioctl
    };
    device_t *dev = hal_mem_alloc_fast(sizeof(device_t));
    g_templates.dev = dev;
    strcpy(dev->name, "templates");
    dev->type = DEV_TYPE_VIDEO;
    dev->ops = &templates_ops;
    dev->priv_data = &g_templates;

    device_register(g_templates.dev);
    
    
    driver_cmd_register_callback("templates", templates_cmd_register);
}

void templates_unregister(void)
{
    device_unregister(g_templates.dev);
    hal_mem_free(g_templates.dev);
}
