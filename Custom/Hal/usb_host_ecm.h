#ifndef USB_HOST_ECM_H
#define USB_HOST_ECM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usbx_host.h"
#include "ux_host_class_cdc_ecm.h"

typedef enum {
    USB_HOST_ECM_EVENT_ACTIVATE = 0,
    USB_HOST_ECM_EVENT_DEACTIVATE = 1,
    USB_HOST_ECM_EVENT_UP = 2,
    USB_HOST_ECM_EVENT_DOWN = 3,
    USB_HOST_ECM_EVENT_DATA = 4,
    USB_HOST_ECM_EVENT_ERROR = 5,
} usb_host_ecm_event_type_t;

typedef void (*usb_host_ecm_event_callback_t)(usb_host_ecm_event_type_t event, void *arg);

int usb_host_ecm_init(usb_host_ecm_event_callback_t event_callback);
int usb_host_ecm_send_raw_data(NX_PACKET *packet);
void usb_host_ecm_deinit(void);

#ifdef __cplusplus
}
#endif

#endif  
