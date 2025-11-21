#ifndef PIR_H_
#define PIR_H_

#include "pwr.h"
#include "dev_manager.h"

#define PIR_INIT_RETRY      (10)

typedef enum {
    PIR_CMD_SET_CB   = MISC_CMD_BASE + 0x100,// Button  short press callback
} PIR_CMD_E;

typedef struct {
    bool is_init;
    device_t *dev;
    osMutexId_t mtx_id;
    void (*cb)(void);
    PowerHandle  pwr_handle;
} pir_t;


int pir_register(void);


#endif