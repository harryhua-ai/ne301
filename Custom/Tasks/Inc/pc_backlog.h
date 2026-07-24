#ifndef PC_BACKLOG_H
#define PC_BACKLOG_H
#include "aicam_types.h"

typedef enum { BACKLOG_MQTT = 0, BACKLOG_WEBHOOK = 1 } backlog_channel_t;
#define BACKLOG_CHANNEL_COUNT 2

const char* backlog_channel_name(backlog_channel_t ch);  /* "mqtt" / "webhook" */

aicam_result_t backlog_push(backlog_channel_t ch, const char* json, uint16_t capacity);
/* Pop oldest into buf (null-terminated). Returns AICAM_ERROR_NOT_FOUND if empty. */
aicam_result_t backlog_pop (backlog_channel_t ch, char* buf, size_t buf_len);
uint16_t       backlog_count(backlog_channel_t ch);
void           backlog_drop_oldest(backlog_channel_t ch);

#endif
