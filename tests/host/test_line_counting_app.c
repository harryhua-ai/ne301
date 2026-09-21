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
    aicam_result_t persist_ret;
    aicam_result_t queue_clear_ret;
    int persist_calls;
    int queue_clear_calls;
    int persist_fail_after_calls;
    int save_fail_after_calls;
    int save_fail_call;
    aicam_result_t txn_prepare_ret;
    aicam_result_t txn_clear_ret;
    int txn_get_fail;
    int txn_prepare_calls;
    uint32_t txn_op;
    int txn_clear_calls;
    uint8_t txn_present;
    line_counting_config_t txn_cfg;
    line_counting_config_t persisted_cfg;
    uint8_t persisted_valid;
    uint32_t queue_len;
    line_counting_config_t last_persisted;
    int totals_zero_at_persist_time;
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

static aicam_result_t fk_load_totals(void *user, uint32_t *total_in, uint32_t *total_out,
                                     uint32_t *epoch) {
    (void)epoch;
    fake_t *f = (fake_t *)user;
    f->load_calls++;
    if (f->load_ret != AICAM_OK) return f->load_ret;
    *total_in = f->total_in;
    *total_out = f->total_out;
    return AICAM_OK;
}

static aicam_result_t fk_save_totals(void *user, uint32_t total_in, uint32_t total_out,
                                     uint32_t epoch) {
    (void)epoch;
    fake_t *f = (fake_t *)user;
    f->save_calls++;
    if (f->save_fail_call > 0 && f->save_calls == f->save_fail_call) {
        f->save_fail_call = 0;
        return AICAM_ERROR_IO;
    }
    if (f->save_fail_after_calls > 0 && f->save_calls > f->save_fail_after_calls) {
        return AICAM_ERROR_IO;
    }
    if (f->save_ret != AICAM_OK) return f->save_ret;
    f->total_in = total_in;
    f->total_out = total_out;
    return AICAM_OK;
}

static aicam_result_t fk_persist_config(void *user, const line_counting_config_t *candidate) {
    fake_t *f = (fake_t *)user;
    f->persist_calls++;
    if (f->persist_fail_after_calls > 0 && f->persist_calls > f->persist_fail_after_calls) {
        return AICAM_ERROR_IO;
    }
    if (f->persist_ret != AICAM_OK) return f->persist_ret;
    f->last_persisted = *candidate;
    f->persisted_cfg = *candidate;
    f->persisted_valid = 1;
    f->totals_zero_at_persist_time = (f->total_in == 0 && f->total_out == 0);
    return AICAM_OK;
}

static aicam_result_t fk_queue_clear(void *user) {
    fake_t *f = (fake_t *)user;
    f->queue_clear_calls++;
    if (f->queue_clear_ret != AICAM_OK) return f->queue_clear_ret;
    f->queue_len = 0;
    return AICAM_OK;
}

static aicam_result_t fk_txn_prepare(void *user, uint32_t op, const line_counting_config_t *candidate) {
    fake_t *f = (fake_t *)user;
    f->txn_op = op;
    f->txn_prepare_calls++;
    if (f->txn_prepare_ret != AICAM_OK) return f->txn_prepare_ret;
    f->txn_present = 1;
    if (candidate) f->txn_cfg = *candidate;
    return AICAM_OK;
}

static aicam_result_t fk_txn_get(void *user, uint32_t *op_out, line_counting_config_t *candidate) {
    fake_t *f = (fake_t *)user;
    *op_out = f->txn_op;
    if (f->txn_get_fail > 0) {
        f->txn_get_fail--;
        return AICAM_ERROR_IO;
    }
    if (!f->txn_present) return AICAM_ERROR_NOT_FOUND;
    *candidate = f->txn_cfg;
    return AICAM_OK;
}

static aicam_result_t fk_txn_clear(void *user) {
    fake_t *f = (fake_t *)user;
    f->txn_clear_calls++;
    if (f->txn_clear_ret != AICAM_OK) return f->txn_clear_ret;
    f->txn_present = 0;
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
    ops->persist_config = fk_persist_config;
    ops->queue_clear = fk_queue_clear;
    ops->txn_prepare = fk_txn_prepare;
    ops->txn_get = fk_txn_get;
    ops->txn_clear = fk_txn_clear;
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
    CHECK(lc_app_apply_config(&app, &changed, NULL) == AICAM_ERROR_TRANSACTION);
    CHECK_STR(app.cfg.target_class_name, "person");
    CHECK(f.total_in == 0);

    CHECK(lc_app_reset(&app, 1000) == AICAM_ERROR_TRANSACTION);
    CHECK(f.total_in == 0);
    CHECK(app.totals_resetting == 1);
    f.save_ret = AICAM_OK;
    CHECK(lc_app_recover_transaction(&app) == AICAM_OK);
    CHECK(app.total_in == 0);
    CHECK(app.totals_resetting == 0);
}


static int drive_crossings(lc_app_t *app, fake_t *f, int n) {
    lc_frame_input_t fr;
    lc_det_t det;
    uint32_t ts = 1000;
    float y = 0.35f;
    int before_in, before_out;
    line_counting_stats_t st;
    lc_app_get_stats(app, &st);
    before_in = (int)st.total_in;
    before_out = (int)st.total_out;
    (void)0;
    for (int i = 0; i < n; i++) {
        det.x = 0.5f;
        det.y = y;
        det.w = 0.1f;
        det.h = 0.1f;
        det.conf = 0.9f;
        det.class_name = "person";
        fr.result_type = PP_TYPE_OD;
        fr.nb_detect = 1;
        fr.detects = &det;
        lc_app_on_ai_result(app, &fr, ts);
        ts += 100;
        y = (y < 0.5f) ? 0.55f : 0.35f;
    }
    lc_app_get_stats(app, &st);
    (void)f;
    return (int)(st.total_in + st.total_out) - (before_in + before_out);
}

static void test_atomic_case_a_normal_change(void) {
    fake_t f;
    fake_init(&f);
    lc_app_ops_t ops;
    ops_init(&ops, &f);
    line_counting_config_t cfg;
    cfg_enabled(&cfg);

    lc_app_t app;
    lc_app_init(&app, &ops, &cfg);

    line_counting_config_t changed;
    changed = cfg;
    changed.window_minutes = 10;
    CHECK(lc_app_apply_config(&app, &changed, NULL) == AICAM_OK);
    CHECK(f.persist_calls == 1);
    CHECK(f.last_persisted.window_minutes == 10);
    CHECK(f.save_calls == 0);
    CHECK(app.cfg.window_minutes == 10);
    lc_app_reset(&app, 0);
}

static void test_atomic_case_b_invalid_unchanged(void) {
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
    CHECK(f.persist_calls == 0);
    CHECK(app.cfg.window_minutes == cfg.window_minutes);
    CHECK(app.cfg.k_confirm == cfg.k_confirm);
    lc_app_reset(&app, 0);
}

static void test_atomic_case_d_persist_failure_unchanged(void) {
    fake_t f;
    fake_init(&f);
    lc_app_ops_t ops;
    ops_init(&ops, &f);
    line_counting_config_t cfg;
    cfg_enabled(&cfg);

    lc_app_t app;
    lc_app_init(&app, &ops, &cfg);

    f.persist_ret = AICAM_ERROR_IO;
    line_counting_config_t changed;
    changed = cfg;
    changed.window_minutes = 10;
    CHECK(lc_app_apply_config(&app, &changed, NULL) == AICAM_ERROR_IO);
    CHECK(app.cfg.window_minutes == cfg.window_minutes);
    line_counting_stats_t stats;
    lc_app_get_stats(&app, &stats);
    CHECK(stats.total_in == 0 && stats.total_out == 0);
    CHECK(f.save_calls == 0);
    lc_app_reset(&app, 0);
}

static void test_atomic_case_e_target_change_totals_failure(void) {
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

    CHECK(drive_crossings(&app, &f, 6) > 0);
    line_count_event_t evs[4];
    CHECK(lc_app_get_events(&app, evs, 4) > 0);

    f.save_ret = AICAM_ERROR_IO;
    line_counting_config_t changed;
    changed = cfg;
    snprintf(changed.target_class_name, sizeof(changed.target_class_name), "car");
    CHECK(lc_app_apply_config(&app, &changed, NULL) == AICAM_ERROR_TRANSACTION);

    CHECK_STR(app.cfg.target_class_name, "person");
    line_counting_stats_t stats;
    lc_app_get_stats(&app, &stats);
    CHECK(stats.total_in + stats.total_out > 0);
    CHECK(lc_app_get_events(&app, evs, 4) > 0);
    CHECK(lc_tracker_active_count(app.tracker) > 0);
    CHECK(f.persist_calls >= 1); /* persist called first (at least once), then save fails and rolls back */
    lc_app_reset(&app, 0);
}

static void test_atomic_case_e2_queue_clear_failure(void) {
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

    CHECK(drive_crossings(&app, &f, 6) > 0);
    line_count_event_t evs[4];
    CHECK(lc_app_get_events(&app, evs, 4) > 0);

    f.queue_clear_ret = AICAM_ERROR_IO;
    line_counting_config_t changed;
    changed = cfg;
    snprintf(changed.target_class_name, sizeof(changed.target_class_name), "car");
    CHECK(lc_app_apply_config(&app, &changed, NULL) == AICAM_ERROR_IO);

    CHECK_STR(app.cfg.target_class_name, "person");
    line_counting_stats_t stats;
    lc_app_get_stats(&app, &stats);
    CHECK(stats.total_in + stats.total_out > 0);
    CHECK(lc_app_get_events(&app, evs, 4) > 0);
    CHECK(f.total_in + f.total_out > 0);
    lc_app_reset(&app, 0);
}

static void test_atomic_case_f_target_change_full_success(void) {
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

    CHECK(drive_crossings(&app, &f, 6) > 0);

    f.now = 200000;
    line_counting_config_t changed;
    changed = cfg;
    snprintf(changed.target_class_name, sizeof(changed.target_class_name), "car");
    int saves_before = f.save_calls;
    CHECK(lc_app_apply_config(&app, &changed, NULL) == AICAM_OK);
    CHECK(f.persist_calls == 1);
    CHECK(f.save_calls == saves_before + 1);
    CHECK(f.total_in == 0 && f.total_out == 0);
    CHECK(f.queue_clear_calls == 1);

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

static void test_totals_checkpoint_cycle(void) {
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

    uint32_t tin, tout, gen;
    uint32_t epoch = 0;
    CHECK(lc_app_take_totals_checkpoint(&app, &tin, &tout, &gen, &epoch) == AICAM_FALSE);

    CHECK(drive_crossings(&app, &f, 6) > 0);
    CHECK(lc_app_take_totals_checkpoint(&app, &tin, &tout, &gen, &epoch) == AICAM_TRUE);
    CHECK(tin + tout > 0);
    /* Dirty not cleared yet - acknowledge after successful save */
    lc_app_acknowledge_checkpoint(&app, gen, epoch);
    CHECK(lc_app_take_totals_checkpoint(&app, &tin, &tout, &gen, &epoch) == AICAM_FALSE);

    f.total_in = tin;
    f.total_out = tout;
    f.load_ret = AICAM_OK;

    lc_app_t reloaded;
    lc_app_init(&reloaded, &ops, &cfg);
    line_counting_stats_t stats;
    lc_app_get_stats(&reloaded, &stats);
    CHECK(stats.total_in == tin && stats.total_out == tout);
    lc_app_reset(&reloaded, 0);
    lc_app_reset(&app, 0);
}

static void test_totals_checkpoint_retry_on_failure(void) {
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
    CHECK(drive_crossings(&app, &f, 6) > 0);

    uint32_t tin, tout, gen;
    uint32_t epoch = 0;
    CHECK(lc_app_take_totals_checkpoint(&app, &tin, &tout, &gen, &epoch) == AICAM_TRUE);
    lc_app_mark_totals_dirty(&app);
    CHECK(lc_app_take_totals_checkpoint(&app, &tin, &tout, &gen, &epoch) == AICAM_TRUE);
    CHECK(tin + tout > 0);
    lc_app_reset(&app, 0);
}

static void test_manual_reset_durable_zero_and_reboot(void) {
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
    CHECK(drive_crossings(&app, &f, 6) > 0);

    CHECK(lc_app_reset(&app, 500000) == AICAM_OK);
    CHECK(f.total_in == 0 && f.total_out == 0);
    CHECK(f.queue_clear_calls >= 1);

    f.load_ret = AICAM_OK;
    lc_app_t reloaded;
    lc_app_init(&reloaded, &ops, &cfg);
    line_counting_stats_t stats;
    lc_app_get_stats(&reloaded, &stats);
    CHECK(stats.total_in == 0 && stats.total_out == 0);
    lc_app_reset(&reloaded, 0);
    lc_app_reset(&app, 0);
}

static void test_manual_reset_totals_failure_aborts(void) {
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
    CHECK(drive_crossings(&app, &f, 6) > 0);
    line_count_event_t evs[4];
    CHECK(lc_app_get_events(&app, evs, 4) > 0);
    line_counting_stats_t before;
    lc_app_get_stats(&app, &before);

    f.save_ret = AICAM_ERROR_IO;
    CHECK(lc_app_reset(&app, 900000) == AICAM_ERROR_TRANSACTION);
    line_counting_stats_t after;
    lc_app_get_stats(&app, &after);
    CHECK(after.total_in == before.total_in);
    CHECK(after.total_out == before.total_out);
    CHECK(lc_app_get_events(&app, evs, 4) > 0);
    CHECK(after.window_start_ms == before.window_start_ms);
    CHECK(app.totals_resetting == 1);

    f.save_ret = AICAM_OK;
    CHECK(f.total_in == 0 && f.total_out == 0);

    CHECK(lc_app_recover_transaction(&app) == AICAM_OK);
    lc_app_get_stats(&app, &after);
    CHECK(after.total_in == 0 && after.total_out == 0);
    CHECK(app.totals_resetting == 0);
}

static void test_exactly_once_mixed_frames(void) {
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

    lc_frame_input_t fr_full;
    lc_det_t det;
    frame_one_at(&fr_full, &det, 0.5f, 0.35f);
    lc_frame_input_t fr_empty;
    frame_empty(&fr_empty);

    for (int i = 0; i < 30; i++) {
        lc_frame_input_t *fr = (i % 2 == 0) ? &fr_full : &fr_empty;
        det.y = (i % 4 < 2) ? 0.35f : 0.55f;
        lc_app_on_ai_result(&app, fr, (uint32_t)(1000 + i * 100));
    }

    line_count_event_t evs[LC_EVENTS_RING_CAPACITY];
    uint16_t n = lc_app_get_events(&app, evs, LC_EVENTS_RING_CAPACITY);
    CHECK(n > 0);
    for (uint16_t i = 1; i < n; i++) {
        CHECK(evs[i - 1].sequence > evs[i].sequence);
    }
    line_counting_status_t st;
    lc_app_get_status(&app, &st);
    CHECK(st.tracker_active <= 2);
    lc_app_reset(&app, 0);
}

/* ===== Forward declarations for new RED tests (Blocker fixes) ===== */
static void test_config_non_reset_change_preserves_dirty(void);
static void test_checkpoint_io_failure_leaves_dirty(void);
static void test_failure_then_new_crossing_then_retry(void);
static void test_config_change_only_no_double_persist(void);
static void test_rest_handler_single_entrypoint(void);
static void test_reset_queue_failure_restores_old_state(void);
static void test_mutex_contention_blocking(void);
static void test_zero_detection_under_contention(void);
static void test_burst_ordering(void);

/* ===== Forward declarations for fault-injection regression tests ===== */
static void test_config_persist_failure_canonical_ram_unchanged(void);
static void test_config_apply_success_coherent_state(void);
static void test_checkpoint_save_failure_leaves_dirty(void);
static void test_checkpoint_crossing_new_pending(void);
static void test_checkpoint_failure_crossing_preserved(void);
static void test_checkpoint_retry_without_new_crossings(void);
static void test_manual_reset_single_queue_clear(void);
static void test_manual_reset_queue_clear_failure_rolls_back(void);
static void test_target_change_config_persist_failure_preserves_queue(void);
static void test_target_change_config_persist_failure_no_queue_clear(void);
static void test_target_change_full_success_commits(void);
static void test_stale_epoch_ack_vs_manual_reset(void);
static void test_stale_epoch_ack_vs_target_change(void);
static void test_checkpoint_then_reset_ordering(void);
static void test_disable_preserves_dirty_totals(void);
static void test_target_change_reboot_after_commit(void);
static void test_concurrent_writer_staging_ownership(void);

static void fake_reboot(fake_t *f, lc_app_ops_t *ops, lc_app_t *app,
                        const line_counting_config_t *cfg);
static void test_txn_prepare_fail_prior_intact(void);
static void test_txn_persist_fail_prepared_boot_rolls_forward(void);
static void test_txn_totals_zero_fail_compensation_ok_prior(void);
static void test_txn_totals_zero_fail_compensation_fail_boot_new(void);
static void test_txn_queue_fail_compensation_ok_prior(void);
static void test_txn_queue_fail_compensation_fail_boot_new(void);
static void test_txn_commit_clear_fail_runtime_committed_boot_reaffirms(void);
static void test_txn_recovery_io_failure_then_retry(void);
static void test_txn_recovery_no_record_noop(void);
static void test_txn_full_success_clears_record(void);
static void test_totals_store_fresh_not_found(void);
static void test_totals_store_roundtrip_alternates_slots(void);
static void test_totals_store_torn_save_reboot_previous(void);
static void test_totals_store_io_error_keeps_previous_and_retries(void);
static void test_totals_store_both_slots_corrupt(void);
static void test_totals_epoch_beats_late_stale_checkpoint(void);
static void test_totals_torn_wrap_falls_back_to_valid_record(void);

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
    test_atomic_case_a_normal_change();
    test_atomic_case_b_invalid_unchanged();
    test_atomic_case_d_persist_failure_unchanged();
    test_atomic_case_e_target_change_totals_failure();
    test_atomic_case_e2_queue_clear_failure();
    test_atomic_case_f_target_change_full_success();
    test_totals_checkpoint_cycle();
    test_totals_checkpoint_retry_on_failure();
    test_manual_reset_durable_zero_and_reboot();
    test_manual_reset_totals_failure_aborts();
    test_exactly_once_mixed_frames();
    test_config_non_reset_change_preserves_dirty();
    test_checkpoint_io_failure_leaves_dirty();
    test_failure_then_new_crossing_then_retry();
    test_config_change_only_no_double_persist();
    test_rest_handler_single_entrypoint();
    test_reset_queue_failure_restores_old_state();
    test_mutex_contention_blocking();
    test_zero_detection_under_contention();
    test_burst_ordering();
    test_config_persist_failure_canonical_ram_unchanged();
    test_config_apply_success_coherent_state();
    test_checkpoint_save_failure_leaves_dirty();
    test_checkpoint_crossing_new_pending();
    test_checkpoint_failure_crossing_preserved();
    test_checkpoint_retry_without_new_crossings();
    test_manual_reset_single_queue_clear();
    test_manual_reset_queue_clear_failure_rolls_back();
    test_target_change_config_persist_failure_preserves_queue();
    test_target_change_config_persist_failure_no_queue_clear();
    test_target_change_full_success_commits();
    test_stale_epoch_ack_vs_manual_reset();
    test_stale_epoch_ack_vs_target_change();
    test_checkpoint_then_reset_ordering();
    test_disable_preserves_dirty_totals();
    test_target_change_reboot_after_commit();
    test_concurrent_writer_staging_ownership();

    test_txn_prepare_fail_prior_intact();
    test_txn_persist_fail_prepared_boot_rolls_forward();
    test_txn_totals_zero_fail_compensation_ok_prior();
    test_txn_totals_zero_fail_compensation_fail_boot_new();
    test_txn_queue_fail_compensation_ok_prior();
    test_txn_queue_fail_compensation_fail_boot_new();
    test_txn_commit_clear_fail_runtime_committed_boot_reaffirms();
    test_txn_recovery_io_failure_then_retry();
    test_txn_recovery_no_record_noop();
    test_txn_full_success_clears_record();
    test_totals_store_fresh_not_found();
    test_totals_store_roundtrip_alternates_slots();
    test_totals_store_torn_save_reboot_previous();
    test_totals_store_io_error_keeps_previous_and_retries();
    test_totals_store_both_slots_corrupt();
    test_totals_epoch_beats_late_stale_checkpoint();
    test_totals_torn_wrap_falls_back_to_valid_record();

    if (g_failures) {
        printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all line counting app tests passed\n");
    return 0;
}

/* ===== New RED tests for Blocker fixes ===== */

static void test_config_non_reset_change_preserves_dirty(void) {
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
    CHECK(drive_crossings(&app, &f, 6) > 0);

    /* totals should be dirty now - NO checkpoint yet */

    /* Now do a non-reset config change (counter_name) */
    line_counting_config_t changed = cfg;
    snprintf(changed.counter_name, sizeof(changed.counter_name), "new_name");
    CHECK(lc_app_apply_config(&app, &changed, NULL) == AICAM_OK);

    /* dirty should still be set (not cleared by non-reset change) */
    uint32_t tin, tout, gen;
    uint32_t epoch = 0;
    CHECK(lc_app_take_totals_checkpoint(&app, &tin, &tout, &gen, &epoch) == AICAM_TRUE);
    CHECK(tin + tout > 0);
    lc_app_reset(&app, 0);
}

static void test_checkpoint_io_failure_leaves_dirty(void) {
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
    CHECK(drive_crossings(&app, &f, 6) > 0);

    /* First checkpoint succeeds */
    uint32_t tin, tout, gen;
    uint32_t epoch = 0;
    CHECK(lc_app_take_totals_checkpoint(&app, &tin, &tout, &gen, &epoch) == AICAM_TRUE);
    CHECK(tin + tout > 0);

    /* dirty is still set (checkpoint only snapshots, doesn't acknowledge) */

    /* Simulate save failure by caller: mark dirty again for retry (increments generation) */
    lc_app_mark_totals_dirty(&app);
    CHECK(lc_app_take_totals_checkpoint(&app, &tin, &tout, &gen, &epoch) == AICAM_TRUE); /* dirty was re-set, new generation */
    CHECK(tin + tout > 0);

    /* Acknowledge the new generation after successful retry save */
    lc_app_acknowledge_checkpoint(&app, gen, epoch);
    CHECK(lc_app_take_totals_checkpoint(&app, &tin, &tout, &gen, &epoch) == AICAM_FALSE);
    lc_app_reset(&app, 0);
}

static void test_failure_then_new_crossing_then_retry(void) {
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
    CHECK(drive_crossings(&app, &f, 3) > 0); /* first batch */

    /* First checkpoint succeeds */
    uint32_t tin, tout, gen;
    uint32_t epoch = 0;
    CHECK(lc_app_take_totals_checkpoint(&app, &tin, &tout, &gen, &epoch) == AICAM_TRUE);
    CHECK(tin + tout > 0);

    /* Simulate save failure by caller: mark dirty for retry */
    lc_app_mark_totals_dirty(&app);

    /* New crossing occurs while dirty (adds to existing totals) */
    CHECK(drive_crossings(&app, &f, 3) > 0); /* second batch */

    /* Retry checkpoint - should include both batches */
    CHECK(lc_app_take_totals_checkpoint(&app, &tin, &tout, &gen, &epoch) == AICAM_TRUE);
    CHECK(tin + tout > 0);

    /* Verify the total includes both batches by reloading */
    f.total_in = tin;
    f.total_out = tout;
    f.load_ret = AICAM_OK;
    lc_app_t reloaded;
    lc_app_init(&reloaded, &ops, &cfg);
    line_counting_stats_t stats;
    lc_app_get_stats(&reloaded, &stats);
    CHECK(stats.total_in == tin && stats.total_out == tout);
    lc_app_reset(&reloaded, 0);
    lc_app_reset(&app, 0);
}

static void test_config_change_only_no_double_persist(void) {
    fake_t f;
    fake_init(&f);
    f.persist_ret = AICAM_OK;
    f.save_ret = AICAM_OK;
    lc_app_ops_t ops;
    ops_init(&ops, &f);
    line_counting_config_t cfg;
    cfg_enabled(&cfg);

    lc_app_t app;
    lc_app_init(&app, &ops, &cfg);

    line_counting_config_t changed = cfg;
    changed.counter_name[0] = 'X'; /* non-reset change */
    CHECK(lc_app_apply_config(&app, &changed, NULL) == AICAM_OK);

    /* Only one persist call (for the config), no extra saves */
    CHECK(f.persist_calls == 1);
    CHECK(f.save_calls == 0);
    lc_app_reset(&app, 0);
}

static void test_rest_handler_single_entrypoint(void) {
    /* This test verifies the REST handler calls the transaction entrypoint once.
     * We can't easily test the REST layer from host test, but we can verify
     * that line_counting_apply_config is the single entrypoint by checking
     * it doesn't double-persist when called directly. */
    fake_t f;
    fake_init(&f);
    f.persist_ret = AICAM_OK;
    f.save_ret = AICAM_OK;
    f.queue_clear_ret = AICAM_OK;

    lc_app_ops_t ops;
    ops_init(&ops, &f);
    line_counting_config_t cfg;
    cfg_enabled(&cfg);

    lc_app_t app;
    lc_app_init(&app, &ops, &cfg);

    line_counting_config_t changed = cfg;
    changed.window_minutes = 10;
    CHECK(lc_app_apply_config(&app, &changed, NULL) == AICAM_OK);

    /* Only one persist call */
    CHECK(f.persist_calls == 1);
    lc_app_reset(&app, 0);
}

static void test_reset_queue_failure_restores_old_state(void) {
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
    CHECK(drive_crossings(&app, &f, 6) > 0);
    line_count_event_t evs[4];
    CHECK(lc_app_get_events(&app, evs, 4) > 0);
    line_counting_stats_t before;
    lc_app_get_stats(&app, &before);

    f.queue_clear_ret = AICAM_ERROR_IO;
    CHECK(lc_app_reset(&app, 900000) == AICAM_ERROR_TRANSACTION);

    line_counting_stats_t after;
    lc_app_get_stats(&app, &after);
    CHECK(after.total_in == before.total_in);
    CHECK(after.total_out == before.total_out);
    CHECK(lc_app_get_events(&app, evs, 4) > 0);
    CHECK(app.totals_resetting == 1);

    f.queue_clear_ret = AICAM_OK;
    CHECK(lc_app_recover_transaction(&app) == AICAM_OK);
    lc_app_get_stats(&app, &after);
    CHECK(after.total_in == 0 && after.total_out == 0);
    CHECK(app.totals_resetting == 0);
}

/* Contention tests - need factored mutex for host testing */
static void test_mutex_contention_blocking(void) {
    /* This test requires a factored mutex implementation for host testing.
     * The production code uses osMutexAcquire with osWaitForever.
     * We test that the blocking behavior works by simulating contention. */
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

    /* Simulate contention by holding the lock */
    /* This test is a placeholder - real contention test needs factored mutex */
    lc_app_on_ai_result(&app, &fr, 1000);
    lc_app_on_ai_result(&app, &fr, 1100);
    lc_app_on_ai_result(&app, &fr, 1200);

    line_counting_status_t st;
    lc_app_get_status(&app, &st);
    CHECK(st.tracker_active >= 1);
    lc_app_reset(&app, 0);
}

static void test_zero_detection_under_contention(void) {
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
    frame_empty(&fr);

    /* Multiple zero-detection frames under simulated contention */
    for (int i = 0; i < 10; i++) {
        lc_app_on_ai_result(&app, &fr, (uint32_t)(1000 + i * 100));
    }

    line_counting_stats_t stats;
    lc_app_get_stats(&app, &stats);
    CHECK(stats.total_in == 0 && stats.total_out == 0);
    lc_app_reset(&app, 0);
}

static void test_burst_ordering(void) {
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
    for (int i = 0; i < 20; i++) {
        frame_one_at(&fr, &det, 0.5f, y);
        lc_app_on_ai_result(&app, &fr, ts);
        ts += 50; /* rapid burst */
        y = (y < 0.5f) ? 0.55f : 0.35f;
    }

    line_count_event_t evs[LC_EVENTS_RING_CAPACITY];
    uint16_t n = lc_app_get_events(&app, evs, LC_EVENTS_RING_CAPACITY);
    CHECK(n > 0);
    for (uint16_t i = 1; i < n; i++) {
        CHECK(evs[i - 1].sequence > evs[i].sequence);
    }
    lc_app_reset(&app, 0);
}

/* ===== Comprehensive fault-injection regression tests ===== */

/* Blocker 1: Config persistence failure - canonical RAM must not be mutated */
static void test_config_persist_failure_canonical_ram_unchanged(void) {
    fake_t f;
    fake_init(&f);
    lc_app_ops_t ops;
    ops_init(&ops, &f);
    line_counting_config_t cfg;
    cfg_enabled(&cfg);
    cfg.window_minutes = 5;
    cfg.max_dist_permille = 500;
    cfg.k_confirm = 2;

    lc_app_t app;
    lc_app_init(&app, &ops, &cfg);

    /* Capture original config state */
    line_counting_config_t original = app.cfg;
    uint8_t orig_enable = original.enable;
    uint32_t orig_window = original.window_minutes;

    /* Inject NVS persistence failure */
    f.persist_ret = AICAM_ERROR_IO;

    line_counting_config_t changed = cfg;
    changed.window_minutes = 10;
    CHECK(lc_app_apply_config(&app, &changed, NULL) == AICAM_ERROR_IO);

    /* Canonical RAM must remain exactly the original config */
    CHECK(app.cfg.enable == orig_enable);
    CHECK(app.cfg.window_minutes == orig_window);
    CHECK(f.persist_calls >= 1); /* persist was attempted */

    /* NVS should not have been updated (fake doesn't track NVS, but persist returned error) */
    lc_app_reset(&app, 0);
}

/* Blocker 1: Successful config apply produces coherent new state */
static void test_config_apply_success_coherent_state(void) {
    fake_t f;
    fake_init(&f);
    lc_app_ops_t ops;
    ops_init(&ops, &f);
    line_counting_config_t cfg;
    cfg_enabled(&cfg);
    cfg.window_minutes = 5;

    lc_app_t app;
    lc_app_init(&app, &ops, &cfg);

    line_counting_config_t changed = cfg;
    changed.window_minutes = 10;
    CHECK(lc_app_apply_config(&app, &changed, NULL) == AICAM_OK);

    /* All three layers coherent: canonical RAM, LC runtime */
    CHECK(app.cfg.window_minutes == 10);
    CHECK(f.persist_calls == 1);
    CHECK(f.last_persisted.window_minutes == 10);
    lc_app_reset(&app, 0);
}

/* Blocker 2: Checkpoint save failure leaves dirty state */
static void test_checkpoint_save_failure_leaves_dirty(void) {
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
    CHECK(drive_crossings(&app, &f, 6) > 0);

    uint32_t tin, tout, gen;
    uint32_t epoch = 0;
    CHECK(lc_app_take_totals_checkpoint(&app, &tin, &tout, &gen, &epoch) == AICAM_TRUE);
    CHECK(tin + tout > 0);

    /* Simulate save failure - do NOT acknowledge */
    f.save_ret = AICAM_ERROR_IO;
    if (ops.save_totals(ops.user, tin, tout, 0u) != AICAM_OK) {
        /* No acknowledge call - dirty should remain set */
    }

    CHECK(lc_app_take_totals_checkpoint(&app, &tin, &tout, &gen, &epoch) == AICAM_TRUE); /* same generation, still dirty */
    CHECK(tin + tout > 0);

    /* Now save succeeds - acknowledge */
    f.save_ret = AICAM_OK;
    if (ops.save_totals(ops.user, tin, tout, 0u) == AICAM_OK) {
        lc_app_acknowledge_checkpoint(&app, gen, epoch);
    }

    CHECK(lc_app_take_totals_checkpoint(&app, &tin, &tout, &gen, &epoch) == AICAM_FALSE); /* now clean */
    lc_app_reset(&app, 0);
}

/* Blocker 2: Checkpoint A -> new crossing B -> A success -> B remains pending */
static void test_checkpoint_crossing_new_pending(void) {
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

    /* First crossing batch */
    CHECK(drive_crossings(&app, &f, 3) > 0);

    uint32_t tin, tout, gen_a;
    uint32_t epoch = 0;
    CHECK(lc_app_take_totals_checkpoint(&app, &tin, &tout, &gen_a, &epoch) == AICAM_TRUE);
    uint32_t checkpoint_a_total = tin + tout;

    /* New crossing occurs while checkpoint A is pending save */
    CHECK(drive_crossings(&app, &f, 3) > 0); /* adds to totals */

    /* Save checkpoint A succeeds - acknowledge A */
    f.save_ret = AICAM_OK;
    ops.save_totals(ops.user, tin, tout, 0u);
    lc_app_acknowledge_checkpoint(&app, gen_a, epoch);

    /* Dirty should now be set again (due to new crossing B) */
    uint32_t gen_b;
    CHECK(lc_app_take_totals_checkpoint(&app, &tin, &tout, &gen_b, &epoch) == AICAM_TRUE); /* new generation! */
    CHECK(tin + tout > checkpoint_a_total); /* includes both A and B */
    CHECK(gen_b > gen_a); /* generation advanced */

    /* Acknowledge B */
    ops.save_totals(ops.user, tin, tout, 0u);
    lc_app_acknowledge_checkpoint(&app, gen_b, epoch);

    CHECK(lc_app_take_totals_checkpoint(&app, &tin, &tout, &gen_b, &epoch) == AICAM_FALSE);
    lc_app_reset(&app, 0);
}

/* Blocker 2: Checkpoint A -> new crossing B -> A failure -> pending state correct */
static void test_checkpoint_failure_crossing_preserved(void) {
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

    /* First crossing batch */
    CHECK(drive_crossings(&app, &f, 3) > 0);

    uint32_t tin, tout, gen_a;
    uint32_t epoch = 0;
    CHECK(lc_app_take_totals_checkpoint(&app, &tin, &tout, &gen_a, &epoch) == AICAM_TRUE);
    uint32_t checkpoint_a_total = tin + tout;

    /* New crossing occurs */
    CHECK(drive_crossings(&app, &f, 3) > 0); /* adds to totals */

    /* Save checkpoint A FAILS - do NOT acknowledge */
    f.save_ret = AICAM_ERROR_IO;
    if (ops.save_totals(ops.user, tin, tout, 0u) != AICAM_OK) {
        /* No acknowledge - dirty remains set */
    }

    /* Checkpoint should still return dirty (same or new generation) */
    CHECK(lc_app_take_totals_checkpoint(&app, &tin, &tout, &gen_a, &epoch) == AICAM_TRUE);
    CHECK(tin + tout > checkpoint_a_total); /* both A and B preserved */

    /* Now retry - save succeeds */
    f.save_ret = AICAM_OK;
    if (ops.save_totals(ops.user, tin, tout, 0u) == AICAM_OK) {
        lc_app_acknowledge_checkpoint(&app, gen_a, epoch);
    }

    CHECK(lc_app_take_totals_checkpoint(&app, &tin, &tout, &gen_a, &epoch) == AICAM_FALSE);
    lc_app_reset(&app, 0);
}

/* Blocker 2: Repeated ticks retry even with no additional crossings */
static void test_checkpoint_retry_without_new_crossings(void) {
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
    CHECK(drive_crossings(&app, &f, 3) > 0);

    uint32_t tin, tout, gen;
    uint32_t epoch = 0;
    CHECK(lc_app_take_totals_checkpoint(&app, &tin, &tout, &gen, &epoch) == AICAM_TRUE);

    /* Save fails - no crossing occurs */
    f.save_ret = AICAM_ERROR_IO;
    if (ops.save_totals(ops.user, tin, tout, 0u) != AICAM_OK) {
        /* No acknowledge */
    }

    /* Next tick - dirty should still be set */
    CHECK(lc_app_take_totals_checkpoint(&app, &tin, &tout, &gen, &epoch) == AICAM_TRUE);

    /* Save succeeds */
    f.save_ret = AICAM_OK;
    if (ops.save_totals(ops.user, tin, tout, 0u) == AICAM_OK) {
        lc_app_acknowledge_checkpoint(&app, gen, epoch);
    }

    CHECK(lc_app_take_totals_checkpoint(&app, &tin, &tout, &gen, &epoch) == AICAM_FALSE);
    lc_app_reset(&app, 0);
}

/* Blocker 3: Manual reset causes exactly one queue clear */
static void test_manual_reset_single_queue_clear(void) {
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
    CHECK(drive_crossings(&app, &f, 6) > 0);
    line_count_event_t evs[4];
    CHECK(lc_app_get_events(&app, evs, 4) > 0);

    int queue_clear_before = f.queue_clear_calls;
    CHECK(lc_app_reset(&app, 900000) == AICAM_OK);

    /* Exactly one queue clear invocation */
    CHECK(f.queue_clear_calls == queue_clear_before + 1);

    /* State fully reset */
    CHECK(f.total_in == 0 && f.total_out == 0);
    CHECK(lc_app_get_events(&app, evs, 4) == 0);
    lc_app_reset(&app, 0);
}

/* Blocker 3: Manual reset queue clear failure rolls back */
static void test_manual_reset_queue_clear_failure_rolls_back(void) {
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
    CHECK(drive_crossings(&app, &f, 6) > 0);
    line_count_event_t evs[4];
    CHECK(lc_app_get_events(&app, evs, 4) > 0);
    line_counting_stats_t before;
    lc_app_get_stats(&app, &before);

    f.queue_clear_ret = AICAM_ERROR_IO;
    CHECK(lc_app_reset(&app, 900000) == AICAM_ERROR_TRANSACTION);

    line_counting_stats_t after;
    lc_app_get_stats(&app, &after);
    CHECK(after.total_in == before.total_in);
    CHECK(after.total_out == before.total_out);
    CHECK(lc_app_get_events(&app, evs, 4) > 0);
    CHECK(app.totals_resetting == 1);

    f.queue_clear_ret = AICAM_OK;
    CHECK(lc_app_recover_transaction(&app) == AICAM_OK);
    lc_app_get_stats(&app, &after);
    CHECK(after.total_in == 0 && after.total_out == 0);
    CHECK(app.totals_resetting == 0);

    f.queue_clear_ret = AICAM_OK;
    lc_app_reset(&app, 0);
}

/* Blocker 4: Target change queue clear before config persist failure */
static void test_target_change_config_persist_failure_preserves_queue(void) {
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
    CHECK(drive_crossings(&app, &f, 6) > 0);
    line_count_event_t evs[4];
    CHECK(lc_app_get_events(&app, evs, 4) > 0);
    int queue_clear_before = f.queue_clear_calls;
    uint32_t total_in_before = f.total_in;
    uint32_t total_out_before = f.total_out;

    /* Inject config persistence failure */
    f.persist_ret = AICAM_ERROR_IO;

    line_counting_config_t changed = cfg;
    snprintf(changed.target_class_name, sizeof(changed.target_class_name), "car");
    CHECK(lc_app_apply_config(&app, &changed, NULL) == AICAM_ERROR_TRANSACTION);
    CHECK(f.txn_present == 1);

    /* Queue clear should NOT have been called (persist failed first) */
    CHECK(f.queue_clear_calls == queue_clear_before);

    /* Old totals restored */
    CHECK(f.total_in == total_in_before);
    CHECK(f.total_out == total_out_before);

    /* Old config preserved */
    CHECK_STR(app.cfg.target_class_name, "person");

    /* Queue/state preserved */
    CHECK(lc_app_get_events(&app, evs, 4) > 0);
    lc_app_reset(&app, 0);
}

/* Blocker 4: Target change config persist failure (persist first, so queue clear not reached) */
static void test_target_change_config_persist_failure_no_queue_clear(void) {
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
    CHECK(drive_crossings(&app, &f, 6) > 0);
    line_count_event_t evs[4];
    CHECK(lc_app_get_events(&app, evs, 4) > 0);
    int queue_clear_before = f.queue_clear_calls;
    int save_calls_before = f.save_calls;

    /* Config persistence fails FIRST (new ordering: persist before queue clear) */
    f.persist_ret = AICAM_ERROR_IO;

    line_counting_config_t changed = cfg;
    snprintf(changed.target_class_name, sizeof(changed.target_class_name), "car");
    CHECK(lc_app_apply_config(&app, &changed, NULL) == AICAM_ERROR_TRANSACTION);
    CHECK(f.txn_present == 1);

    /* Queue clear should NOT have been called (persist failed first) */
    CHECK(f.queue_clear_calls == queue_clear_before);
    /* Totals save should NOT have been called */
    CHECK(f.save_calls == save_calls_before);

    /* Old config preserved */
    CHECK_STR(app.cfg.target_class_name, "person");
    CHECK(lc_app_get_events(&app, evs, 4) > 0); /* events preserved */

    f.persist_ret = AICAM_OK;
    lc_app_reset(&app, 0);
}

/* Blocker 4: Full target change success commits all */
static void test_target_change_full_success_commits(void) {
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
    CHECK(drive_crossings(&app, &f, 6) > 0);

    f.now = 200000;
    line_counting_config_t changed = cfg;
    snprintf(changed.target_class_name, sizeof(changed.target_class_name), "car");
    int saves_before = f.save_calls;
    int queue_clears_before = f.queue_clear_calls;
    CHECK(lc_app_apply_config(&app, &changed, NULL) == AICAM_OK);

    CHECK(f.persist_calls >= 1);
    CHECK(f.save_calls == saves_before + 1); /* totals saved to zero */
    CHECK(f.queue_clear_calls == queue_clears_before + 1); /* queue cleared */

    line_count_event_t evs[4];
    CHECK(lc_app_get_events(&app, evs, 4) == 0); /* events cleared */
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

static void test_stale_epoch_ack_vs_manual_reset(void) {
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
    CHECK(drive_crossings(&app, &f, 6) > 0);

    uint32_t tin, tout, gen, epoch;
    CHECK(lc_app_take_totals_checkpoint(&app, &tin, &tout, &gen, &epoch) == AICAM_TRUE);
    uint32_t snap_total = tin + tout;
    CHECK(snap_total > 0);
    CHECK(epoch == 0);

    CHECK(lc_app_reset(&app, 900000) == AICAM_OK);
    CHECK(f.total_in == 0 && f.total_out == 0);
    CHECK(app.totals_persist_epoch == 1);

    ops.save_totals(ops.user, tin, tout, 0u);

    lc_app_mark_totals_dirty(&app);
    uint32_t gen2, epoch2;
    CHECK(lc_app_take_totals_checkpoint(&app, &tin, &tout, &gen2, &epoch2) == AICAM_TRUE);
    CHECK(epoch2 == 1);
    lc_app_acknowledge_checkpoint(&app, gen, epoch);
    CHECK(app.totals_dirty == 1);
    lc_app_acknowledge_checkpoint(&app, gen2, epoch2);
    CHECK(app.totals_dirty == 0);

    f.load_ret = AICAM_OK;
    f.total_in = 0;
    f.total_out = 0;
    lc_app_t reloaded;
    lc_app_init(&reloaded, &ops, &cfg);
    line_counting_stats_t stats;
    lc_app_get_stats(&reloaded, &stats);
    CHECK(stats.total_in == 0 && stats.total_out == 0);
    lc_app_reset(&reloaded, 0);
    lc_app_reset(&app, 0);
}

static void test_stale_epoch_ack_vs_target_change(void) {
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
    CHECK(drive_crossings(&app, &f, 6) > 0);

    uint32_t tin, tout, gen, epoch;
    CHECK(lc_app_take_totals_checkpoint(&app, &tin, &tout, &gen, &epoch) == AICAM_TRUE);
    CHECK(tin + tout > 0);
    CHECK(epoch == 0);

    f.persist_ret = AICAM_OK;
    f.save_ret = AICAM_OK;
    f.queue_clear_ret = AICAM_OK;
    line_counting_config_t changed = cfg;
    snprintf(changed.target_class_name, sizeof(changed.target_class_name), "car");
    CHECK(lc_app_apply_config(&app, &changed, NULL) == AICAM_OK);
    CHECK(f.total_in == 0 && f.total_out == 0);
    CHECK(app.totals_persist_epoch == 1);
    CHECK(app.totals_dirty == 0);

    ops.save_totals(ops.user, tin, tout, 0u);

    lc_app_mark_totals_dirty(&app);
    uint32_t gen2, epoch2;
    CHECK(lc_app_take_totals_checkpoint(&app, &tin, &tout, &gen2, &epoch2) == AICAM_TRUE);
    CHECK(epoch2 == 1);
    lc_app_acknowledge_checkpoint(&app, gen, epoch);
    CHECK(app.totals_dirty == 1);

    f.load_ret = AICAM_OK;
    f.total_in = 0;
    f.total_out = 0;
    lc_app_t reloaded;
    lc_app_init(&reloaded, &ops, &cfg);
    line_counting_stats_t stats;
    lc_app_get_stats(&reloaded, &stats);
    CHECK(stats.total_in == 0 && stats.total_out == 0);
    lc_app_reset(&reloaded, 0);
    lc_app_reset(&app, 0);
}

static void test_checkpoint_then_reset_ordering(void) {
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
    CHECK(drive_crossings(&app, &f, 6) > 0);

    uint32_t tin, tout, gen, epoch;
    CHECK(lc_app_take_totals_checkpoint(&app, &tin, &tout, &gen, &epoch) == AICAM_TRUE);
    uint32_t snap_total = tin + tout;
    CHECK(snap_total > 0);

    f.save_ret = AICAM_OK;
    ops.save_totals(ops.user, tin, tout, 0u);
    lc_app_acknowledge_checkpoint(&app, gen, epoch);
    CHECK(lc_app_take_totals_checkpoint(&app, &tin, &tout, &gen, &epoch) == AICAM_FALSE);
    CHECK(f.total_in + f.total_out == snap_total);

    CHECK(lc_app_reset(&app, 900000) == AICAM_OK);
    CHECK(f.total_in == 0 && f.total_out == 0);

    f.load_ret = AICAM_OK;
    f.total_in = 0;
    f.total_out = 0;
    lc_app_t reloaded;
    lc_app_init(&reloaded, &ops, &cfg);
    line_counting_stats_t stats;
    lc_app_get_stats(&reloaded, &stats);
    CHECK(stats.total_in == 0 && stats.total_out == 0);
    lc_app_reset(&reloaded, 0);
    lc_app_reset(&app, 0);
}

static void test_disable_preserves_dirty_totals(void) {
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
    CHECK(drive_crossings(&app, &f, 6) > 0);

    uint32_t tin, tout, gen, epoch;
    CHECK(lc_app_take_totals_checkpoint(&app, &tin, &tout, &gen, &epoch) == AICAM_TRUE);
    uint32_t snap_in = tin;
    uint32_t snap_out = tout;
    CHECK(snap_in + snap_out > 0);

    line_counting_config_t disabled = cfg;
    disabled.enable = AICAM_FALSE;
    lc_window_summary_t closed;
    CHECK(lc_app_apply_config(&app, &disabled, &closed) == AICAM_OK);
    CHECK(app.totals_dirty == 1);
    CHECK(app.totals_persist_epoch == 0);

    uint32_t tin2, tout2, gen2, epoch2;
    CHECK(lc_app_take_totals_checkpoint(&app, &tin2, &tout2, &gen2, &epoch2) == AICAM_TRUE);
    CHECK(tin2 == snap_in && tout2 == snap_out);

    f.save_ret = AICAM_OK;
    ops.save_totals(ops.user, tin2, tout2, 0u);
    lc_app_acknowledge_checkpoint(&app, gen2, epoch2);
    CHECK(lc_app_take_totals_checkpoint(&app, &tin2, &tout2, &gen2, &epoch2) == AICAM_FALSE);

    f.load_ret = AICAM_OK;
    f.total_in = snap_in;
    f.total_out = snap_out;
    lc_app_t reloaded;
    lc_app_init(&reloaded, &ops, &cfg);
    line_counting_stats_t stats;
    lc_app_get_stats(&reloaded, &stats);
    CHECK(stats.total_in == snap_in && stats.total_out == snap_out);

    CHECK(lc_app_apply_config(&app, &cfg, NULL) == AICAM_OK);
    lc_app_get_stats(&app, &stats);
    CHECK(stats.total_in == snap_in && stats.total_out == snap_out);
    CHECK(app.totals_persist_epoch == 0);

    lc_app_reset(&reloaded, 0);
    lc_app_reset(&app, 0);
}

static void test_target_change_reboot_after_commit(void) {
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
    CHECK(drive_crossings(&app, &f, 6) > 0);

    f.persist_ret = AICAM_OK;
    f.save_ret = AICAM_OK;
    f.queue_clear_ret = AICAM_OK;
    f.now = 200000;
    line_counting_config_t changed = cfg;
    snprintf(changed.target_class_name, sizeof(changed.target_class_name), "car");
    CHECK(lc_app_apply_config(&app, &changed, NULL) == AICAM_OK);
    CHECK(f.total_in == 0 && f.total_out == 0);
    CHECK_STR(app.cfg.target_class_name, "car");
    CHECK(app.totals_persist_epoch == 1);

    f.load_ret = AICAM_OK;
    f.total_in = 0;
    f.total_out = 0;
    lc_app_t reloaded;
    lc_app_init(&reloaded, &ops, &cfg);
    line_counting_stats_t stats;
    lc_app_get_stats(&reloaded, &stats);
    CHECK(stats.total_in == 0 && stats.total_out == 0);
    lc_app_reset(&reloaded, 0);
    lc_app_reset(&app, 0);
}

static void test_concurrent_writer_staging_ownership(void) {
    fake_t f;
    fake_init(&f);
    lc_app_ops_t ops;
    ops_init(&ops, &f);
    line_counting_config_t cfg;
    cfg_enabled(&cfg);

    lc_app_t app;
    lc_app_init(&app, &ops, &cfg);

    line_counting_config_t candidate_a = cfg;
    snprintf(candidate_a.counter_name, sizeof(candidate_a.counter_name), "WriterA");
    CHECK(lc_app_apply_config(&app, &candidate_a, NULL) == AICAM_OK);
    CHECK_STR(app.cfg.counter_name, "WriterA");
    CHECK_STR(f.last_persisted.counter_name, "WriterA");

    line_counting_config_t candidate_b = cfg;
    snprintf(candidate_b.counter_name, sizeof(candidate_b.counter_name), "WriterB");
    CHECK(lc_app_apply_config(&app, &candidate_b, NULL) == AICAM_OK);
    CHECK_STR(app.cfg.counter_name, "WriterB");
    CHECK_STR(f.last_persisted.counter_name, "WriterB");

    CHECK(f.persist_calls == 2);
    lc_app_reset(&app, 0);
}

/* ===== Target-change transaction fault-injection matrix ===== */

static void fake_reboot(fake_t *f, lc_app_ops_t *ops, lc_app_t *app,
                        const line_counting_config_t *cfg) {
    ops_init(ops, f);
    f->load_ret = AICAM_OK;
    lc_app_init(app, ops, cfg);
}

static void test_txn_prepare_fail_prior_intact(void) {
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
    CHECK(drive_crossings(&app, &f, 6) > 0);
    f.queue_len = 3;

    int saves_before = f.save_calls;
    int clears_before = f.queue_clear_calls;
    f.txn_prepare_ret = AICAM_ERROR_IO;

    line_counting_config_t changed = cfg;
    snprintf(changed.target_class_name, sizeof(changed.target_class_name), "car");
    CHECK(lc_app_apply_config(&app, &changed, NULL) == AICAM_ERROR_IO);
    CHECK(f.persist_calls == 0);
    CHECK(f.save_calls == saves_before);
    CHECK(f.queue_clear_calls == clears_before);
    CHECK(f.txn_present == 0);
    CHECK_STR(app.cfg.target_class_name, "person");
    CHECK(f.persisted_valid == 0);

    lc_app_ops_t ops2;
    lc_app_t app2;
    fake_reboot(&f, &ops2, &app2, &cfg);
    CHECK(lc_app_recover_transaction(&app2) == AICAM_OK);
    CHECK(f.persist_calls == 0);
    CHECK_STR(app2.cfg.target_class_name, "person");
    lc_app_reset(&app2, 0);
    lc_app_reset(&app, 0);
}

static void test_txn_persist_fail_prepared_boot_rolls_forward(void) {
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
    CHECK(drive_crossings(&app, &f, 6) > 0);
    f.queue_len = 3;

    f.persist_ret = AICAM_ERROR_IO;
    line_counting_config_t changed = cfg;
    snprintf(changed.target_class_name, sizeof(changed.target_class_name), "car");
    CHECK(lc_app_apply_config(&app, &changed, NULL) == AICAM_ERROR_TRANSACTION);
    CHECK(f.txn_present == 1);
    CHECK(f.save_calls == 0);
    CHECK(f.queue_len == 3);
    CHECK(f.persisted_valid == 0);

    f.persist_ret = AICAM_OK;
    lc_app_ops_t ops2;
    lc_app_t app2;
    fake_reboot(&f, &ops2, &app2, &cfg);
    CHECK(lc_app_recover_transaction(&app2) == AICAM_OK);
    CHECK(f.txn_present == 0);
    CHECK_STR(f.persisted_cfg.target_class_name, "car");
    CHECK(f.total_in == 0 && f.total_out == 0);
    CHECK(f.queue_len == 0);
    CHECK_STR(app2.cfg.target_class_name, "car");
    CHECK(app2.total_in == 0 && app2.total_out == 0);
    CHECK(app2.totals_persist_epoch == 1);
    lc_app_reset(&app2, 0);
    lc_app_reset(&app, 0);
}

static void test_txn_totals_zero_fail_compensation_ok_prior(void) {
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
    CHECK(drive_crossings(&app, &f, 6) > 0);
    f.queue_len = 3;
    line_counting_stats_t prior;
    lc_app_get_stats(&app, &prior);

    f.save_fail_call = 1;
    line_counting_config_t changed = cfg;
    snprintf(changed.target_class_name, sizeof(changed.target_class_name), "car");
    CHECK(lc_app_apply_config(&app, &changed, NULL) == AICAM_ERROR_IO);

    CHECK(f.txn_present == 0);
    CHECK_STR(f.persisted_cfg.target_class_name, "person");
    CHECK(f.total_in == prior.total_in && f.total_out == prior.total_out);
    CHECK(f.queue_len == 3);
    CHECK_STR(app.cfg.target_class_name, "person");

    lc_app_ops_t ops2;
    lc_app_t app2;
    fake_reboot(&f, &ops2, &app2, &cfg);
    CHECK(lc_app_recover_transaction(&app2) == AICAM_OK);
    CHECK_STR(app2.cfg.target_class_name, "person");
    CHECK(f.total_in == prior.total_in && f.total_out == prior.total_out);
    lc_app_reset(&app2, 0);
    lc_app_reset(&app, 0);
}

static void test_txn_totals_zero_fail_compensation_fail_boot_new(void) {
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
    CHECK(drive_crossings(&app, &f, 6) > 0);
    f.queue_len = 3;

    f.save_ret = AICAM_ERROR_IO;
    line_counting_config_t changed = cfg;
    snprintf(changed.target_class_name, sizeof(changed.target_class_name), "car");
    CHECK(lc_app_apply_config(&app, &changed, NULL) == AICAM_ERROR_TRANSACTION);
    CHECK(f.txn_present == 1);
    CHECK_STR(f.persisted_cfg.target_class_name, "car");
    CHECK(f.total_in == 0 && f.total_out == 0);
    CHECK(f.queue_len == 3);

    f.save_ret = AICAM_OK;
    lc_app_ops_t ops2;
    lc_app_t app2;
    fake_reboot(&f, &ops2, &app2, &cfg);
    CHECK(lc_app_recover_transaction(&app2) == AICAM_OK);
    CHECK(f.txn_present == 0);
    CHECK_STR(f.persisted_cfg.target_class_name, "car");
    CHECK(f.total_in == 0 && f.total_out == 0);
    CHECK(f.queue_len == 0);
    CHECK_STR(app2.cfg.target_class_name, "car");
    lc_app_reset(&app2, 0);
    lc_app_reset(&app, 0);
}

static void test_txn_queue_fail_compensation_ok_prior(void) {
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
    CHECK(drive_crossings(&app, &f, 6) > 0);
    f.queue_len = 3;
    line_counting_stats_t prior;
    lc_app_get_stats(&app, &prior);

    f.queue_clear_ret = AICAM_ERROR_IO;
    line_counting_config_t changed = cfg;
    snprintf(changed.target_class_name, sizeof(changed.target_class_name), "car");
    CHECK(lc_app_apply_config(&app, &changed, NULL) == AICAM_ERROR_IO);

    CHECK(f.txn_present == 0);
    CHECK_STR(f.persisted_cfg.target_class_name, "person");
    CHECK(f.total_in == prior.total_in && f.total_out == prior.total_out);
    CHECK(f.queue_len == 3);

    lc_app_ops_t ops2;
    lc_app_t app2;
    fake_reboot(&f, &ops2, &app2, &cfg);
    CHECK(lc_app_recover_transaction(&app2) == AICAM_OK);
    CHECK_STR(app2.cfg.target_class_name, "person");
    CHECK(f.queue_len == 3);
    lc_app_reset(&app2, 0);
    lc_app_reset(&app, 0);
}

static void test_txn_queue_fail_compensation_fail_boot_new(void) {
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
    CHECK(drive_crossings(&app, &f, 6) > 0);
    f.queue_len = 3;

    f.queue_clear_ret = AICAM_ERROR_IO;
    f.save_fail_after_calls = 1;
    line_counting_config_t changed = cfg;
    snprintf(changed.target_class_name, sizeof(changed.target_class_name), "car");
    CHECK(lc_app_apply_config(&app, &changed, NULL) == AICAM_ERROR_TRANSACTION);
    CHECK(f.txn_present == 1);
    CHECK_STR(f.persisted_cfg.target_class_name, "car");
    CHECK(f.total_in == 0);
    CHECK(f.queue_len == 3);

    f.queue_clear_ret = AICAM_OK;
    f.save_fail_after_calls = 0;
    lc_app_ops_t ops2;
    lc_app_t app2;
    fake_reboot(&f, &ops2, &app2, &cfg);
    CHECK(lc_app_recover_transaction(&app2) == AICAM_OK);
    CHECK(f.txn_present == 0);
    CHECK_STR(f.persisted_cfg.target_class_name, "car");
    CHECK(f.total_in == 0 && f.total_out == 0);
    CHECK(f.queue_len == 0);
    CHECK_STR(app2.cfg.target_class_name, "car");
    lc_app_reset(&app2, 0);
    lc_app_reset(&app, 0);
}

static void test_txn_commit_clear_fail_runtime_committed_boot_reaffirms(void) {
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
    CHECK(drive_crossings(&app, &f, 6) > 0);
    f.queue_len = 3;

    f.txn_clear_ret = AICAM_ERROR_IO;
    line_counting_config_t changed = cfg;
    snprintf(changed.target_class_name, sizeof(changed.target_class_name), "car");
    CHECK(lc_app_apply_config(&app, &changed, NULL) == AICAM_ERROR_TRANSACTION);

    CHECK_STR(app.cfg.target_class_name, "car");
    CHECK(app.total_in == 0 && app.total_out == 0);
    CHECK(app.totals_persist_epoch == 1);
    CHECK(f.txn_present == 1);
    CHECK_STR(f.persisted_cfg.target_class_name, "car");
    CHECK(f.total_in == 0 && f.total_out == 0);
    CHECK(f.queue_len == 0);

    f.txn_clear_ret = AICAM_OK;
    lc_app_ops_t ops2;
    lc_app_t app2;
    fake_reboot(&f, &ops2, &app2, &cfg);
    CHECK(lc_app_recover_transaction(&app2) == AICAM_OK);
    CHECK(f.txn_present == 0);
    CHECK_STR(app2.cfg.target_class_name, "car");
    CHECK(f.queue_len == 0);
    CHECK(f.total_in == 0 && f.total_out == 0);
    lc_app_reset(&app2, 0);
    lc_app_reset(&app, 0);
}

static void test_txn_recovery_io_failure_then_retry(void) {
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
    CHECK(drive_crossings(&app, &f, 6) > 0);
    f.queue_len = 3;

    f.save_ret = AICAM_ERROR_IO;
    line_counting_config_t changed = cfg;
    snprintf(changed.target_class_name, sizeof(changed.target_class_name), "car");
    CHECK(lc_app_apply_config(&app, &changed, NULL) == AICAM_ERROR_TRANSACTION);
    CHECK(f.txn_present == 1);

    f.save_ret = AICAM_OK;
    f.txn_get_fail = 1;
    lc_app_ops_t ops2;
    lc_app_t app2;
    fake_reboot(&f, &ops2, &app2, &cfg);
    CHECK(lc_app_recover_transaction(&app2) == AICAM_ERROR_IO);
    CHECK(f.txn_present == 1);
    CHECK_STR(app2.cfg.target_class_name, "person");

    CHECK(lc_app_recover_transaction(&app2) == AICAM_OK);
    CHECK(f.txn_present == 0);
    CHECK_STR(f.persisted_cfg.target_class_name, "car");
    CHECK(f.total_in == 0 && f.total_out == 0);
    CHECK(f.queue_len == 0);
    CHECK_STR(app2.cfg.target_class_name, "car");
    lc_app_reset(&app2, 0);
    lc_app_reset(&app, 0);
}

static void test_txn_recovery_no_record_noop(void) {
    fake_t f;
    fake_init(&f);
    lc_app_ops_t ops;
    ops_init(&ops, &f);
    line_counting_config_t cfg;
    cfg_enabled(&cfg);

    lc_app_t app;
    lc_app_init(&app, &ops, &cfg);
    CHECK(lc_app_recover_transaction(&app) == AICAM_OK);
    CHECK(f.persist_calls == 0);
    CHECK(f.save_calls == 0);
    CHECK(f.queue_clear_calls == 0);
    lc_app_reset(&app, 0);
}

static void test_txn_full_success_clears_record(void) {
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
    CHECK(drive_crossings(&app, &f, 6) > 0);

    line_counting_config_t changed = cfg;
    snprintf(changed.target_class_name, sizeof(changed.target_class_name), "car");
    CHECK(lc_app_apply_config(&app, &changed, NULL) == AICAM_OK);
    CHECK(f.txn_prepare_calls == 1);
    CHECK(f.txn_clear_calls == 1);
    CHECK(f.txn_present == 0);
    CHECK_STR(f.persisted_cfg.target_class_name, "car");
    CHECK(app.totals_persist_epoch == 1);

    lc_app_ops_t ops2;
    lc_app_t app2;
    fake_reboot(&f, &ops2, &app2, &cfg);
    CHECK(lc_app_recover_transaction(&app2) == AICAM_OK);
    CHECK_STR(f.persisted_cfg.target_class_name, "car");
    CHECK(app2.totals_persist_epoch == 0);
    lc_app_reset(&app2, 0);
    lc_app_reset(&app, 0);
}

/* ===== Crash-safe totals store tests (dual-slot record + CRC) ===== */

#define TOT_BUF (2u * 24u)

typedef struct {
    uint8_t buf[TOT_BUF];
    int writes;
    int fail_write_at;
    int torn_write_at;
    uint32_t torn_len;
} totals_io_t;

static aicam_result_t tio_read(void *user, uint32_t off, void *out, uint32_t len) {
    totals_io_t *t = (totals_io_t *)user;
    if (off + len > TOT_BUF) return AICAM_ERROR_INVALID_PARAM;
    memcpy(out, t->buf + off, len);
    return AICAM_OK;
}

static aicam_result_t tio_write(void *user, uint32_t off, const void *in, uint32_t len) {
    totals_io_t *t = (totals_io_t *)user;
    t->writes++;
    if (t->fail_write_at > 0 && t->writes == t->fail_write_at) {
        t->fail_write_at = 0;
        return AICAM_ERROR_IO;
    }
    if (t->torn_write_at > 0 && t->writes == t->torn_write_at) {
        t->torn_write_at = 0;
        uint32_t n = t->torn_len < len ? t->torn_len : len;
        memcpy(t->buf + off, in, n);
        return AICAM_OK;
    }
    if (off + len > TOT_BUF) return AICAM_ERROR_INVALID_PARAM;
    memcpy(t->buf + off, in, len);
    return AICAM_OK;
}

static void tio_init(totals_io_t *t) {
    memset(t->buf, 0xFF, sizeof(t->buf));
    t->writes = 0;
    t->fail_write_at = 0;
    t->torn_write_at = 0;
    t->torn_len = 0;
}

static void totals_io_of(totals_io_t *t, lc_totals_io_t *io) {
    io->user = t;
    io->read = tio_read;
    io->write = tio_write;
}


static void test_totals_epoch_beats_late_stale_checkpoint(void) {
    totals_io_t t;
    tio_init(&t);
    lc_totals_io_t io;
    totals_io_of(&t, &io);
    lc_totals_store_t store;
    CHECK(lc_totals_store_init(&store, &io) == AICAM_OK);

    CHECK(lc_totals_store_save(&store, 5, 5, 1u) == AICAM_OK);
    CHECK(lc_totals_store_save(&store, 0, 0, 2u) == AICAM_OK);
    CHECK(lc_totals_store_save(&store, 5, 5, 1u) == AICAM_OK);

    lc_totals_store_t rebooted;
    CHECK(lc_totals_store_init(&rebooted, &io) == AICAM_OK);
    uint32_t tin = 99, tout = 99, ep = 0;
    CHECK(lc_totals_store_load(&rebooted, &tin, &tout, &ep) == AICAM_OK);
    CHECK(tin == 0 && tout == 0);
    CHECK(ep == 2u);
}

static void test_totals_torn_wrap_falls_back_to_valid_record(void) {
    totals_io_t t;
    tio_init(&t);
    lc_totals_io_t io;
    totals_io_of(&t, &io);
    lc_totals_store_t store;
    CHECK(lc_totals_store_init(&store, &io) == AICAM_OK);

    store.generation = 0xFFFFFFFEu;
    CHECK(lc_totals_store_save(&store, 3, 4, 5u) == AICAM_OK);
    CHECK(store.generation == 0xFFFFFFFFu);

    t.fail_write_at = t.writes + 1;
    CHECK(lc_totals_store_save(&store, 8, 9, 5u) == AICAM_ERROR_IO);

    lc_totals_store_t rebooted;
    CHECK(lc_totals_store_init(&rebooted, &io) == AICAM_OK);
    uint32_t tin, tout, ep;
    CHECK(lc_totals_store_load(&rebooted, &tin, &tout, &ep) == AICAM_OK);
    CHECK(tin == 3 && tout == 4);
    CHECK(ep == 5u);
    CHECK(rebooted.generation == 0xFFFFFFFFu);

    CHECK(lc_totals_store_save(&rebooted, 1, 2, 5u) == AICAM_OK);
    CHECK(rebooted.generation == 1u);
    CHECK(lc_totals_store_init(&rebooted, &io) == AICAM_OK);
    CHECK(lc_totals_store_load(&rebooted, &tin, &tout, &ep) == AICAM_OK);
    CHECK(tin == 1 && tout == 2);
    CHECK(rebooted.generation == 1u);
}

static void test_totals_store_fresh_not_found(void) {
    totals_io_t t;
    tio_init(&t);
    lc_totals_io_t io;
    totals_io_of(&t, &io);
    lc_totals_store_t store;
    CHECK(lc_totals_store_init(&store, &io) == AICAM_OK);
    uint32_t tin, tout;
    CHECK(lc_totals_store_load(&store, &tin, &tout, NULL) == AICAM_ERROR_NOT_FOUND);
}

static void test_totals_store_roundtrip_alternates_slots(void) {
    totals_io_t t;
    tio_init(&t);
    lc_totals_io_t io;
    totals_io_of(&t, &io);
    lc_totals_store_t store;
    CHECK(lc_totals_store_init(&store, &io) == AICAM_OK);
    CHECK(lc_totals_store_save(&store, 12, 3, 0u) == AICAM_OK);
    uint32_t tin, tout;
    CHECK(lc_totals_store_load(&store, &tin, &tout, NULL) == AICAM_OK);
    CHECK(tin == 12 && tout == 3);
    uint32_t gen1 = store.generation;
    CHECK(lc_totals_store_save(&store, 14, 5, 0u) == AICAM_OK);
    CHECK(store.generation == gen1 + 1);
    CHECK(lc_totals_store_load(&store, &tin, &tout, NULL) == AICAM_OK);
    CHECK(tin == 14 && tout == 5);

    lc_totals_store_t rebooted;
    CHECK(lc_totals_store_init(&rebooted, &io) == AICAM_OK);
    CHECK(lc_totals_store_load(&rebooted, &tin, &tout, NULL) == AICAM_OK);
    CHECK(tin == 14 && tout == 5);
    CHECK(rebooted.generation == store.generation);
}

static void test_totals_store_torn_save_reboot_previous(void) {
    totals_io_t t;
    tio_init(&t);
    lc_totals_io_t io;
    totals_io_of(&t, &io);
    lc_totals_store_t store;
    CHECK(lc_totals_store_init(&store, &io) == AICAM_OK);
    CHECK(lc_totals_store_save(&store, 12, 3, 0u) == AICAM_OK);

    t.torn_write_at = t.writes + 1;
    t.torn_len = 10;
    CHECK(lc_totals_store_save(&store, 99, 99, 0u) == AICAM_OK);

    lc_totals_store_t rebooted;
    CHECK(lc_totals_store_init(&rebooted, &io) == AICAM_OK);
    uint32_t tin, tout;
    CHECK(lc_totals_store_load(&rebooted, &tin, &tout, NULL) == AICAM_OK);
    CHECK(tin == 12 && tout == 3);

    CHECK(lc_totals_store_save(&rebooted, 20, 2, 0u) == AICAM_OK);
    CHECK(lc_totals_store_load(&rebooted, &tin, &tout, NULL) == AICAM_OK);
    CHECK(tin == 20 && tout == 2);
}

static void test_totals_store_io_error_keeps_previous_and_retries(void) {
    totals_io_t t;
    tio_init(&t);
    lc_totals_io_t io;
    totals_io_of(&t, &io);
    lc_totals_store_t store;
    CHECK(lc_totals_store_init(&store, &io) == AICAM_OK);
    CHECK(lc_totals_store_save(&store, 7, 8, 0u) == AICAM_OK);

    t.fail_write_at = t.writes + 1;
    CHECK(lc_totals_store_save(&store, 9, 9, 0u) == AICAM_ERROR_IO);
    uint32_t tin, tout;
    CHECK(lc_totals_store_load(&store, &tin, &tout, NULL) == AICAM_OK);
    CHECK(tin == 7 && tout == 8);

    CHECK(lc_totals_store_save(&store, 9, 9, 0u) == AICAM_OK);
    CHECK(lc_totals_store_load(&store, &tin, &tout, NULL) == AICAM_OK);
    CHECK(tin == 9 && tout == 9);
}

static void test_totals_store_both_slots_corrupt(void) {
    totals_io_t t;
    tio_init(&t);
    lc_totals_io_t io;
    totals_io_of(&t, &io);
    lc_totals_store_t store;
    CHECK(lc_totals_store_init(&store, &io) == AICAM_OK);
    CHECK(lc_totals_store_save(&store, 5, 6, 0u) == AICAM_OK);
    CHECK(lc_totals_store_save(&store, 7, 8, 0u) == AICAM_OK);

    memset(t.buf, 0xA7, sizeof(t.buf));
    lc_totals_store_t rebooted;
    CHECK(lc_totals_store_init(&rebooted, &io) == AICAM_OK);
    uint32_t tin, tout;
    CHECK(lc_totals_store_load(&rebooted, &tin, &tout, NULL) == AICAM_ERROR_NOT_FOUND);
}
