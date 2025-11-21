#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

//#include "FreeRTOS.h"
//#include "task.h"
#include "cmsis_os2.h"


typedef int PowerHandle;
#define POWER_NAME_MAX_LEN 32
// Power state structure
typedef struct {
    char name[POWER_NAME_MAX_LEN];
    void (*power_on)(void);
    void (*power_off)(void);
    int ref_count;
    int is_on;
    osMutexId_t lock;
    PowerHandle handle;
} PowerState;

// Power manager structure
typedef struct {
    PowerState **powers;
    size_t capacity;
    size_t count;
    osMutexId_t lock;
    PowerHandle next_handle;
} PowerManager;

PowerManager* power_manager_create();
void power_manager_destroy(PowerManager *manager);
PowerHandle power_manager_register(PowerManager *manager, const char *name,void (*power_on)(void), void (*power_off)(void));
PowerHandle power_manager_get_handle(PowerManager *manager, const char *name);
int power_manager_acquire_by_handle(PowerManager *manager, PowerHandle handle);
int power_manager_release_by_handle(PowerManager *manager, PowerHandle handle);
int power_manager_get_state_by_handle(PowerManager *manager, PowerHandle handle,int *is_on, int *ref_count);

#endif