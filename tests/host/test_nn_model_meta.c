#include <stdio.h>
#include <string.h>
#include "nn_model_meta.h"

static int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);         \
            g_failures++;                                                 \
        }                                                                 \
    } while (0)

#define CHECK_STR(actual, expected)                                       \
    do {                                                                  \
        const char *a_ = (actual);                                        \
        const char *e_ = (expected);                                      \
        if (!a_ || strcmp(a_, e_) != 0) {                                 \
            printf("FAIL %s:%d \"%s\" != \"%s\"\n", __FILE__, __LINE__,   \
                   a_ ? a_ : "(null)", e_);                               \
            g_failures++;                                                 \
        }                                                                 \
    } while (0)

static const char *VALID_OD =
    "{"
    "\"model_info\":{\"name\":\"test_od\",\"version\":\"1.0\",\"type\":\"OBJECT_DETECTION\"},"
    "\"postprocess_type\":\"pp_od_yolo_v11_ui\","
    "\"postprocess_params\":{\"num_classes\":2,\"class_names\":[\"person\",\"car\"]}"
    "}";

static const char *VALID_OD_ALT =
    "{"
    "\"model_info\":{\"name\":\"test_od_b\",\"version\":\"2.0\",\"type\":\"OBJECT_DETECTION\"},"
    "\"postprocess_type\":\"pp_od_yolo_v8_ui\","
    "\"postprocess_params\":{\"num_classes\":1,\"class_names\":[\"worker\"]}"
    "}";

static void case_valid_od_metadata(void)
{
    nn_config_meta_t meta;
    CHECK(nn_parse_model_config(VALID_OD, &meta) == 0);
    CHECK_STR(meta.model_type, "OBJECT_DETECTION");
    CHECK(meta.num_classes == 2);
    CHECK(meta.classes.count == 2);

    char buf[NN_CLASS_NAME_MAX];
    CHECK(nn_class_list_get(&meta.classes, 0, buf, sizeof(buf)) == 0);
    CHECK_STR(buf, "person");
    CHECK(nn_class_list_get(&meta.classes, 1, buf, sizeof(buf)) == 0);
    CHECK_STR(buf, "car");
}

static void case_class_lookup_out_of_range(void)
{
    nn_config_meta_t meta;
    CHECK(nn_parse_model_config(VALID_OD, &meta) == 0);

    char buf[NN_CLASS_NAME_MAX];
    CHECK(nn_class_list_get(&meta.classes, 2, buf, sizeof(buf)) == -1);
    CHECK(nn_class_list_get(&meta.classes, 96, buf, sizeof(buf)) == -1);
    CHECK(nn_class_list_get(&meta.classes, 65535, buf, sizeof(buf)) == -1);
    CHECK(nn_class_list_get(&meta.classes, 0, NULL, sizeof(buf)) == -1);
    CHECK(nn_class_list_get(&meta.classes, 0, buf, 0) == -1);
    CHECK(nn_class_list_get(NULL, 0, buf, sizeof(buf)) == -1);

    char small[4];
    CHECK(nn_class_list_get(&meta.classes, 0, small, sizeof(small)) == -1);
}

static void case_unload_clears_metadata(void)
{
    nn_config_meta_t meta;
    CHECK(nn_parse_model_config(VALID_OD, &meta) == 0);
    CHECK(meta.classes.count == 2);

    memset(&meta.classes, 0, sizeof(meta.classes));

    char buf[NN_CLASS_NAME_MAX];
    CHECK(nn_class_list_get(&meta.classes, 0, buf, sizeof(buf)) == -1);
    CHECK(meta.classes.count == 0);
}

static void case_reload_new_model_independent_lists(void)
{
    nn_config_meta_t first;
    CHECK(nn_parse_model_config(VALID_OD, &first) == 0);

    nn_config_meta_t second;
    CHECK(nn_parse_model_config(VALID_OD_ALT, &second) == 0);

    char buf[NN_CLASS_NAME_MAX];
    CHECK_STR(first.model_type, "OBJECT_DETECTION");
    CHECK_STR(second.model_type, "OBJECT_DETECTION");
    CHECK(nn_class_list_get(&first.classes, 0, buf, sizeof(buf)) == 0);
    CHECK_STR(buf, "person");
    CHECK(nn_class_list_get(&second.classes, 0, buf, sizeof(buf)) == 0);
    CHECK_STR(buf, "worker");
}

static void case_invalid_class_metadata(void)
{
    nn_config_meta_t meta;

    CHECK(nn_parse_model_config(NULL, &meta) == -1);
    CHECK(nn_parse_model_config(VALID_OD, NULL) == -1);

    const char *zero_classes =
        "{\"model_info\":{\"type\":\"OBJECT_DETECTION\"},"
        "\"postprocess_params\":{\"num_classes\":0,\"class_names\":[]}}";
    CHECK(nn_parse_model_config(zero_classes, &meta) == 0);
    CHECK(meta.num_classes == 0);
    CHECK(meta.classes.count == 0);

    const char *count_mismatch =
        "{\"model_info\":{\"type\":\"OBJECT_DETECTION\"},"
        "\"postprocess_params\":{\"num_classes\":2,\"class_names\":[\"person\",\"car\",\"bus\"]}}";
    CHECK(nn_parse_model_config(count_mismatch, &meta) == 0);
    CHECK(meta.num_classes == 0);
    CHECK(meta.classes.count == 0);

    const char *missing_names =
        "{\"model_info\":{\"type\":\"OBJECT_DETECTION\"},"
        "\"postprocess_params\":{\"num_classes\":2}}";
    CHECK(nn_parse_model_config(missing_names, &meta) == 0);
    CHECK(meta.num_classes == 0);
    CHECK(meta.classes.count == 0);

    const char *missing_params =
        "{\"model_info\":{\"type\":\"OBJECT_DETECTION\"}}";
    CHECK(nn_parse_model_config(missing_params, &meta) == 0);
    CHECK(meta.num_classes == 0);
    CHECK(meta.classes.count == 0);

    const char *empty_name =
        "{\"model_info\":{\"type\":\"OBJECT_DETECTION\"},"
        "\"postprocess_params\":{\"num_classes\":2,\"class_names\":[\"person\",\"\"]}}";
    CHECK(nn_parse_model_config(empty_name, &meta) == 0);
    CHECK(meta.num_classes == 0);
    CHECK(meta.classes.count == 0);

    char long_name[64];
    memset(long_name, 'a', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    char oversized_name_json[256];
    snprintf(oversized_name_json, sizeof(oversized_name_json),
             "{\"model_info\":{\"type\":\"OBJECT_DETECTION\"},"
             "\"postprocess_params\":{\"num_classes\":1,\"class_names\":[\"%s\"]}}",
             long_name);
    CHECK(nn_parse_model_config(oversized_name_json, &meta) == 0);
    CHECK(meta.num_classes == 0);
    CHECK(meta.classes.count == 0);

    char too_many_classes_json[1024];
    snprintf(too_many_classes_json, sizeof(too_many_classes_json),
             "{\"model_info\":{\"type\":\"OBJECT_DETECTION\"},"
             "\"postprocess_params\":{\"num_classes\":%d,\"class_names\":[", NN_MAX_CLASS_COUNT + 1);
    for (int i = 0; i < NN_MAX_CLASS_COUNT + 1; ++i) {
        char item[24];
        snprintf(item, sizeof(item), "%s\"c%d\"", (i > 0) ? "," : "", i);
        strncat(too_many_classes_json, item, sizeof(too_many_classes_json) - strlen(too_many_classes_json) - 1);
    }
    strncat(too_many_classes_json, "]}}", sizeof(too_many_classes_json) - strlen(too_many_classes_json) - 1);
    CHECK(nn_parse_model_config(too_many_classes_json, &meta) == 0);
    CHECK(meta.num_classes == 0);
    CHECK(meta.classes.count == 0);

    const char *non_string_name =
        "{\"model_info\":{\"type\":\"OBJECT_DETECTION\"},"
        "\"postprocess_params\":{\"num_classes\":1,\"class_names\":[7]}}";
    CHECK(nn_parse_model_config(non_string_name, &meta) == 0);
    CHECK(meta.num_classes == 0);
    CHECK(meta.classes.count == 0);

    const char *malformed = "{\"model_info\": {\"type\": ";
    CHECK(nn_parse_model_config(malformed, &meta) == -1);
    CHECK(meta.num_classes == 0);
    CHECK(meta.classes.count == 0);
}

static void case_missing_optional_fields(void)
{
    nn_config_meta_t meta;

    const char *no_type =
        "{\"model_info\":{\"name\":\"m\"},"
        "\"postprocess_params\":{\"num_classes\":1,\"class_names\":[\"person\"]}}";
    CHECK(nn_parse_model_config(no_type, &meta) == 0);
    CHECK_STR(meta.model_type, "");
    CHECK(meta.num_classes == 1);

    const char *legacy =
        "{\"model_info\":{\"name\":\"legacy\",\"version\":\"1.0\"}}";
    CHECK(nn_parse_model_config(legacy, &meta) == 0);
    CHECK_STR(meta.model_type, "");
    CHECK(meta.num_classes == 0);
    CHECK(meta.classes.count == 0);
}

int main(void)
{
    case_valid_od_metadata();
    case_class_lookup_out_of_range();
    case_unload_clears_metadata();
    case_reload_new_model_independent_lists();
    case_invalid_class_metadata();
    case_missing_optional_fields();

    if (g_failures != 0) {
        printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all nn model metadata tests passed\n");
    return 0;
}
