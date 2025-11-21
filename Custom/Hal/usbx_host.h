#ifndef __USB_HOST_H__
#define __USB_HOST_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "ux_api.h"

#define H_USBX_MEM_SIZE                                     (96 * 1024)
#define H_USBX_MEM_SIZE_UNCACHED                            (20 * 1024)

typedef UINT (*ux_host_event_callback_t)(ULONG event, UX_HOST_CLASS *current_class, void *current_instance);
typedef VOID (*ux_host_error_callback_t)(UINT system_level, UINT system_context, UINT error_code);
typedef UINT (*ux_host_class_entry_function_t)(struct UX_HOST_CLASS_COMMAND_STRUCT *);
typedef UINT (*ux_host_hcd_init_function_t)(UX_HCD *hcd);

typedef struct {
    uint8_t is_uninit_memory;
    ux_host_event_callback_t event_callback;
    ux_host_error_callback_t error_callback;
    UCHAR *class_name;
    ux_host_class_entry_function_t class_entry_function;
    UCHAR *hcd_name;
    ux_host_hcd_init_function_t hcd_init_function;
} ux_host_config_t;

int USBX_Host_Init(ux_host_config_t *config);
void USBX_Host_Deinit(ux_host_config_t *config);

#ifdef __cplusplus
}
#endif

#endif
