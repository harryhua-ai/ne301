#ifndef DRIVER_CORE_H
#define DRIVER_CORE_H

#include <stdbool.h>


typedef int (*register_func_t)(void);
typedef int (*unregister_func_t)(void);

typedef struct {
    const char *name;
    register_func_t reg_func;
    unregister_func_t unreg_func;
} driver_module_t;

bool driver_core_init(void);
#endif