#include "video_ai_result_list.h"
#include <string.h>

void video_ai_result_list_reset(video_ai_result_list_t *list)
{
    if (!list) {
        return;
    }
    memset(list, 0, sizeof(*list));
}

int video_ai_result_list_add(video_ai_result_list_t *list,
                             video_ai_result_fn_t cb, void *user_data)
{
    if (!list || !cb) {
        return -1;
    }
    for (uint8_t i = 0; i < list->count; ++i) {
        if (list->entries[i].cb == cb) {
            list->entries[i].user_data = user_data;
            return 0;
        }
    }
    if (list->count >= VIDEO_AI_RESULT_LIST_MAX) {
        return -1;
    }
    list->entries[list->count].cb = cb;
    list->entries[list->count].user_data = user_data;
    list->count++;
    return 0;
}

int video_ai_result_list_remove(video_ai_result_list_t *list, video_ai_result_fn_t cb)
{
    if (!list || !cb) {
        return -1;
    }
    for (uint8_t i = 0; i < list->count; ++i) {
        if (list->entries[i].cb == cb) {
            memmove(&list->entries[i], &list->entries[i + 1],
                    (list->count - 1u - i) * sizeof(list->entries[0]));
            list->count--;
            return 0;
        }
    }
    return -1;
}

uint8_t video_ai_result_list_count(const video_ai_result_list_t *list)
{
    return list ? list->count : 0u;
}

void video_ai_result_list_foreach(const video_ai_result_list_t *list,
                                  video_ai_result_list_invoke_t invoke, void *ctx)
{
    if (!list || !invoke) {
        return;
    }
    for (uint8_t i = 0; i < list->count; ++i) {
        invoke(list->entries[i].cb, list->entries[i].user_data, ctx);
    }
}
