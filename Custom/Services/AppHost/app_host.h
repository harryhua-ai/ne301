#ifndef APP_HOST_H
#define APP_HOST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_HOST_DEFAULT_IMAGE_PATH "/apps/apphost_test.bin"

int app_host_load_and_run(const char *path);
void app_host_cmd_register(void);

#ifdef __cplusplus
}
#endif

#endif
