#include <stdio.h>
#include <string.h>
#include "line_counting_config.h"

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

static void test_canonical_defaults(void)
{
    line_counting_config_t cfg;
    line_counting_config_defaults(&cfg);

    CHECK(cfg.enable == AICAM_FALSE);
    CHECK_STR(cfg.counter_name, "客流统计");
    CHECK_STR(cfg.target_class_name, "person");
    CHECK(cfg.line_x1_permille == 200);
    CHECK(cfg.line_y1_permille == 500);
    CHECK(cfg.line_x2_permille == 800);
    CHECK(cfg.line_y2_permille == 500);
    CHECK(cfg.outside_x_permille == 500);
    CHECK(cfg.outside_y_permille == 200);
    CHECK(cfg.conf_threshold_permille == 250);
    CHECK(cfg.max_dist_permille == 250);
    CHECK(cfg.track_history_k == 8);
    CHECK(cfg.max_miss == 5);
    CHECK(cfg.k_confirm == 5);
    CHECK(cfg.window_minutes == 5);
    CHECK(cfg.mqtt_report_enable == AICAM_TRUE);
    CHECK(cfg.webhook_report_enable == AICAM_FALSE);
    CHECK(cfg.tracks_report_enable == AICAM_TRUE);
    CHECK(cfg.heat_grid_enable == AICAM_FALSE);
    CHECK(cfg.backlog_capacity == 24);
}

static void test_validation_accepts_defaults(void)
{
    line_counting_config_t cfg;
    line_counting_config_defaults(&cfg);
    CHECK(line_counting_config_is_valid(&cfg) == AICAM_TRUE);
}

static void test_validation_rejects_invalid(void)
{
    line_counting_config_t cfg;
    line_counting_config_defaults(&cfg);

    cfg.line_x2_permille = cfg.line_x1_permille;
    cfg.line_y2_permille = cfg.line_y1_permille;
    CHECK(line_counting_config_is_valid(&cfg) == AICAM_FALSE);

    line_counting_config_defaults(&cfg);
    cfg.line_x1_permille = 1001;
    CHECK(line_counting_config_is_valid(&cfg) == AICAM_FALSE);

    line_counting_config_defaults(&cfg);
    cfg.conf_threshold_permille = 1001;
    CHECK(line_counting_config_is_valid(&cfg) == AICAM_FALSE);

    line_counting_config_defaults(&cfg);
    cfg.max_dist_permille = 0;
    CHECK(line_counting_config_is_valid(&cfg) == AICAM_FALSE);

    line_counting_config_defaults(&cfg);
    cfg.track_history_k = 3;
    CHECK(line_counting_config_is_valid(&cfg) == AICAM_FALSE);

    line_counting_config_defaults(&cfg);
    cfg.track_history_k = 17;
    CHECK(line_counting_config_is_valid(&cfg) == AICAM_FALSE);

    line_counting_config_defaults(&cfg);
    cfg.max_miss = 0;
    CHECK(line_counting_config_is_valid(&cfg) == AICAM_FALSE);

    line_counting_config_defaults(&cfg);
    cfg.k_confirm = 0;
    CHECK(line_counting_config_is_valid(&cfg) == AICAM_FALSE);

    line_counting_config_defaults(&cfg);
    cfg.window_minutes = 0;
    CHECK(line_counting_config_is_valid(&cfg) == AICAM_FALSE);

    line_counting_config_defaults(&cfg);
    cfg.window_minutes = 1441;
    CHECK(line_counting_config_is_valid(&cfg) == AICAM_FALSE);

    line_counting_config_defaults(&cfg);
    cfg.backlog_capacity = 0;
    CHECK(line_counting_config_is_valid(&cfg) == AICAM_FALSE);

    line_counting_config_defaults(&cfg);
    cfg.backlog_capacity = LC_BACKLOG_CAPACITY_MAX + 1;
    CHECK(line_counting_config_is_valid(&cfg) == AICAM_FALSE);

    line_counting_config_defaults(&cfg);
    cfg.counter_name[0] = '\0';
    CHECK(line_counting_config_is_valid(&cfg) == AICAM_FALSE);

    line_counting_config_defaults(&cfg);
    cfg.target_class_name[0] = '\0';
    CHECK(line_counting_config_is_valid(&cfg) == AICAM_FALSE);

    CHECK(line_counting_config_is_valid(NULL) == AICAM_FALSE);
}

static void test_migration_maps_legacy_fields(void)
{
    people_counting_config_t legacy;
    memset(&legacy, 0, sizeof(legacy));
    legacy.enable = AICAM_TRUE;
    legacy.line_x1_permille = 100;
    legacy.line_y1_permille = 300;
    legacy.line_x2_permille = 900;
    legacy.line_y2_permille = 700;
    legacy.outside_x_permille = 400;
    legacy.outside_y_permille = 100;
    legacy.conf_threshold_permille = 300;
    legacy.max_dist_permille = 200;
    snprintf(legacy.target_class_name, sizeof(legacy.target_class_name), "worker");
    snprintf(legacy.model_name, sizeof(legacy.model_name), "some_model_name");
    snprintf(legacy.model_pp_type, sizeof(legacy.model_pp_type), "pp_od_some_pp");
    legacy.track_history_k = 6;
    legacy.max_miss = 4;
    legacy.k_confirm = 3;
    legacy.window_minutes = 10;
    legacy.mqtt_report_enable = AICAM_FALSE;
    legacy.webhook_report_enable = AICAM_TRUE;
    legacy.tracks_report_enable = AICAM_FALSE;
    legacy.heat_grid_enable = AICAM_TRUE;
    legacy.backlog_capacity = 32;

    line_counting_config_t canonical;
    line_counting_config_map_from_people(&legacy, &canonical);

    CHECK(canonical.enable == AICAM_TRUE);
    CHECK(canonical.line_x1_permille == 100);
    CHECK(canonical.line_y1_permille == 300);
    CHECK(canonical.line_x2_permille == 900);
    CHECK(canonical.line_y2_permille == 700);
    CHECK(canonical.outside_x_permille == 400);
    CHECK(canonical.outside_y_permille == 100);
    CHECK(canonical.conf_threshold_permille == 300);
    CHECK(canonical.max_dist_permille == 200);
    CHECK_STR(canonical.target_class_name, "worker");
    CHECK(canonical.track_history_k == 6);
    CHECK(canonical.max_miss == 4);
    CHECK(canonical.k_confirm == 3);
    CHECK(canonical.window_minutes == 10);
    CHECK(canonical.mqtt_report_enable == AICAM_FALSE);
    CHECK(canonical.webhook_report_enable == AICAM_TRUE);
    CHECK(canonical.tracks_report_enable == AICAM_FALSE);
    CHECK(canonical.heat_grid_enable == AICAM_TRUE);
    CHECK(canonical.backlog_capacity == 32);
}

static void test_migration_preserves_counter_name_default(void)
{
    people_counting_config_t legacy;
    memset(&legacy, 0, sizeof(legacy));
    legacy.enable = AICAM_TRUE;
    legacy.line_x1_permille = 100;

    line_counting_config_t canonical;
    line_counting_config_map_from_people(&legacy, &canonical);
    CHECK_STR(canonical.counter_name, "客流统计");
}

static void test_map_null_safety(void)
{
    line_counting_config_t canonical;
    people_counting_config_t legacy;
    memset(&legacy, 0, sizeof(legacy));

    line_counting_config_map_from_people(&legacy, NULL);
    line_counting_config_map_from_people(NULL, &canonical);
    CHECK(canonical.line_x1_permille == 0);
}

int main(void)
{
    test_canonical_defaults();
    test_validation_accepts_defaults();
    test_validation_rejects_invalid();
    test_migration_maps_legacy_fields();
    test_migration_preserves_counter_name_default();
    test_map_null_safety();

    if (g_failures != 0) {
        printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all line counting config tests passed\n");
    return 0;
}
