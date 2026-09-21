#include "line_counting.h"
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);            \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

#define CHECK_STR(a, b)                                                      \
    do {                                                                     \
        const char *_a = (a);                                                \
        const char *_b = (b);                                                \
        if (!_a || !_b || strcmp(_a, _b) != 0) {                             \
            printf("FAIL %s:%d \"%s\" != \"%s\"\n", __FILE__, __LINE__,      \
                   _a ? _a : "(null)", _b ? _b : "(null)");                  \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

typedef struct {
    lc_runtime_model_info_t info;
    const char *classes[8];
    uint16_t n_classes;
    uint32_t now;
    uint32_t total_in;
    uint32_t total_out;
    int save_calls;
    int load_calls;
    int class_calls;
    aicam_result_t load_ret;
    aicam_result_t save_ret;
    aicam_result_t class_ret;
    aicam_result_t info_ret;
} fake_t;

static aicam_result_t fk_get_model_info(void *user, lc_runtime_model_info_t *info) {
    fake_t *f = (fake_t *)user;
    if (f->info_ret != AICAM_OK) return f->info_ret;
    *info = f->info;
    return AICAM_OK;
}

static aicam_result_t fk_get_class_name(void *user, uint16_t idx, char *buf, size_t n) {
    fake_t *f = (fake_t *)user;
    f->class_calls++;
    if (f->class_ret != AICAM_OK) return f->class_ret;
    if (idx >= f->n_classes) return AICAM_ERROR_INVALID_PARAM;
    snprintf(buf, n, "%s", f->classes[idx]);
    return AICAM_OK;
}

static uint32_t fk_now(void *user) {
    return ((fake_t *)user)->now;
}

static aicam_result_t fk_load_totals(void *user, uint32_t *total_in, uint32_t *total_out) {
    fake_t *f = (fake_t *)user;
    f->load_calls++;
    if (f->load_ret != AICAM_OK) return f->load_ret;
    *total_in = f->total_in;
    *total_out = f->total_out;
    return AICAM_OK;
}

static aicam_result_t fk_save_totals(void *user, uint32_t total_in, uint32_t total_out) {
    fake_t *f = (fake_t *)user;
    f->save_calls++;
    if (f->save_ret != AICAM_OK) return f->save_ret;
    f->total_in = total_in;
    f->total_out = total_out;
    return AICAM_OK;
}

static void fake_init(fake_t *f) {
    memset(f, 0, sizeof(*f));
    f->info.loaded = AICAM_TRUE;
    f->info.generation = 7;
    f->info.result_type = PP_TYPE_OD;
    f->info.num_classes = 1;
    f->classes[0] = "person";
    f->n_classes = 1;
    f->load_ret = AICAM_ERROR_NOT_FOUND;
}

static void ops_init(lc_app_ops_t *ops, fake_t *f) {
    memset(ops, 0, sizeof(*ops));
    ops->user = f;
    ops->get_model_info = fk_get_model_info;
    ops->get_class_name = fk_get_class_name;
    ops->now_ms = fk_now;
    ops->load_totals = fk_load_totals;
    ops->save_totals = fk_save_totals;
}

static void cfg_enabled(line_counting_config_t *cfg) {
    line_counting_config_defaults(cfg);
    cfg->enable = AICAM_TRUE;
    snprintf(cfg->target_class_name, sizeof(cfg->target_class_name), "person");
}

static void frame_empty(lc_frame_input_t *fr) {
    fr->result_type = PP_TYPE_OD;
    fr->nb_detect = 0;
    fr->detects = NULL;
}

static void run_frames(lc_app_t *app, lc_frame_input_t *fr, int n) {
    for (int i = 0; i < n; i++) {
        lc_app_on_ai_result(app, fr, (uint32_t)(1000 + i * 100));
    }
}

static void test_init_disabled_loads_totals(void) {
    fake_t f;
    fake_init(&f);
    f.load_ret = AICAM_OK;
    f.total_in = 100;
    f.total_out = 50;
    lc_app_ops_t ops;
    ops_init(&ops, &f);
    line_counting_config_t cfg;
    line_counting_config_defaults(&cfg);

    lc_app_t app;
    lc_app_init(&app, &ops, &cfg);

    line_counting_status_t st;
    lc_app_get_status(&app, &st);
    CHECK(st.state == LC_STATE_DISABLED);
    line_counting_stats_t stats;
    lc_app_get_stats(&app, &stats);
    CHECK(stats.total_in == 100);
    CHECK(stats.total_out == 50);
    CHECK(stats.window_in == 0 && stats.window_out == 0);
    lc_app_reset(&app, 0);
}

static void test_running_od_binding(void) {
    fake_t f;
    fake_init(&f);
    lc_app_ops_t ops;
    ops_init(&ops, &f);
    line_counting_config_t cfg;
    cfg_enabled(&cfg);

    lc_app_t app;
    lc_app_init(&app, &ops, &cfg);

    lc_frame_input_t fr;
    frame_empty(&fr);
    run_frames(&app, &fr, 1);

    line_counting_status_t st;
    lc_app_get_status(&app, &st);
    CHECK(st.state == LC_STATE_RUNNING);
    CHECK(st.binding.target_class_index == 0);
    CHECK_STR(st.binding.target_class_name, "person");
    CHECK(st.binding.generation == 7);

    int calls_after_bind = f.class_calls;
    run_frames(&app, &fr, 10);
    CHECK(f.class_calls == calls_after_bind);
    lc_app_reset(&app, 0);
}

static void test_unsupported_model(void) {
    fake_t f;
    fake_init(&f);
    f.info.result_type = PP_TYPE_SEG;
    lc_app_ops_t ops;
    ops_init(&ops, &f);
    line_counting_config_t cfg;
    cfg_enabled(&cfg);

    lc_app_t app;
    lc_app_init(&app, &ops, &cfg);

    lc_frame_input_t fr;
    frame_empty(&fr);
    run_frames(&app, &fr, 3);

    line_counting_status_t st;
    lc_app_get_status(&app, &st);
    CHECK(st.state == LC_STATE_UNSUPPORTED_MODEL);
    CHECK_STR(st.binding.target_class_name, "");
    line_counting_stats_t stats;
    lc_app_get_stats(&app, &stats);
    CHECK(stats.total_in == 0 && stats.window_in == 0);

    lc_window_close_t closed;
    memset(&closed, 0, sizeof(closed));
    CHECK(lc_app_tick_window(&app, 300000, &closed, NULL, NULL));
    CHECK(closed.summary.in == 0 && closed.summary.out == 0);
    lc_app_reset(&app, 0);
}

static void test_target_class_invalid_and_recovery(void) {
    fake_t f;
    fake_init(&f);
    f.classes[0] = "car";
    lc_app_ops_t ops;
    ops_init(&ops, &f);
    line_counting_config_t cfg;
    cfg_enabled(&cfg);

    lc_app_t app;
    lc_app_init(&app, &ops, &cfg);

    lc_frame_input_t fr;
    frame_empty(&fr);
    run_frames(&app, &fr, 1);

    line_counting_status_t st;
    lc_app_get_status(&app, &st);
    CHECK(st.state == LC_STATE_TARGET_CLASS_INVALID);
    CHECK(st.binding.target_class_index == -1);
    lc_line_cross_t chk;
    (void)chk;
    CHECK_STR(app.cfg.target_class_name, "person");

    f.now = 30000;
    CHECK(lc_app_tick_window(&app, 30000 + 5u * 60u * 1000u, NULL, NULL, NULL));

    f.info.generation = 8;
    f.classes[0] = "person";
    run_frames(&app, &fr, 1);
    lc_app_get_status(&app, &st);
    CHECK(st.state == LC_STATE_RUNNING);
    line_counting_stats_t stats;
    lc_app_get_stats(&app, &stats);
    CHECK(stats.window_in == 0 && stats.total_in == 0);
    lc_app_reset(&app, 0);
}

static lc_frame_input_t *frame_one_at(lc_frame_input_t *fr, lc_det_t *det, float x, float y) {
    det->x = x;
    det->y = y;
    det->w = 0.1f;
    det->h = 0.1f;
    det->conf = 0.9f;
    det->class_name = "person";
    fr->result_type = PP_TYPE_OD;
    fr->nb_detect = 1;
    fr->detects = det;
    return fr;
}

static void test_zero_detection_frames_reach_engine(void) {
    fake_t f;
    fake_init(&f);
    lc_app_ops_t ops;
    ops_init(&ops, &f);
    line_counting_config_t cfg;
    cfg_enabled(&cfg);

    lc_app_t app;
    lc_app_init(&app, &ops, &cfg);

    lc_frame_input_t fr;
    lc_det_t det;
    frame_one_at(&fr, &det, 0.5f, 0.35f);
    run_frames(&app, &fr, 1);

    lc_frame_input_t empty;
    frame_empty(&empty);
    run_frames(&app, &empty, 20);

    line_counting_status_t st;
    lc_app_get_status(&app, &st);
    CHECK(st.state == LC_STATE_RUNNING);
    CHECK(st.tracker_active == 0);
    line_counting_stats_t stats;
    lc_app_get_stats(&app, &stats);
    CHECK(stats.total_in == 0 && stats.total_out == 0);
    lc_app_reset(&app, 0);
}

static void test_model_change_preserves_business_clears_transient(void) {
    fake_t f;
    fake_init(&f);
    lc_app_ops_t ops;
    ops_init(&ops, &f);
    line_counting_config_t cfg;
    cfg_enabled(&cfg);
    cfg.max_dist_permille = 500;
    cfg.k_confirm = 2;

    lc_app_t app;
    lc_app_init(&app, &ops, &cfg);

    lc_frame_input_t fr;
    lc_det_t det;
    frame_one_at(&fr, &det, 0.5f, 0.35f);
    run_frames(&app, &fr, 3);
    det.y = 0.55f;
    run_frames(&app, &fr, 3);

    line_counting_stats_t before;
    lc_app_get_stats(&app, &before);
    CHECK(before.total_in + before.total_out > 0);

    f.info.generation = 8;
    lc_frame_input_t empty;
    frame_empty(&empty);
    run_frames(&app, &empty, 1);

    line_count_event_t evs[LC_EVENTS_RING_CAPACITY];
    CHECK(lc_app_get_events(&app, evs, LC_EVENTS_RING_CAPACITY) == 0);

    line_counting_stats_t after;
    lc_app_get_stats(&app, &after);
    CHECK(after.window_in == before.window_in);
    CHECK(after.window_out == before.window_out);
    CHECK(after.total_in == before.total_in);
    CHECK(after.total_out == before.total_out);
    CHECK(after.window_start_ms == before.window_start_ms);

    line_counting_status_t st;
    lc_app_get_status(&app, &st);
    CHECK(st.state == LC_STATE_RUNNING);
    lc_app_reset(&app, 0);
}

static void test_events_ring_bounded(void) {
    fake_t f;
    fake_init(&f);
    lc_app_ops_t ops;
    ops_init(&ops, &f);
    line_counting_config_t cfg;
    cfg_enabled(&cfg);
    cfg.max_dist_permille = 500;
    cfg.k_confirm = 2;

    lc_app_t app;
    lc_app_init(&app, &ops, &cfg);

    lc_frame_input_t fr;
    lc_det_t det;
    uint32_t ts = 1000;
    float y = 0.35f;
    for (int i = 0; i < 120; i++) {
        frame_one_at(&fr, &det, 0.5f, y);
        lc_app_on_ai_result(&app, &fr, ts);
        ts += 100;
        y = (y < 0.5f) ? 0.55f : 0.35f;
    }

    line_counting_stats_t stats;
    lc_app_get_stats(&app, &stats);
    CHECK(stats.total_in + stats.total_out >= 60);

    line_count_event_t evs[LC_EVENTS_RING_CAPACITY];
    uint16_t n = lc_app_get_events(&app, evs, LC_EVENTS_RING_CAPACITY);
    CHECK(n == LC_EVENTS_RING_CAPACITY);
    for (uint16_t i = 1; i < n; i++) {
        CHECK(evs[i - 1].sequence > evs[i].sequence);
    }
    lc_app_reset(&app, 0);
}

static void test_window_close(void) {
    fake_t f;
    fake_init(&f);
    lc_app_ops_t ops;
    ops_init(&ops, &f);
    line_counting_config_t cfg;
    cfg_enabled(&cfg);
    cfg.window_minutes = 1;

    lc_app_t app;
    lc_app_init(&app, &ops, &cfg);

    lc_frame_input_t fr;
    frame_empty(&fr);
    run_frames(&app, &fr, 1);

    f.now = 30000;
    CHECK(!lc_app_tick_window(&app, 30000, NULL, NULL, NULL));

    f.now = 60000;
    lc_window_close_t closed;
    memset(&closed, 0xAA, sizeof(closed));
    CHECK(lc_app_tick_window(&app, 60000, &closed, NULL, NULL));
    CHECK(closed.summary.start_ms == 0);
    CHECK(closed.summary.end_ms == 60000);
    CHECK(closed.summary.in == 0 && closed.summary.out == 0);

    line_counting_stats_t stats;
    lc_app_get_stats(&app, &stats);
    CHECK(stats.window_in == 0 && stats.window_out == 0);
    CHECK(stats.window_start_ms == 60000);
    CHECK(stats.total_in == 0 && stats.total_out == 0);
    lc_app_reset(&app, 0);
}

static void test_disable_closes_window_and_reenable_fresh(void) {
    fake_t f;
    fake_init(&f);
    lc_app_ops_t ops;
    ops_init(&ops, &f);
    line_counting_config_t cfg;
    cfg_enabled(&cfg);
    cfg.max_dist_permille = 500;
    cfg.k_confirm = 2;

    lc_app_t app;
    lc_app_init(&app, &ops, &cfg);

    lc_frame_input_t fr;
    lc_det_t det;
    frame_one_at(&fr, &det, 0.5f, 0.35f);
    run_frames(&app, &fr, 3);
    det.y = 0.55f;
    run_frames(&app, &fr, 3);

    line_counting_stats_t before;
    lc_app_get_stats(&app, &before);
    CHECK(before.window_in + before.window_out > 0);

    f.now = 120000;
    line_counting_config_t off;
    off = cfg;
    off.enable = AICAM_FALSE;
    lc_window_summary_t closed;
    CHECK(lc_app_apply_config(&app, &off, &closed) == AICAM_OK);
    CHECK(closed.end_ms == 120000);
    CHECK(closed.in == before.window_in);
    CHECK(closed.out == before.window_out);

    line_counting_status_t st;
    lc_app_get_status(&app, &st);
    CHECK(st.state == LC_STATE_DISABLED);
    CHECK(lc_app_get_events(&app, NULL, 0) == 0);
    CHECK(st.tracker_active == 0);
    line_counting_stats_t stats;
    lc_app_get_stats(&app, &stats);
    CHECK(stats.total_in == before.total_in);
    CHECK(stats.total_out == before.total_out);

    f.now = 180000;
    CHECK(!lc_app_tick_window(&app, 180000, NULL, NULL, NULL));

    lc_window_summary_t reopen;
    CHECK(lc_app_apply_config(&app, &cfg, &reopen) == AICAM_OK);
    lc_app_get_stats(&app, &stats);
    CHECK(stats.window_start_ms == 180000);
    CHECK(stats.window_in == 0 && stats.window_out == 0);
    CHECK(stats.total_in == before.total_in);
    lc_app_reset(&app, 0);
}

static void test_target_change_resets_session(void) {
    fake_t f;
    fake_init(&f);
    f.n_classes = 2;
    f.info.num_classes = 2;
    f.classes[0] = "person";
    f.classes[1] = "car";
    lc_app_ops_t ops;
    ops_init(&ops, &f);
    line_counting_config_t cfg;
    cfg_enabled(&cfg);
    cfg.max_dist_permille = 500;
    cfg.k_confirm = 2;

    lc_app_t app;
    lc_app_init(&app, &ops, &cfg);

    lc_frame_input_t fr;
    lc_det_t det;
    frame_one_at(&fr, &det, 0.5f, 0.35f);
    run_frames(&app, &fr, 3);
    det.y = 0.55f;
    run_frames(&app, &fr, 3);
    line_counting_stats_t before;
    lc_app_get_stats(&app, &before);
    CHECK(before.total_in + before.total_out > 0);
    int saves_before = f.save_calls;

    f.now = 200000;
    line_counting_config_t changed;
    changed = cfg;
    snprintf(changed.target_class_name, sizeof(changed.target_class_name), "car");
    CHECK(lc_app_apply_config(&app, &changed, NULL) == AICAM_OK);
    CHECK(f.save_calls == saves_before + 1);
    CHECK(f.total_in == 0 && f.total_out == 0);

    line_count_event_t evs[4];
    CHECK(lc_app_get_events(&app, evs, 4) == 0);
    line_counting_stats_t stats;
    lc_app_get_stats(&app, &stats);
    CHECK(stats.total_in == 0 && stats.total_out == 0);
    CHECK(stats.window_in == 0 && stats.window_out == 0);
    CHECK(stats.window_start_ms == 200000);
    line_counting_status_t st;
    lc_app_get_status(&app, &st);
    CHECK(st.state == LC_STATE_RUNNING);
    CHECK(st.binding.target_class_index == 1);
    lc_app_reset(&app, 0);
}

static void test_counter_name_change_keeps_stats(void) {
    fake_t f;
    fake_init(&f);
    lc_app_ops_t ops;
    ops_init(&ops, &f);
    line_counting_config_t cfg;
    cfg_enabled(&cfg);
    cfg.max_dist_permille = 500;
    cfg.k_confirm = 2;

    lc_app_t app;
    lc_app_init(&app, &ops, &cfg);

    lc_frame_input_t fr;
    lc_det_t det;
    frame_one_at(&fr, &det, 0.5f, 0.35f);
    run_frames(&app, &fr, 3);
    det.y = 0.55f;
    run_frames(&app, &fr, 3);
    line_counting_stats_t before;
    lc_app_get_stats(&app, &before);
    CHECK(before.total_in + before.total_out > 0);
    int saves_before = f.save_calls;

    line_counting_config_t renamed;
    renamed = cfg;
    snprintf(renamed.counter_name, sizeof(renamed.counter_name), "北门");
    CHECK(lc_app_apply_config(&app, &renamed, NULL) == AICAM_OK);
    CHECK(f.save_calls == saves_before);

    line_counting_stats_t after;
    lc_app_get_stats(&app, &after);
    CHECK(after.total_in == before.total_in);
    CHECK(after.total_out == before.total_out);
    CHECK(after.window_in == before.window_in);
    lc_app_reset(&app, 0);
}

static void test_apply_invalid_rejected(void) {
    fake_t f;
    fake_init(&f);
    lc_app_ops_t ops;
    ops_init(&ops, &f);
    line_counting_config_t cfg;
    cfg_enabled(&cfg);

    lc_app_t app;
    lc_app_init(&app, &ops, &cfg);

    line_counting_config_t bad;
    bad = cfg;
    bad.k_confirm = 0;
    CHECK(lc_app_apply_config(&app, &bad, NULL) == AICAM_ERROR_INVALID_DATA);

    bad = cfg;
    bad.line_x2_permille = bad.line_x1_permille;
    bad.line_y2_permille = bad.line_y1_permille;
    CHECK(lc_app_apply_config(&app, &bad, NULL) == AICAM_ERROR_INVALID_DATA);

    CHECK(f.save_calls == 0);
    line_counting_stats_t stats;
    lc_app_get_stats(&app, &stats);
    CHECK(stats.total_in == 0);
    CHECK(app.cfg.k_confirm == cfg.k_confirm);
    lc_app_reset(&app, 0);
}

static void test_manual_reset(void) {
    fake_t f;
    fake_init(&f);
    lc_app_ops_t ops;
    ops_init(&ops, &f);
    line_counting_config_t cfg;
    cfg_enabled(&cfg);
    cfg.max_dist_permille = 500;
    cfg.k_confirm = 2;

    lc_app_t app;
    lc_app_init(&app, &ops, &cfg);

    lc_frame_input_t fr;
    lc_det_t det;
    frame_one_at(&fr, &det, 0.5f, 0.35f);
    run_frames(&app, &fr, 3);
    det.y = 0.55f;
    run_frames(&app, &fr, 3);
    line_counting_stats_t before;
    lc_app_get_stats(&app, &before);
    CHECK(before.total_in + before.total_out > 0);

    f.now = 250000;
    CHECK(lc_app_reset(&app, 250000) == AICAM_OK);
    CHECK(f.total_in == 0 && f.total_out == 0);
    line_counting_stats_t stats;
    lc_app_get_stats(&app, &stats);
    CHECK(stats.total_in == 0 && stats.total_out == 0);
    CHECK(stats.window_in == 0 && stats.window_out == 0);
    CHECK(stats.window_start_ms == 250000);
    line_count_event_t evs[4];
    CHECK(lc_app_get_events(&app, evs, 4) == 0);
    line_counting_status_t st;
    lc_app_get_status(&app, &st);
    CHECK(st.state == LC_STATE_RUNNING);
    lc_app_reset(&app, 0);
}

static void test_persistence_failure_paths(void) {
    fake_t f;
    fake_init(&f);
    lc_app_ops_t ops;
    ops_init(&ops, &f);
    line_counting_config_t cfg;
    cfg_enabled(&cfg);

    lc_app_t app;
    lc_app_init(&app, &ops, &cfg);

    f.info_ret = AICAM_ERROR_NOT_INITIALIZED;
    lc_frame_input_t fr;
    frame_empty(&fr);
    run_frames(&app, &fr, 1);
    line_counting_status_t st;
    lc_app_get_status(&app, &st);
    CHECK(st.state == LC_STATE_UNSUPPORTED_MODEL);
    f.info_ret = AICAM_OK;

    f.save_ret = AICAM_ERROR_IO;
    line_counting_config_t changed;
    changed = cfg;
    snprintf(changed.target_class_name, sizeof(changed.target_class_name), "car");
    CHECK(lc_app_apply_config(&app, &changed, NULL) == AICAM_ERROR_IO);
    CHECK_STR(app.cfg.target_class_name, "person");
    CHECK(f.total_in == 0);

    CHECK(lc_app_reset(&app, 1000) == AICAM_ERROR_IO);
    CHECK(f.total_in == 0);
    f.save_ret = AICAM_OK;
    lc_app_reset(&app, 0);
}

int main(void) {
    test_init_disabled_loads_totals();
    test_running_od_binding();
    test_unsupported_model();
    test_target_class_invalid_and_recovery();
    test_zero_detection_frames_reach_engine();
    test_model_change_preserves_business_clears_transient();
    test_events_ring_bounded();
    test_window_close();
    test_disable_closes_window_and_reenable_fresh();
    test_target_change_resets_session();
    test_counter_name_change_keeps_stats();
    test_apply_invalid_rejected();
    test_manual_reset();
    test_persistence_failure_paths();

    if (g_failures) {
        printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all line counting app tests passed\n");
    return 0;
}
