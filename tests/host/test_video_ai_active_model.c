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

static video_ai_active_model_install_t make_install(const char *name, const char *type,
                                                    pp_type_t result_type,
                                                    const char *const *classes, uint16_t count)
{
    video_ai_active_model_install_t install;
    memset(&install, 0, sizeof(install));
    snprintf(install.name, sizeof(install.name), "%s", name);
    snprintf(install.version, sizeof(install.version), "1.0");
    snprintf(install.model_type, sizeof(install.model_type), "%s", type);
    snprintf(install.postprocess_type, sizeof(install.postprocess_type), "%s", name);
    install.result_type = result_type;
    install.classes.count = count;
    for (uint16_t i = 0; i < count; ++i) {
        snprintf(install.classes.names[i], NN_CLASS_NAME_MAX, "%s", classes[i]);
    }
    return install;
}

static void case_a_successful_install(void)
{
    video_ai_active_model_state_t state;
    video_ai_active_model_reset(&state);

    CHECK(video_ai_active_model_is_loaded(&state) == 0u);
    CHECK(video_ai_active_model_generation(&state) == 0u);
    CHECK(video_ai_active_model_class_count(&state) == 0u);

    const char *classes[] = { "person", "car" };
    video_ai_active_model_install_t a = make_install("model_a", "OBJECT_DETECTION",
                                                     PP_TYPE_OD, classes, 2);
    video_ai_active_model_commit(&state, &a);

    CHECK(video_ai_active_model_is_loaded(&state) == 1u);
    CHECK(video_ai_active_model_generation(&state) == 1u);
    CHECK(video_ai_active_model_class_count(&state) == 2u);

    char buf[NN_CLASS_NAME_MAX];
    CHECK(video_ai_active_model_class_name(&state, 0, buf, sizeof(buf)) == 0);
    CHECK_STR(buf, "person");
    CHECK(video_ai_active_model_class_name(&state, 1, buf, sizeof(buf)) == 0);
    CHECK_STR(buf, "car");
    CHECK(video_ai_active_model_class_name(&state, 2, buf, sizeof(buf)) == -1);

    uint8_t loaded = 0;
    uint32_t generation = 0;
    video_ai_active_model_install_t view;
    video_ai_active_model_view(&state, &loaded, &generation, &view);
    CHECK(loaded == 1u);
    CHECK(generation == 1u);
    CHECK_STR(view.name, "model_a");
    CHECK(view.result_type == PP_TYPE_OD);
    CHECK(view.classes.count == 2u);
}

static void case_b_failed_install_publishes_nothing(void)
{
    video_ai_active_model_state_t state;
    video_ai_active_model_reset(&state);

    const char *classes_a[] = { "person" };
    video_ai_active_model_install_t a = make_install("model_a", "OBJECT_DETECTION",
                                                     PP_TYPE_OD, classes_a, 1);
    video_ai_active_model_commit(&state, &a);
    CHECK(video_ai_active_model_generation(&state) == 1u);

    const char *classes_b[] = { "dog", "cat" };
    video_ai_active_model_install_t b = make_install("model_b", "OBJECT_DETECTION",
                                                     PP_TYPE_OD, classes_b, 2);
    video_ai_active_model_commit(&state, NULL);
    (void)b;

    char buf[NN_CLASS_NAME_MAX];
    CHECK(video_ai_active_model_is_loaded(&state) == 1u);
    CHECK(video_ai_active_model_generation(&state) == 1u);
    CHECK(video_ai_active_model_class_count(&state) == 1u);
    CHECK(video_ai_active_model_class_name(&state, 0, buf, sizeof(buf)) == 0);
    CHECK_STR(buf, "person");
    CHECK(video_ai_active_model_class_name(&state, 1, buf, sizeof(buf)) == -1);
}

static void case_c_unload_clears_active_metadata(void)
{
    video_ai_active_model_state_t state;
    video_ai_active_model_reset(&state);

    const char *classes[] = { "person", "car" };
    video_ai_active_model_install_t a = make_install("model_a", "OBJECT_DETECTION",
                                                     PP_TYPE_OD, classes, 2);
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

static void case_d_successful_replacement(void)
{
    video_ai_active_model_state_t state;
    video_ai_active_model_reset(&state);

    const char *classes_a[] = { "person", "car" };
    video_ai_active_model_install_t a = make_install("model_a", "OBJECT_DETECTION",
                                                     PP_TYPE_OD, classes_a, 2);
    video_ai_active_model_commit(&state, &a);
    CHECK(video_ai_active_model_generation(&state) == 1u);

    const char *classes_b[] = { "worker" };
    video_ai_active_model_install_t b = make_install("model_b", "OBJECT_DETECTION",
                                                     PP_TYPE_OD, classes_b, 1);
    video_ai_active_model_commit(&state, &b);

    CHECK(video_ai_active_model_is_loaded(&state) == 1u);
    CHECK(video_ai_active_model_generation(&state) == 2u);
    CHECK(video_ai_active_model_class_count(&state) == 1u);

    char buf[NN_CLASS_NAME_MAX];
    CHECK(video_ai_active_model_class_name(&state, 0, buf, sizeof(buf)) == 0);
    CHECK_STR(buf, "worker");
    CHECK(video_ai_active_model_class_name(&state, 1, buf, sizeof(buf)) == -1);
}

static void case_e_invalid_metadata_not_published(void)
{
    video_ai_active_model_state_t state;
    video_ai_active_model_reset(&state);

    const char *classes_a[] = { "person", "car" };
    video_ai_active_model_install_t a = make_install("model_a", "OBJECT_DETECTION",
                                                     PP_TYPE_OD, classes_a, 2);
    video_ai_active_model_commit(&state, &a);
    CHECK(video_ai_active_model_generation(&state) == 1u);

    video_ai_active_model_install_t bad = make_install("model_bad", "OBJECT_DETECTION",
                                                       PP_TYPE_OD, NULL, 0);
    video_ai_active_model_commit(&state, &bad);

    CHECK(video_ai_active_model_is_loaded(&state) == 1u);
    CHECK(video_ai_active_model_generation(&state) == 2u);
    CHECK(video_ai_active_model_class_count(&state) == 0u);

    char buf[NN_CLASS_NAME_MAX];
    CHECK(video_ai_active_model_class_name(&state, 0, buf, sizeof(buf)) == -1);
    CHECK(video_ai_active_model_is_loaded(&state) == 1u);
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
    video_ai_active_model_install_t a = make_install("model_a", "OBJECT_DETECTION",
                                                     PP_TYPE_OD, classes, 1);
    video_ai_active_model_commit(NULL, &a);
    video_ai_active_model_commit(&state, NULL);
    video_ai_active_model_uninstall(NULL);
    video_ai_active_model_reset(NULL);
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
    case_a_successful_install();
    case_b_failed_install_publishes_nothing();
    case_c_unload_clears_active_metadata();
    case_d_successful_replacement();
    case_e_invalid_metadata_not_published();
    case_null_and_reset_safety();

    if (g_failures != 0) {
        printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all video ai active model tests passed\n");
    return 0;
}
