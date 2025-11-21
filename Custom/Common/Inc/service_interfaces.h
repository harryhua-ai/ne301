#ifndef SERVICE_INTERFACES_H
#define SERVICE_INTERFACES_H

#include "aicam_types.h"
#include "aicam_error.h"

/* Service state enumeration */
typedef enum {
    SERVICE_STATE_UNINITIALIZED = 0,
    SERVICE_STATE_INITIALIZING,
    SERVICE_STATE_INITIALIZED,
    SERVICE_STATE_CONNECTED,
    SERVICE_STATE_DISCONNECTED,
    SERVICE_STATE_WAIT_RECONNECT,
    SERVICE_STATE_RUNNING,
    SERVICE_STATE_SUSPENDED,
    SERVICE_STATE_ERROR,
    SERVICE_STATE_SHUTDOWN
} service_state_t;

/* Service base interface */
typedef struct {
    const char *name;
    uint32_t version;
    aicam_result_t (*init)(void *config);
    aicam_result_t (*start)(void);
    aicam_result_t (*stop)(void);
    aicam_result_t (*suspend)(void);
    aicam_result_t (*resume)(void);
    aicam_result_t (*deinit)(void);
    service_state_t (*get_state)(void);
} service_interface_t;

#endif /* SERVICE_INTERFACES_H */ 