#include "usbx_host.h"
#include "common_utils.h"
#include "debug.h"
#include "usb_otg.h"
#include "ux_system.h"
#include "ux_utility.h"

extern HCD_HandleTypeDef hhcd_USB_OTG_HS2;
static uint8_t usbx_mem_pool[H_USBX_MEM_SIZE] IN_PSRAM ALIGN_32;
static uint8_t usbx_mem_pool_uncached[H_USBX_MEM_SIZE_UNCACHED] UNCACHED ALIGN_32;
static uint8_t usbx_is_init = 0;

int USBX_Host_Init(ux_host_config_t *config)
{
    int ret = 0;
    if (config == NULL) return UX_INVALID_PARAMETER;
    if (usbx_is_init) return UX_INVALID_STATE;

    /* Initialize the USB OTG HS2 */
    MX_USB2_OTG_HS_HCD_Init();

    if (!config->is_uninit_memory) {
        /* Initialize USBX Memory */
        ret = ux_system_initialize(usbx_mem_pool, H_USBX_MEM_SIZE, usbx_mem_pool_uncached, H_USBX_MEM_SIZE_UNCACHED);
        if (ret != UX_SUCCESS) {
            LOG_DRV_ERROR("USBX Memory Initialization Failed: 0x%X", ret);
            goto USBX_Host_Init_Exit;
        }
    }
    
    /* Install the host portion of USBX */
    ret = ux_host_stack_initialize(config->event_callback);
    if (ret != UX_SUCCESS) {
        LOG_DRV_ERROR("USBX Host Initialization Failed: 0x%X", ret);
        goto USBX_Host_Init_Exit;
    }

    /* Register a callback error function */
    ux_utility_error_callback_register(config->error_callback);

    /* Register the host class */
    ret = ux_host_stack_class_register(config->class_name, config->class_entry_function);
    if (ret != UX_SUCCESS) {
        LOG_DRV_ERROR("USBX Host Class Registration Failed: 0x%X", ret);
        goto USBX_Host_Init_Exit;
    }

    /* Register the host HCD */
    ret = ux_host_stack_hcd_register(config->hcd_name, config->hcd_init_function, USB2_OTG_HS_BASE, (ULONG)&hhcd_USB_OTG_HS2);
    if (ret != UX_SUCCESS) {
        LOG_DRV_ERROR("USBX Host HCD Registration Failed: 0x%X", ret);
        goto USBX_Host_Init_Exit;
    }

    ret = HAL_HCD_Start(&hhcd_USB_OTG_HS2);
    if (ret != HAL_OK) {
        LOG_DRV_ERROR("USBX Host HCD Start Failed: 0x%X", ret);
        goto USBX_Host_Init_Exit;
    }
    
    usbx_is_init = 1;
    return ret;
USBX_Host_Init_Exit:
    USBX_Host_Deinit(config);
    return ret;
}

void USBX_Host_Deinit(ux_host_config_t *config)
{
    int ret = 0;
    if (config == NULL || !usbx_is_init) return;

    ret = HAL_HCD_Stop(&hhcd_USB_OTG_HS2);
    if (ret != HAL_OK) {
        LOG_DRV_ERROR("USBX Host HCD Stop Failed: 0x%X", ret);
    }
    printf("1\r\n");
    ret = ux_host_stack_hcd_unregister(config->hcd_name, USB2_OTG_HS_BASE, (ULONG)&hhcd_USB_OTG_HS2);
    if (ret != UX_SUCCESS) {
        LOG_DRV_ERROR("USBX Host HCD Unregistration Failed: 0x%X", ret);
    }
    printf("2\r\n");
    ret = ux_host_stack_class_unregister(config->class_entry_function);
    if (ret != UX_SUCCESS) {
        LOG_DRV_ERROR("USBX Host Class Unregistration Failed: 0x%X", ret);
    }
    printf("3\r\n");
    ret = ux_host_stack_uninitialize();
    if (ret != UX_SUCCESS) {
        LOG_DRV_ERROR("USBX Host Uninitialization Failed: 0x%X", ret);
    }
    printf("4\r\n");
    if (!config->is_uninit_memory) {
        ret = ux_system_uninitialize();
        if (ret != UX_SUCCESS) {
            LOG_DRV_ERROR("USBX Memory Uninitialization Failed: 0x%X", ret);
        }
    }
    printf("5\r\n");
    HAL_HCD_DeInit(&hhcd_USB_OTG_HS2);
    usbx_is_init = 0;
}
