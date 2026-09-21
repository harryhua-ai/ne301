#include <stdio.h>
#include <string.h>
#include "video_ai_result_list.h"

static int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);       \
            g_failures++;                                               \
        }                                                               \
    } while (0)

typedef struct {
    int a_calls;
    int b_calls;
    int c_calls;
    int last_payload;
} invoke_log_t;

static void counting_invoke(video_ai_result_fn_t cb, void *user_data, void *ctx)
{
    invoke_log_t *log = (invoke_log_t *)ctx;
    if (cb == (video_ai_result_fn_t)0xA) {
        log->a_calls++;
        log->last_payload = (int)(uintptr_t)user_data;
    } else if (cb == (video_ai_result_fn_t)0xB) {
        log->b_calls++;
    } else if (cb == (video_ai_result_fn_t)0xC) {
        log->c_calls++;
    }
}

static void test_add_and_foreach_exactly_once(void)
{
    video_ai_result_list_t list;
    video_ai_result_list_reset(&list);
    CHECK(video_ai_result_list_count(&list) == 0);

    CHECK(video_ai_result_list_add(&list, (video_ai_result_fn_t)0xA, (void *)1) == 0);
    CHECK(video_ai_result_list_add(&list, (video_ai_result_fn_t)0xB, (void *)2) == 0);
    CHECK(video_ai_result_list_count(&list) == 2);

    invoke_log_t log;
    memset(&log, 0, sizeof(log));
    video_ai_result_list_foreach(&list, counting_invoke, &log);
    CHECK(log.a_calls == 1);
    CHECK(log.b_calls == 1);
    CHECK(log.last_payload == 1);
}

static void test_duplicate_add_replaces_user_data(void)
{
    video_ai_result_list_t list;
    video_ai_result_list_reset(&list);

    CHECK(video_ai_result_list_add(&list, (video_ai_result_fn_t)0xA, (void *)1) == 0);
    CHECK(video_ai_result_list_add(&list, (video_ai_result_fn_t)0xA, (void *)7) == 0);
    CHECK(video_ai_result_list_count(&list) == 1);

    invoke_log_t log;
    memset(&log, 0, sizeof(log));
    video_ai_result_list_foreach(&list, counting_invoke, &log);
    CHECK(log.a_calls == 1);
    CHECK(log.last_payload == 7);
}

static void test_remove(void)
{
    video_ai_result_list_t list;
    video_ai_result_list_reset(&list);

    CHECK(video_ai_result_list_add(&list, (video_ai_result_fn_t)0xA, (void *)1) == 0);
    CHECK(video_ai_result_list_add(&list, (video_ai_result_fn_t)0xB, (void *)2) == 0);

    CHECK(video_ai_result_list_remove(&list, (video_ai_result_fn_t)0xA) == 0);
    CHECK(video_ai_result_list_remove(&list, (video_ai_result_fn_t)0xA) == -1);
    CHECK(video_ai_result_list_remove(&list, NULL) == -1);
    CHECK(video_ai_result_list_count(&list) == 1);

    invoke_log_t log;
    memset(&log, 0, sizeof(log));
    video_ai_result_list_foreach(&list, counting_invoke, &log);
    CHECK(log.a_calls == 0);
    CHECK(log.b_calls == 1);
}

static void test_capacity_full(void)
{
    video_ai_result_list_t list;
    video_ai_result_list_reset(&list);

    for (int i = 0; i < VIDEO_AI_RESULT_LIST_MAX; ++i) {
        CHECK(video_ai_result_list_add(&list, (video_ai_result_fn_t)(uintptr_t)(0x10 + i), NULL) == 0);
    }
    CHECK(video_ai_result_list_count(&list) == VIDEO_AI_RESULT_LIST_MAX);
    CHECK(video_ai_result_list_add(&list, (video_ai_result_fn_t)0xFF, NULL) == -1);
    CHECK(video_ai_result_list_count(&list) == VIDEO_AI_RESULT_LIST_MAX);

    video_ai_result_list_remove(&list, (video_ai_result_fn_t)0x11);
    CHECK(video_ai_result_list_add(&list, (video_ai_result_fn_t)0xFF, NULL) == 0);
    CHECK(video_ai_result_list_count(&list) == VIDEO_AI_RESULT_LIST_MAX);
}

static void test_reset_and_null_safety(void)
{
    video_ai_result_list_t list;
    video_ai_result_list_reset(&list);

    CHECK(video_ai_result_list_add(&list, (video_ai_result_fn_t)0xA, NULL) == 0);
    video_ai_result_list_reset(&list);
    CHECK(video_ai_result_list_count(&list) == 0);

    video_ai_result_list_reset(NULL);
    CHECK(video_ai_result_list_add(NULL, (video_ai_result_fn_t)0xA, NULL) == -1);
    CHECK(video_ai_result_list_remove(NULL, (video_ai_result_fn_t)0xA) == -1);
    CHECK(video_ai_result_list_count(NULL) == 0);
    video_ai_result_list_foreach(NULL, counting_invoke, NULL);
    video_ai_result_list_foreach(&list, NULL, NULL);
    CHECK(video_ai_result_list_add(&list, NULL, NULL) == -1);
    CHECK(video_ai_result_list_count(&list) == 0);
}

int main(void)
{
    test_add_and_foreach_exactly_once();
    test_duplicate_add_replaces_user_data();
    test_remove();
    test_capacity_full();
    test_reset_and_null_safety();

    if (g_failures != 0) {
        printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all video ai result list tests passed\n");
    return 0;
}
