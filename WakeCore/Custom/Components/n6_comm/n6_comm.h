#ifndef __N6_COMM_H
#define __N6_COMM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sys_config.h"

#define N6_COMM_MAX_LEN                 2048
#define N6_COMM_TASK_NAME               "n6_comm_task"
#define N6_COMM_TASK_STACK_SIZE         1024
#define N6_COMM_TASK_PRIORITY           5

#define N6_COMM_EVENT_TX_DONE           (1 << 0)
#define N6_COMM_EVENT_RX_DONE           (1 << 1)
#define N6_COMM_EVENT_ERR               (1 << 2)

typedef void (*n6_comm_recv_callback_t)(uint8_t *rbuf, uint16_t rlen);

int n6_comm_init(void);
int n6_comm_send(uint8_t *wbuf, uint16_t wlen, uint32_t timeout_ms);
int n6_comm_send_str(const char *str);
void n6_comm_set_recv_callback(n6_comm_recv_callback_t callback);
void n6_comm_deinit(void);

void n6_comm_set_event_isr(uint32_t event);

#ifdef __cplusplus
}
#endif
#endif /* __N6_COMM_H */
