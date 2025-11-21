#ifndef GENERIC_UTILS_H
#define GENERIC_UTILS_H

#include <stddef.h>
#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*utils_lock_func_t)(void);
typedef void (*utils_unlock_func_t)(void);
typedef void (*utils_free_func_t)(void *);

typedef struct QueueNode {
    void *data;
    size_t data_size;
    size_t read_offset;
    struct QueueNode *next;
} QueueNode;

typedef struct {
    QueueNode *head;
    QueueNode *tail;
    size_t size;                // Number of nodes
    size_t total_data_size;     // Total bytes of all data in queue
    void *lock_ctx;
    utils_lock_func_t lock;
    utils_unlock_func_t unlock;
} GenericQueue;

void generic_queue_init(GenericQueue *queue, utils_lock_func_t lock, utils_unlock_func_t unlock);
void generic_queue_destroy(GenericQueue *queue, utils_free_func_t free_func);
void generic_queue_push(GenericQueue *queue, void *data, size_t data_size); // Enqueue (need to specify data size)
size_t generic_queue_pop(GenericQueue *queue, void *buf, size_t buf_size);
size_t generic_queue_size(GenericQueue *queue);                             // Number of nodes
size_t generic_queue_data_size(GenericQueue *queue);                        // Total data bytes
void generic_queue_foreach(GenericQueue *queue, void (*cb)(void *data, size_t data_size, void *user), void *user);
int generic_queue_empty(GenericQueue *queue);
int str2hex(const char *str, uint8_t *out, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif