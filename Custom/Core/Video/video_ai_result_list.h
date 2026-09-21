#ifndef VIDEO_AI_RESULT_LIST_H
#define VIDEO_AI_RESULT_LIST_H

#include <stdint.h>

#define VIDEO_AI_RESULT_LIST_MAX 4

typedef void (*video_ai_result_fn_t)(const void *result, uint32_t frame_id,
                                     uint32_t inference_time_ms, void *user_data);

typedef struct {
    video_ai_result_fn_t cb;
    void *user_data;
} video_ai_result_list_entry_t;

typedef struct {
    video_ai_result_list_entry_t entries[VIDEO_AI_RESULT_LIST_MAX];
    uint8_t count;
} video_ai_result_list_t;

void video_ai_result_list_reset(video_ai_result_list_t *list);

int video_ai_result_list_add(video_ai_result_list_t *list,
                             video_ai_result_fn_t cb, void *user_data);

int video_ai_result_list_remove(video_ai_result_list_t *list, video_ai_result_fn_t cb);

uint8_t video_ai_result_list_count(const video_ai_result_list_t *list);

typedef void (*video_ai_result_list_invoke_t)(video_ai_result_fn_t cb,
                                              void *user_data, void *ctx);

void video_ai_result_list_foreach(const video_ai_result_list_t *list,
                                  video_ai_result_list_invoke_t invoke, void *ctx);

#endif
