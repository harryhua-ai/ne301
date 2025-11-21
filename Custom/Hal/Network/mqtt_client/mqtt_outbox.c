#include <stdlib.h>
#include <string.h>
#include "queue.h"
#include "mqtt_outbox.h"

typedef struct outbox_item {
    char *buffer;
    int len;
    int msg_id;
    int msg_type;
    int msg_qos;
    outbox_tick_t tick;
    pending_state_t pending;
    STAILQ_ENTRY(outbox_item) next;
} outbox_item_t;

STAILQ_HEAD(outbox_list_t, outbox_item);

struct outbox_t {
    uint64_t size;
    uint64_t num;
    struct outbox_list_t *list;
};

outbox_handle_t outbox_init(void)
{
    outbox_handle_t outbox = hal_mem_calloc_fast(1, sizeof(struct outbox_t));
    OBX_MEM_CHECK(outbox, return NULL);
    outbox->list = hal_mem_calloc_fast(1, sizeof(struct outbox_list_t));
    OBX_MEM_CHECK(outbox->list, {hal_mem_free(outbox); return NULL;});
    outbox->size = 0;
    outbox->num = 0;
    STAILQ_INIT(outbox->list);
    return outbox;
}

outbox_item_handle_t outbox_enqueue(outbox_handle_t outbox, outbox_message_handle_t message, outbox_tick_t tick)
{
    outbox_item_handle_t item = hal_mem_calloc_fast(1, sizeof(outbox_item_t));
    OBX_MEM_CHECK(item, return NULL);
    item->msg_id = message->msg_id;
    item->msg_type = message->msg_type;
    item->msg_qos = message->msg_qos;
    item->tick = tick;
    item->len =  message->len + message->remaining_len;
    item->pending = QUEUED;
    item->buffer = hal_mem_alloc_large(message->len + message->remaining_len);
    OBX_MEM_CHECK(item->buffer, {
        hal_mem_free(item);
        return NULL;
    });
    memcpy(item->buffer, message->data, message->len);
    if (message->remaining_data) {
        memcpy(item->buffer + message->len, message->remaining_data, message->remaining_len);
    }
    STAILQ_INSERT_TAIL(outbox->list, item, next);
    outbox->size += item->len;
    outbox->num ++;
    LOG_DRV_DEBUG("ENQUEUE msgid=%d, msg_type=%d, len=%d, size=%lu", message->msg_id, message->msg_type, message->len + message->remaining_len, outbox_get_size(outbox));
    return item;
}

outbox_item_handle_t outbox_get(outbox_handle_t outbox, int msg_id)
{
    outbox_item_handle_t item;
    STAILQ_FOREACH(item, outbox->list, next) {
        if (item->msg_id == msg_id) {
            return item;
        }
    }
    return NULL;
}

outbox_item_handle_t outbox_dequeue(outbox_handle_t outbox, pending_state_t pending, outbox_tick_t *tick)
{
    outbox_item_handle_t item;
    STAILQ_FOREACH(item, outbox->list, next) {
        if (item->pending == pending) {
            if (tick) {
                *tick = item->tick;
            }
            return item;
        }
    }
    return NULL;
}

int outbox_delete_item(outbox_handle_t outbox, outbox_item_handle_t item_to_delete)
{
    outbox_item_handle_t item;
    STAILQ_FOREACH(item, outbox->list, next) {
        if (item == item_to_delete) {
            STAILQ_REMOVE(outbox->list, item, outbox_item, next);
            outbox->size -= item->len;
            outbox->num --;
            hal_mem_free(item->buffer);
            hal_mem_free(item);
            return 0;
        }
    }
    return -1;
}

uint8_t *outbox_item_get_data(outbox_item_handle_t item,  size_t *len, uint16_t *msg_id, int *msg_type, int *qos)
{
    if (item) {
        *len = item->len;
        *msg_id = item->msg_id;
        *msg_type = item->msg_type;
        *qos = item->msg_qos;
        return (uint8_t *)item->buffer;
    }
    return NULL;
}

int outbox_delete(outbox_handle_t outbox, int msg_id, int msg_type)
{
    outbox_item_handle_t item, tmp;
    STAILQ_FOREACH_SAFE(item, outbox->list, next, tmp) {
        if (item->msg_id == msg_id && (0xFF & (item->msg_type)) == msg_type) {
            STAILQ_REMOVE(outbox->list, item, outbox_item, next);
            outbox->size -= item->len;
            outbox->num --;
            hal_mem_free(item->buffer);
            hal_mem_free(item);
            LOG_DRV_DEBUG("DELETED msgid=%d, msg_type=%d, remain size=%lu", msg_id, msg_type, outbox_get_size(outbox));
            return 0;
        }

    }
    return -1;
}

int outbox_set_pending(outbox_handle_t outbox, int msg_id, pending_state_t pending)
{
    outbox_item_handle_t item = outbox_get(outbox, msg_id);
    if (item) {
        item->pending = pending;
        return 0;
    }
    return -1;
}

int outbox_item_set_pending(outbox_item_handle_t item, pending_state_t pending)
{
    if (item) {
        item->pending = pending;
        return 0;
    }
    return -1;
}

pending_state_t outbox_item_get_pending(outbox_item_handle_t item)
{
    if (item) {
        return item->pending;
    }
    return QUEUED;
}

int outbox_set_tick(outbox_handle_t outbox, int msg_id, outbox_tick_t tick)
{
    outbox_item_handle_t item = outbox_get(outbox, msg_id);
    if (item) {
        item->tick = tick;
        return 0;
    }
    return -1;
}

int outbox_delete_single_expired(outbox_handle_t outbox, outbox_tick_t current_tick, outbox_tick_t timeout)
{
    int msg_id = -1;
    outbox_item_handle_t item;
    STAILQ_FOREACH(item, outbox->list, next) {
        if (current_tick - item->tick > timeout) {
            STAILQ_REMOVE(outbox->list, item, outbox_item, next);
            hal_mem_free(item->buffer);
            outbox->size -= item->len;
            outbox->num --;
            msg_id = item->msg_id;
            hal_mem_free(item);
            return msg_id;
        }

    }
    return msg_id;
}

int outbox_delete_expired(outbox_handle_t outbox, outbox_tick_t current_tick, outbox_tick_t timeout)
{
    int deleted_items = 0;
    outbox_item_handle_t item, tmp;
    STAILQ_FOREACH_SAFE(item, outbox->list, next, tmp) {
        if (current_tick - item->tick > timeout) {
            STAILQ_REMOVE(outbox->list, item, outbox_item, next);
            hal_mem_free(item->buffer);
            outbox->size -= item->len;
            outbox->num --;
            hal_mem_free(item);
            deleted_items ++;
        }

    }
    return deleted_items;
}

uint64_t outbox_get_size(outbox_handle_t outbox)
{
    return outbox->size;
}

uint64_t outbox_get_num(outbox_handle_t outbox)
{
    return outbox->num;
}

void outbox_delete_all_items(outbox_handle_t outbox)
{
    outbox_item_handle_t item, tmp;
    STAILQ_FOREACH_SAFE(item, outbox->list, next, tmp) {
        STAILQ_REMOVE(outbox->list, item, outbox_item, next);
        outbox->size -= item->len;
        outbox->num --;
        hal_mem_free(item->buffer);
        hal_mem_free(item);
    }
}

void outbox_destroy(outbox_handle_t outbox)
{
    outbox_delete_all_items(outbox);
    hal_mem_free(outbox->list);
    hal_mem_free(outbox);
}
