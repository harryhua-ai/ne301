#include "generic_utils.h"
#include "mem.h"

void generic_queue_init(GenericQueue *queue, utils_lock_func_t lock, utils_unlock_func_t unlock) 
{
    queue->head = NULL;
    queue->tail = NULL;
    queue->size = 0;
    queue->total_data_size = 0;
    queue->lock = lock;
    queue->unlock = unlock;
}

void generic_queue_destroy(GenericQueue *queue, utils_free_func_t free_func) 
{
    if (queue->lock) queue->lock();
    QueueNode *cur = queue->head;
    while (cur) {
        QueueNode *tmp = cur;
        cur = cur->next;
        if (free_func) free_func(tmp->data);
        hal_mem_free(tmp);
    }
    queue->head = queue->tail = NULL;
    queue->size = 0;
    queue->total_data_size = 0;
    if (queue->unlock) queue->unlock();
}

void generic_queue_push(GenericQueue *queue, void *data, size_t data_size) 
{
    QueueNode *node = hal_mem_alloc_fast(sizeof(QueueNode));
    node->data = hal_mem_alloc_fast(data_size);
    memcpy(node->data, data, data_size);
    node->data_size = data_size;
    node->read_offset = 0;
    node->next = NULL;
    if (queue->lock) queue->lock();
    if (queue->tail) {
        queue->tail->next = node;
        queue->tail = node;
    } else {
        queue->head = queue->tail = node;
    }
    queue->size++;
    queue->total_data_size += data_size;
    if (queue->unlock) queue->unlock();
}

size_t generic_queue_pop(GenericQueue *queue, void *buf, size_t buf_size)
{
    if (queue->lock) queue->lock();

    QueueNode *node = queue->head;
    size_t copy_size = 0;

    if (!node) {
        if (queue->unlock) queue->unlock();
        return 0;
    }

    // Calculate copyable data length this time
    size_t remaining = node->data_size - node->read_offset;
    if (remaining > 0) {
        copy_size = remaining < buf_size ? remaining : buf_size;
        if (buf && copy_size > 0)
            memcpy(buf, (char*)node->data + node->read_offset, copy_size);

        node->read_offset += copy_size;
    }

    // If all data has been read, delete the node
    if (node->read_offset >= node->data_size) {
        queue->head = node->next;
        if (!queue->head)
            queue->tail = NULL;
        queue->size--;
        queue->total_data_size -= node->data_size;

        hal_mem_free(node->data);
        hal_mem_free(node);
    }

    if (queue->unlock) queue->unlock();

    return copy_size;
}

size_t generic_queue_size(GenericQueue *queue) 
{
    size_t sz;
    if (queue->lock) queue->lock();
    sz = queue->size;
    if (queue->unlock) queue->unlock();
    return sz;
}

size_t generic_queue_data_size(GenericQueue *queue) 
{
    size_t sz;
    if (queue->lock) queue->lock();
    sz = queue->total_data_size;
    if (queue->unlock) queue->unlock();
    return sz;
}

void generic_queue_foreach(GenericQueue *queue, void (*cb)(void *data, size_t data_size, void *user), void *user) 
{
    if (queue->lock) queue->lock();
    QueueNode *cur = queue->head;
    while (cur) {
        cb(cur->data, cur->data_size, user);
        cur = cur->next;
    }
    if (queue->unlock) queue->unlock();
}

int generic_queue_empty(GenericQueue *queue)
{
    int empty;
    if (queue->lock) queue->lock();
    empty = (queue->size == 0);
    if (queue->unlock) queue->unlock();
    return empty;
}

int str2hex(const char *str, uint8_t *out, size_t max_len)
{
    size_t out_idx = 0;
    size_t i = 0;
    while (str[i] != '\0') {
        // Skip spaces
        while (str[i] == ' ') i++;
        // Skip 0x/0X prefix
        if ((str[i] == '0') && (str[i+1] == 'x' || str[i+1] == 'X')) {
            i += 2;
            continue;
        }
        // Check if there are two more characters to read
        if (!isxdigit((unsigned char)str[i]) || !isxdigit((unsigned char)str[i+1]))
            break;
        if (out_idx >= max_len)
            return -2; // Buffer overflow

        char byte_str[3] = {str[i], str[i+1], 0};
        out[out_idx++] = (uint8_t)strtol(byte_str, NULL, 16);
        i += 2;
    }
    // At least one byte is required
    return (out_idx > 0) ? out_idx : -1;
}