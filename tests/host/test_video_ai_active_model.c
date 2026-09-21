#include <stdio.h>
#include <string.h>
#include "video_ai_active_model.h"

static int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);       \
            g_failures++;                                               \
        }                                                               \
    } while (0)

#define CHECK_STR(actual, expected)                                     \
    do {                                                                \
        const char *a_ = (actual);                                      \
        const char *e_ = (expected);                                    \
        if (!a_ || strcmp(a_, e_) != 0) {                               \
            printf("FAIL %s:%d \"%s\" != \"%s\"\n", __FILE__,           \
                   __LINE__, a_ ? a_ : "(null)", e_);                   \
            g_failures++;                                               \
        }                                                               \
    } while (0)

typedef struct {
    int fail_unload;
    int fail_prepare;
    int unload_calls;
    int prepare_calls;
    video_ai_active_model_install_t next;
} fake_reload_t;

static aicam_result_t fake_unload_active(void *user)
{
    fake_reload_t *fake = (fake_reload_t *)user;
    if (!fake) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    fake->unload_calls++;
    return fake->fail_unload ? AICAM_ERROR : AICAM_OK;
}

static aicam_result_t fake_prepare_install(void *user, video_ai_active_model_install_t *out)
{
    fake_reload_t *fake = (fake_reload_t *)user;
    if (!fake || !out) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    fake->prepare_calls++;
    if (fake->fail_prepare) {
        return AICAM_ERROR;
    }
    *out = fake->next;
    return AICAM_OK;
}

static void run_reload(video_ai_active_model_state_t *state, fake_reload_t *fake)
{
    video_ai_active_model_reload_ops_t ops = {
        .user = fake,
        .unload_active = fake_unload_active,
        .prepare_install = fake_prepare_install,
    };
    aicam_result_t result = video_ai_active_model_reload(state, &ops);
    if (result != AICAM_OK) {
        printf("reload returned %d (unload_calls=%d prepare_calls=%d)\n",
               result, fake->unload_calls, fake->prepare_calls);
    }
}

static video_ai_active_model_install_t make_install(const char *name, pp_type_t result_type,
                                                    const char *const *classes, uint16_t count)
{
    video_ai_active_model_install_t install;
    memset(&install, 0, sizeof(install));
    snprintf(install.name, sizeof(install.name), "%s", name);
    snprintf(install.version, sizeof(install.version), "1.0");
    snprintf(install.model_type, sizeof(install.model_type), "OBJECT_DETECTION");
    snprintf(install.postprocess_type, sizeof(install.postprocess_type), "%s", name);
    install.result_type = result_type;
    install.classes.count = count;
    for (uint16_t i = 0; i < count; ++i) {
        snprintf(install.classes.names[i], NN_CLASS_NAME_MAX, "%s", classes[i]);
    }
    return install;
}

static void case_a_initial_install(void)
{
    video_ai_active_model_state_t state;
    video_ai_active_model_reset(&state);

    CHECK(video_ai_active_model_is_loaded(&state) == 0u);
    CHECK(video_ai_active_model_generation(&state) == 0u);
    CHECK(video_ai_active_model_class_count(&state) == 0u);

    const char *classes[] = { "person", "car" };
    video_ai_active_model_install_t a = make_install("model_a", PP_TYPE_OD, classes, 2);
    video_ai_active_model_commit(&state, &a);

    CHECK(video_ai_active_model_is_loaded(&state) == 1u);
    CHECK(video_ai_active_model_generation(&state) == 1u);
    CHECK(video_ai_active_model_class_count(&state) == 2u);

    char buf[NN_CLASS_NAME_MAX];
    CHECK(video_ai_active_model_class_name(&state, 0, buf, sizeof(buf)) == 0);
    CHECK_STR(buf, "person");
    CHECK(video_ai_active_model_class_name(&state, 1, buf, sizeof(buf)) == 0);
    CHECK_STR(buf, "car");
}

static void case_b_failed_reload(void)
{
    video_ai_active_model_state_t state;
    video_ai_active_model_reset(&state);

    const char *classes_a[] = { "person" };
    video_ai_active_model_install_t a = make_install("model_a", PP_TYPE_OD, classes_a, 1);
    video_ai_active_model_commit(&state, &a);
    CHECK(video_ai_active_model_generation(&state) == 1u);

    fake_reload_t fake;
    memset(&fake, 0, sizeof(fake));
    const char *classes_b[] = { "dog", "cat" };
    fake.next = make_install("model_b", PP_TYPE_OD, classes_b, 2);
    fake.fail_prepare = 1;
    run_reload(&state, &fake);

    CHECK(fake.unload_calls == 1);
    CHECK(fake.prepare_calls == 1);
    CHECK(video_ai_active_model_generation(&state) == 1u);
    CHECK(video_ai_active_model_is_loaded(&state) == 0u);
    CHECK(video_ai_active_model_class_count(&state) == 0u);
    char buf[NN_CLASS_NAME_MAX];
    CHECK(video_ai_active_model_class_name(&state, 0, buf, sizeof(buf)) == -1);

    fake.fail_prepare = 0;
    run_reload(&state, &fake);
    CHECK(video_ai_active_model_is_loaded(&state) == 1u);
    CHECK(video_ai_active_model_generation(&state) == 2u);
    CHECK(video_ai_active_model_class_count(&state) == 2u);

    video_ai_active_model_state_t state2;
    video_ai_active_model_reset(&state2);
    video_ai_active_model_commit(&state2, &a);

    fake_reload_t fake_unload_fail;
    memset(&fake_unload_fail, 0, sizeof(fake_unload_fail));
    fake_unload_fail.fail_unload = 1;
    fake_unload_fail.next = make_install("model_b", PP_TYPE_OD, classes_b, 2);
    run_reload(&state2, &fake_unload_fail);

    CHECK(fake_unload_fail.prepare_calls == 0);
    CHECK(video_ai_active_model_is_loaded(&state2) == 1u);
    CHECK(video_ai_active_model_generation(&state2) == 1u);
    CHECK(video_ai_active_model_class_count(&state2) == 1u);
    CHECK(video_ai_active_model_class_name(&state2, 0, buf, sizeof(buf)) == 0);
    CHECK_STR(buf, "person");
}

static void case_c_successful_replacement(void)
{
    video_ai_active_model_state_t state;
    video_ai_active_model_reset(&state);

    const char *classes_a[] = { "person", "car" };
    video_ai_active_model_install_t a = make_install("model_a", PP_TYPE_OD, classes_a, 2);
    video_ai_active_model_commit(&state, &a);
    CHECK(video_ai_active_model_generation(&state) == 1u);

    fake_reload_t fake;
    memset(&fake, 0, sizeof(fake));
    const char *classes_b[] = { "worker" };
    fake.next = make_install("model_b", PP_TYPE_OD, classes_b, 1);
    run_reload(&state, &fake);

    CHECK(fake.unload_calls == 1);
    CHECK(fake.prepare_calls == 1);
    CHECK(video_ai_active_model_is_loaded(&state) == 1u);
    CHECK(video_ai_active_model_generation(&state) == 2u);
    CHECK(video_ai_active_model_class_count(&state) == 1u);

    char buf[NN_CLASS_NAME_MAX];
    CHECK(video_ai_active_model_class_name(&state, 0, buf, sizeof(buf)) == 0);
    CHECK_STR(buf, "worker");
    CHECK(video_ai_active_model_class_name(&state, 1, buf, sizeof(buf)) == -1);

    uint8_t loaded = 0;
    uint32_t generation = 0;
    video_ai_active_model_install_t view;
    video_ai_active_model_view(&state, &loaded, &generation, &view);
    CHECK(loaded == 1u);
    CHECK(generation == 2u);
    CHECK_STR(view.name, "model_b");
    CHECK_STR(view.classes.names[0], "worker");
}

static void case_d_unload(void)
{
    video_ai_active_model_state_t state;
    video_ai_active_model_reset(&state);

    const char *classes[] = { "person", "car" };
    video_ai_active_model_install_t a = make_install("model_a", PP_TYPE_OD, classes, 2);
    video_ai_active_model_commit(&state, &a);
    CHECK(video_ai_active_model_is_loaded(&state) == 1u);

    video_ai_active_model_uninstall(&state);

    CHECK(video_ai_active_model_is_loaded(&state) == 0u);
    CHECK(video_ai_active_model_class_count(&state) == 0u);
    char buf[NN_CLASS_NAME_MAX];
    CHECK(video_ai_active_model_class_name(&state, 0, buf, sizeof(buf)) == -1);
    CHECK(video_ai_active_model_class_name(&state, 1, buf, sizeof(buf)) == -1);

    uint8_t loaded = 1;
    uint32_t generation = 0;
    video_ai_active_model_install_t view;
    memset(&view, 0xAA, sizeof(view));
    video_ai_active_model_view(&state, &loaded, &generation, &view);
    CHECK(loaded == 0u);
    CHECK_STR(view.name, "");
    CHECK(view.result_type == PP_TYPE_NONE);
    CHECK(view.classes.count == 0u);
}

static void case_e_invalid_class_metadata(void)
{
    video_ai_active_model_state_t state;
    video_ai_active_model_reset(&state);

    const char *classes_a[] = { "person", "car" };
    video_ai_active_model_install_t a = make_install("model_a", PP_TYPE_OD, classes_a, 2);
    video_ai_active_model_commit(&state, &a);
    CHECK(video_ai_active_model_generation(&state) == 1u);

    video_ai_active_model_install_t bad = make_install("model_bad", PP_TYPE_OD, NULL, 0);
    video_ai_active_model_commit(&state, &bad);

    CHECK(video_ai_active_model_is_loaded(&state) == 1u);
    CHECK(video_ai_active_model_generation(&state) == 2u);
    CHECK(video_ai_active_model_class_count(&state) == 0u);
    char buf[NN_CLASS_NAME_MAX];
    CHECK(video_ai_active_model_class_name(&state, 0, buf, sizeof(buf)) == -1);
    CHECK_STR(state.active.name, "model_bad");
    CHECK_STR(state.active.model_type, "OBJECT_DETECTION");

    video_ai_active_model_uninstall(&state);
    CHECK(video_ai_active_model_generation(&state) == 2u);
    video_ai_active_model_commit(&state, &a);
    CHECK(video_ai_active_model_class_count(&state) == 2u);
    CHECK(video_ai_active_model_generation(&state) == 3u);
}

static void case_null_and_reset_safety(void)
{
    video_ai_active_model_state_t state;
    video_ai_active_model_reset(&state);

    const char *classes[] = { "person" };
    video_ai_active_model_install_t a = make_install("model_a", PP_TYPE_OD, classes, 1);
    video_ai_active_model_commit(NULL, &a);
    video_ai_active_model_commit(&state, NULL);
    video_ai_active_model_uninstall(NULL);
    video_ai_active_model_reset(NULL);
    CHECK(video_ai_active_model_generation(&state) == 0u);

    CHECK(video_ai_active_model_reload(NULL, NULL) == AICAM_ERROR_INVALID_PARAM);
    video_ai_active_model_reload_ops_t ops = { .user = NULL, .unload_active = fake_unload_active, .prepare_install = fake_prepare_install };
    CHECK(video_ai_active_model_reload(&state, &ops) == AICAM_ERROR_INVALID_PARAM);
    video_ai_active_model_reload_ops_t partial_ops = { .user = NULL, .unload_active = fake_unload_active, .prepare_install = NULL };
    CHECK(video_ai_active_model_reload(&state, &partial_ops) == AICAM_ERROR_INVALID_PARAM);
    CHECK(video_ai_active_model_generation(&state) == 0u);

    video_ai_active_model_commit(&state, &a);
    video_ai_active_model_reset(&state);
    CHECK(video_ai_active_model_is_loaded(&state) == 0u);
    CHECK(video_ai_active_model_generation(&state) == 0u);
    CHECK(video_ai_active_model_class_count(&state) == 0u);

    CHECK(video_ai_active_model_is_loaded(NULL) == 0u);
    CHECK(video_ai_active_model_generation(NULL) == 0u);
    CHECK(video_ai_active_model_class_count(NULL) == 0u);
    CHECK(video_ai_active_model_class_name(NULL, 0, NULL, 0) == -1);
}

int main(void)
{
    case_a_initial_install();
    case_b_failed_reload();
    case_c_successful_replacement();
    case_d_unload();
    case_e_invalid_class_metadata();
    case_null_and_reset_safety();

    if (g_failures != 0) {
        printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all video ai active model tests passed\n");
    return 0;
}
