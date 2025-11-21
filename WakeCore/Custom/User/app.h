#ifndef __APP_H
#define __APP_H

#ifdef __cplusplus
extern "C" {
#endif

#define APP_IS_USER_STR_CMD         (0)
#define APP_TASK_NAME               "app_task"
#define APP_TASK_STACK_SIZE         (1024)
#define APP_TASK_PRIORITY           (2)

#if APP_IS_USER_STR_CMD == 0
#define MS_BD_TASK_NAME             "ms_bd_task"
#define MS_BD_TASK_STACK_SIZE       (1024)
#define MS_BD_TASK_PRIORITY         (3)
#define MS_BD_KEEPLIVE_ENABLE       (0)
#define MS_BD_STARTUP_TIMEOUT_MS    (1000 * 60 * 5)
#define MS_BD_KEEPLIVE_INTERVAL_MS  (1000 * 60 * 5)

typedef enum {
    N6_STATE_STARTUP = 0,
    N6_STATE_RUNNING,
    N6_STATE_STOPPED,
    N6_STATE_WAIT_REBOOT,
} app_n6_state_t;

#endif

void app_init(void);

#ifdef __cplusplus
}
#endif
#endif /* __APP_H */
