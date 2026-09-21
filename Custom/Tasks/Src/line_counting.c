#include "line_counting.h"
#include <string.h>
#include <stdio.h>

#ifndef __LC_TEST__
#include "ai_service.h"
#include "json_config_mgr.h"
#include "storage.h"
#include "debug.h"
#include "cmsis_os2.h"
#include "common_utils.h"
#include "cJSON.h"
#endif

static void lc_clear_transient(lc_app_t *app) {
    if (app->tracker) {
        app->next_id_carry = lc_tracker_next_id(app->tracker);
        lc_tracker_destroy(app->tracker);
        app->tracker = NULL;
    }
    app->events_head = 0;
    app->events_count = 0;
}

static uint16_t lc_resolve_target(lc_app_t *app, const lc_runtime_model_info_t *info) {
    app->binding.generation = info->generation;
    app->binding.loaded = info->loaded;
    app->binding.result_type = info->result_type;
    app->binding.target_class_index = -1;
    app->binding.target_class_name[0] = '\0';

    if (!info->loaded) {
        app->state = LC_STATE_UNSUPPORTED_MODEL;
        app->reason = LC_REASON_MODEL_NOT_LOADED;
        return 0;
    }
    if (info->result_type != PP_TYPE_OD) {
        app->state = LC_STATE_UNSUPPORTED_MODEL;
        app->reason = LC_REASON_NONE;
        return 0;
    }

    char name[LC_TARGET_CLASS_NAME_LEN];
    for (uint16_t i = 0; i < info->num_classes; i++) {
        if (app->ops.get_class_name(app->ops.user, i, name, sizeof(name)) != AICAM_OK) {
            app->state = LC_STATE_UNSUPPORTED_MODEL;
            app->reason = LC_REASON_CLASS_METADATA_UNAVAILABLE;
            return 0;
        }
        if (strcmp(name, app->cfg.target_class_name) == 0) {
            snprintf(app->binding.target_class_name,
                     sizeof(app->binding.target_class_name), "%s", name);
            app->binding.target_class_index = (int16_t)i;
            break;
        }
    }

    if (app->binding.target_class_index < 0) {
        app->state = LC_STATE_TARGET_CLASS_INVALID;
        app->reason = LC_REASON_NONE;
        return 0;
    }

    app->state = LC_STATE_RUNNING;
    app->reason = LC_REASON_NONE;
    return 1;
}

static void lc_rebind(lc_app_t *app) {
    lc_runtime_model_info_t info;
    if (app->ops.get_model_info(app->ops.user, &info) != AICAM_OK) {
        app->state = LC_STATE_UNSUPPORTED_MODEL;
        app->reason = LC_REASON_MODEL_NOT_LOADED;
        return;
    }

    if (app->binding_valid &&
        app->binding.generation == info.generation &&
        app->binding.loaded == info.loaded) {
        return;
    }

    uint8_t had_binding = app->binding_valid;
    app->binding_valid = 1;
    if (had_binding) {
        lc_clear_transient(app);
    }
    lc_resolve_target(app, &info);
}

static aicam_bool_t lc_ensure_resources(lc_app_t *app) {
    if (!app->line) {
        app->line = lc_line_cross_create(
            app->cfg.line_x1_permille / 1000.0f, app->cfg.line_y1_permille / 1000.0f,
            app->cfg.line_x2_permille / 1000.0f, app->cfg.line_y2_permille / 1000.0f,
            app->cfg.outside_x_permille / 1000.0f, app->cfg.outside_y_permille / 1000.0f);
        if (!app->line) return AICAM_FALSE;
    }
    if (!app->tracker) {
        lc_tracker_config_t tc = {
            .max_dist_permille = app->cfg.max_dist_permille,
            .track_history_k   = app->cfg.track_history_k,
            .max_miss          = app->cfg.max_miss,
            .k_confirm         = app->cfg.k_confirm,
        };
        app->tracker = lc_tracker_create(&tc, app->next_id_carry);
        if (!app->tracker) return AICAM_FALSE;
    }
    return AICAM_TRUE;
}

static void lc_push_event(lc_app_t *app, uint32_t ts_ms, uint32_t track_id,
                          lc_cross_event_t direction) {
    line_count_event_t *e = &app->events[app->events_head];
    e->sequence = ++app->event_seq;
    e->timestamp_ms = ts_ms;
    e->track_id = track_id;
    e->direction = (direction == LC_CROSS_IN) ? LC_DIRECTION_IN : LC_DIRECTION_OUT;
    app->events_head = (uint16_t)((app->events_head + 1u) % LC_EVENTS_RING_CAPACITY);
    if (app->events_count < LC_EVENTS_RING_CAPACITY) app->events_count++;
}

void lc_app_init(lc_app_t *app, const lc_app_ops_t *ops,
                 const line_counting_config_t *initial) {
    if (!app || !ops || !initial) return;
    memset(app, 0, sizeof(*app));
    app->ops = *ops;
    app->cfg = *initial;
    if (!line_counting_config_is_valid(&app->cfg)) {
        line_counting_config_defaults(&app->cfg);
    }
    app->binding_valid = 0;
    app->binding.target_class_index = -1;
    app->next_id_carry = 1;

    uint32_t total_in = 0;
    uint32_t total_out = 0;
    if (app->ops.load_totals &&
        app->ops.load_totals(app->ops.user, &total_in, &total_out) == AICAM_OK) {
        app->total_in = total_in;
        app->total_out = total_out;
    }
    app->window_start_ms = app->ops.now_ms ? app->ops.now_ms(app->ops.user) : 0;

    if (app->cfg.enable) {
        lc_rebind(app);
    } else {
        app->state = LC_STATE_DISABLED;
    }
}

aicam_result_t lc_app_on_ai_result(lc_app_t *app, const lc_frame_input_t *frame,
                                   uint32_t ts_ms) {
    if (!app || !frame) return AICAM_ERROR_INVALID_PARAM;
    if (!app->cfg.enable) {
        app->state = LC_STATE_DISABLED;
        return AICAM_OK;
    }

    lc_rebind(app);
    if (app->state != LC_STATE_RUNNING) return AICAM_OK;
    if (!lc_ensure_resources(app)) return AICAM_ERROR_NO_MEMORY;

    lc_point_t centers[LC_FRAME_MAX_DETECTIONS];
    uint8_t n = 0;
    float conf_thr = app->cfg.conf_threshold_permille / 1000.0f;
    uint8_t limit = frame->nb_detect < LC_FRAME_MAX_DETECTIONS
                        ? frame->nb_detect : LC_FRAME_MAX_DETECTIONS;
    for (uint8_t i = 0; i < limit; i++) {
        const lc_det_t *d = &frame->detects[i];
        if (d->conf < conf_thr) continue;
        if (!d->class_name || strcmp(d->class_name, app->cfg.target_class_name) != 0) continue;
        centers[n].x = d->x + d->w * 0.5f;
        centers[n].y = d->y + d->h * 0.5f;
        n++;
    }

    lc_track_record_t **recs = NULL;
    uint16_t n_recs = 0;
    lc_tracker_update(app->tracker, n > 0 ? centers : NULL, n, ts_ms, &recs, &n_recs);
    for (uint16_t k = 0; k < n_recs; k++) LC_FREE(recs[k]);
    if (recs) LC_FREE(recs);

    lc_cross_evt_t evts[16];
    uint8_t n_evts = 0;
    uint32_t win_in = 0;
    uint32_t win_out = 0;
    lc_tracker_check_line_crossings(app->tracker, app->line, ts_ms,
                                    &win_in, &win_out,
                                    &app->total_in, &app->total_out,
                                    evts, 16, &n_evts);
    app->window_in += win_in;
    app->window_out += win_out;
    for (uint8_t k = 0; k < n_evts; k++) {
        lc_push_event(app, evts[k].ts_ms, evts[k].track_id, evts[k].direction);
    }
    return AICAM_OK;
}

aicam_result_t lc_app_apply_config(lc_app_t *app, const line_counting_config_t *candidate,
                                   lc_window_summary_t *closed_out) {
    if (!app || !candidate) return AICAM_ERROR_INVALID_PARAM;
    if (!line_counting_config_is_valid(candidate)) return AICAM_ERROR_INVALID_DATA;

    uint32_t now = app->ops.now_ms ? app->ops.now_ms(app->ops.user) : 0;
    uint8_t target_changed = strcmp(candidate->target_class_name,
                                    app->cfg.target_class_name) != 0;

    if (target_changed) {
        if (app->ops.save_totals &&
            app->ops.save_totals(app->ops.user, 0, 0) != AICAM_OK) {
            return AICAM_ERROR_IO;
        }
        app->total_in = 0;
        app->total_out = 0;
        app->window_in = 0;
        app->window_out = 0;
        app->window_start_ms = now;
        lc_clear_transient(app);
        app->binding_valid = 0;
    }

    if (candidate->enable && !app->cfg.enable) {
        app->window_start_ms = now;
    }

    if (!candidate->enable && app->cfg.enable) {
        lc_window_summary_t closed;
        closed.start_ms = app->window_start_ms;
        closed.end_ms = now;
        closed.in = app->window_in;
        closed.out = app->window_out;
        app->window_in = 0;
        app->window_out = 0;
        if (closed_out) *closed_out = closed;
        lc_clear_transient(app);
    }

    if (candidate->line_x1_permille != app->cfg.line_x1_permille ||
        candidate->line_y1_permille != app->cfg.line_y1_permille ||
        candidate->line_x2_permille != app->cfg.line_x2_permille ||
        candidate->line_y2_permille != app->cfg.line_y2_permille ||
        candidate->outside_x_permille != app->cfg.outside_x_permille ||
        candidate->outside_y_permille != app->cfg.outside_y_permille) {
        if (app->line) {
            lc_line_cross_destroy(app->line);
            app->line = NULL;
        }
    }
    if (candidate->max_dist_permille != app->cfg.max_dist_permille ||
        candidate->track_history_k != app->cfg.track_history_k ||
        candidate->max_miss != app->cfg.max_miss ||
        candidate->k_confirm != app->cfg.k_confirm) {
        if (app->tracker) {
            lc_tracker_destroy(app->tracker);
            app->tracker = NULL;
        }
    }

    app->cfg = *candidate;

    if (!app->cfg.enable) {
        app->state = LC_STATE_DISABLED;
        app->reason = LC_REASON_NONE;
    } else {
        lc_rebind(app);
    }
    return AICAM_OK;
}

aicam_bool_t lc_app_tick_window(lc_app_t *app, uint32_t now_ms,
                                lc_window_summary_t *closed_out) {
    if (!app || !app->cfg.enable) return AICAM_FALSE;
    uint32_t period_ms = (uint32_t)app->cfg.window_minutes * 60u * 1000u;
    if (period_ms == 0) return AICAM_FALSE;
    if ((uint32_t)(now_ms - app->window_start_ms) < period_ms) return AICAM_FALSE;

    if (closed_out) {
        closed_out->start_ms = app->window_start_ms;
        closed_out->end_ms = now_ms;
        closed_out->in = app->window_in;
        closed_out->out = app->window_out;
    }

    app->window_in = 0;
    app->window_out = 0;
    app->window_start_ms = now_ms;
    return AICAM_TRUE;
}

aicam_result_t lc_app_reset(lc_app_t *app, uint32_t now_ms) {
    if (!app) return AICAM_ERROR_INVALID_PARAM;
    if (app->ops.save_totals &&
        app->ops.save_totals(app->ops.user, 0, 0) != AICAM_OK) {
        return AICAM_ERROR_IO;
    }
    app->total_in = 0;
    app->total_out = 0;
    app->window_in = 0;
    app->window_out = 0;
    app->window_start_ms = now_ms;
    app->event_seq = 0;
    lc_clear_transient(app);

    if (app->cfg.enable) {
        lc_rebind(app);
    } else {
        app->state = LC_STATE_DISABLED;
    }
    return AICAM_OK;
}

void lc_app_get_status(const lc_app_t *app, line_counting_status_t *out) {
    if (!app || !out) return;
    out->state = app->state;
    out->reason = app->reason;
    out->binding = app->binding;
    snprintf(out->counter_name, sizeof(out->counter_name), "%s", app->cfg.counter_name);
    out->tracker_active = lc_tracker_active_count(app->tracker);
}

void lc_app_get_stats(const lc_app_t *app, line_counting_stats_t *out) {
    if (!app || !out) return;
    out->window_in = app->window_in;
    out->window_out = app->window_out;
    out->total_in = app->total_in;
    out->total_out = app->total_out;
    out->window_start_ms = app->window_start_ms;
}

uint16_t lc_app_get_events(const lc_app_t *app, line_count_event_t *out,
                           uint16_t max_events) {
    if (!app || !out || max_events == 0) return 0;
    uint16_t n = 0;
    for (uint16_t i = 0; i < app->events_count && n < max_events; i++) {
        uint16_t idx = (uint16_t)((app->events_head + LC_EVENTS_RING_CAPACITY - 1u - i)
                                  % LC_EVENTS_RING_CAPACITY);
        out[n++] = app->events[idx];
    }
    return n;
}

#ifndef __LC_TEST__

#define LC_TOTALS_PATH "/config/line_counting_totals.json"
#define LC_LEGACY_TOTALS_PATH "/config/pc_totals.json"

static aicam_bool_t lc_parse_totals(const char *buf, uint32_t *total_in, uint32_t *total_out) {
    cJSON *o = cJSON_Parse(buf);
    if (!o) return AICAM_FALSE;
    cJSON *jin = cJSON_GetObjectItem(o, "total_in");
    cJSON *jout = cJSON_GetObjectItem(o, "total_out");
    aicam_bool_t ok = AICAM_FALSE;
    if (cJSON_IsNumber(jin) && cJSON_IsNumber(jout)) {
        *total_in = (uint32_t)jin->valueint;
        *total_out = (uint32_t)jout->valueint;
        ok = AICAM_TRUE;
    }
    cJSON_Delete(o);
    return ok;
}

static aicam_bool_t lc_read_totals_file(const char *path, uint32_t *total_in,
                                        uint32_t *total_out) {
    void *fd = flash_lfs_fopen(path, "r");
    if (!fd) return AICAM_FALSE;
    char buf[80];
    int n = flash_lfs_fread(fd, buf, sizeof(buf) - 1);
    flash_lfs_fclose(fd);
    if (n <= 0) return AICAM_FALSE;
    buf[n] = '\0';
    return lc_parse_totals(buf, total_in, total_out);
}

static aicam_result_t lc_write_totals_file(const char *path, uint32_t total_in,
                                           uint32_t total_out) {
    char buf[80];
    int len = snprintf(buf, sizeof(buf),
                       "{\"total_in\":%lu,\"total_out\":%lu}",
                       (unsigned long)total_in, (unsigned long)total_out);
    if (len <= 0 || (size_t)len >= sizeof(buf)) return AICAM_ERROR_INVALID_DATA;
    void *fd = flash_lfs_fopen(path, "w");
    if (!fd) return AICAM_ERROR_IO;
    (void)flash_lfs_fwrite(fd, buf, (size_t)len);
    flash_lfs_fclose(fd);
    return AICAM_OK;
}

static aicam_result_t lc_shell_load_totals(void *user, uint32_t *total_in,
                                           uint32_t *total_out) {
    (void)user;
    *total_in = 0;
    *total_out = 0;
    if (lc_read_totals_file(LC_TOTALS_PATH, total_in, total_out)) return AICAM_OK;

    if (lc_read_totals_file(LC_LEGACY_TOTALS_PATH, total_in, total_out)) {
        if (lc_write_totals_file(LC_TOTALS_PATH, *total_in, *total_out) == AICAM_OK) {
            LOG_CORE_INFO("Line counting totals migrated from legacy pc_totals");
            return AICAM_OK;
        }
        return AICAM_ERROR_IO;
    }
    return AICAM_ERROR_NOT_FOUND;
}

static aicam_result_t lc_shell_save_totals(void *user, uint32_t total_in,
                                           uint32_t total_out) {
    (void)user;
    return lc_write_totals_file(LC_TOTALS_PATH, total_in, total_out);
}

static aicam_result_t lc_shell_get_model_info(void *user, lc_runtime_model_info_t *info) {
    (void)user;
    ai_model_runtime_info_t rt;
    aicam_result_t r = ai_get_model_runtime_info(&rt);
    if (r != AICAM_OK) return r;
    memset(info, 0, sizeof(*info));
    info->loaded = (rt.loaded == AICAM_TRUE) ? 1u : 0u;
    info->generation = rt.generation;
    info->result_type = rt.result_type;
    info->num_classes = rt.num_classes;
    return AICAM_OK;
}

static aicam_result_t lc_shell_get_class_name(void *user, uint16_t index, char *buf,
                                              size_t buf_size) {
    (void)user;
    return ai_get_model_class_name(index, buf, buf_size);
}

static uint32_t lc_shell_now_ms(void *user) {
    (void)user;
    return osKernelGetTickCount();
}

static lc_app_t g_lc_app;

static struct {
    osMutexId_t     mutex;
    osTimerId_t     timer;
    osSemaphoreId_t tick_sem;
    osThreadId_t    tick_thread;
    uint8_t         inited;
} g_lc;

static uint8_t lc_tick_task_stack[4 * 1024] ALIGN_32 IN_PSRAM;

static void lc_timer_cb(void *arg) {
    (void)arg;
    if (!g_lc.inited) return;
    osSemaphoreRelease(g_lc.tick_sem);
}

static void lc_tick_task(void *arg) {
    (void)arg;
    for (;;) {
        if (osSemaphoreAcquire(g_lc.tick_sem, osWaitForever) != osOK) continue;
        if (!g_lc.inited) continue;

        osMutexAcquire(g_lc.mutex, osWaitForever);
        lc_window_summary_t closed;
        aicam_bool_t did_close = lc_app_tick_window(&g_lc_app, osKernelGetTickCount(),
                                                    &closed);
        osMutexRelease(g_lc.mutex);

        if (did_close) {
            LOG_CORE_INFO("LC_WINDOW_CLOSED in=%lu out=%lu",
                          (unsigned long)closed.in, (unsigned long)closed.out);
            osMutexAcquire(g_lc.mutex, osWaitForever);
            line_counting_stats_t stats;
            lc_app_get_stats(&g_lc_app, &stats);
            osMutexRelease(g_lc.mutex);
            lc_shell_save_totals(NULL, stats.total_in, stats.total_out);
        }
    }
}

static void lc_subscriber_thunk(const nn_result_t *result, uint32_t timestamp_ms) {
    line_counting_on_ai_result(result, timestamp_ms);
}

aicam_result_t line_counting_init(void) {
    if (g_lc.inited) return AICAM_OK;
    memset(&g_lc, 0, sizeof(g_lc));

    g_lc.mutex = osMutexNew(NULL);
    if (!g_lc.mutex) return AICAM_ERROR_NO_MEMORY;

    line_counting_config_t cfg;
    if (json_config_get_line_counting_config(&cfg) != AICAM_OK) {
        line_counting_config_defaults(&cfg);
    }

    lc_app_ops_t ops;
    memset(&ops, 0, sizeof(ops));
    ops.get_model_info = lc_shell_get_model_info;
    ops.get_class_name = lc_shell_get_class_name;
    ops.now_ms = lc_shell_now_ms;
    ops.load_totals = lc_shell_load_totals;
    ops.save_totals = lc_shell_save_totals;

    lc_app_init(&g_lc_app, &ops, &cfg);

    if (ai_service_register_subscriber(lc_subscriber_thunk) != AICAM_OK) {
        return AICAM_ERROR;
    }

    g_lc.tick_sem = osSemaphoreNew(4, 0, NULL);
    if (!g_lc.tick_sem) return AICAM_ERROR_NO_MEMORY;
    osThreadAttr_t tick_attr = {
        .name = "lc_tick",
        .stack_mem = lc_tick_task_stack,
        .stack_size = sizeof(lc_tick_task_stack),
        .priority = osPriorityBelowNormal,
    };
    g_lc.tick_thread = osThreadNew(lc_tick_task, NULL, &tick_attr);
    if (!g_lc.tick_thread) return AICAM_ERROR_NO_MEMORY;

    osTimerAttr_t timer_attr = { .name = "lc_win" };
    g_lc.timer = osTimerNew(lc_timer_cb, osTimerPeriodic, NULL, &timer_attr);
    if (!g_lc.timer) return AICAM_ERROR_NO_MEMORY;
    osTimerStart(g_lc.timer, 1000u);

    g_lc.inited = 1;

    line_counting_stats_t stats;
    lc_app_get_stats(&g_lc_app, &stats);
    LOG_CORE_INFO("LC_CONFIG_LOADED state=%d window_minutes=%u total_in=%lu total_out=%lu",
                  (int)g_lc_app.state, (unsigned)g_lc_app.cfg.window_minutes,
                  (unsigned long)stats.total_in, (unsigned long)stats.total_out);
    return AICAM_OK;
}

void line_counting_on_ai_result(const nn_result_t *result, uint32_t timestamp_ms) {
    if (!g_lc.inited || !result) return;
    if (result->type != PP_TYPE_OD) return;

    lc_det_t dets[LC_FRAME_MAX_DETECTIONS];
    uint8_t n = result->od.nb_detect < LC_FRAME_MAX_DETECTIONS
                    ? result->od.nb_detect : LC_FRAME_MAX_DETECTIONS;
    for (uint8_t i = 0; i < n; i++) {
        const od_detect_t *d = &result->od.detects[i];
        dets[i].x = d->x;
        dets[i].y = d->y;
        dets[i].w = d->width;
        dets[i].h = d->height;
        dets[i].conf = d->conf;
        dets[i].class_name = d->class_name;
    }
    lc_frame_input_t frame;
    frame.result_type = (int32_t)result->type;
    frame.nb_detect = n;
    frame.detects = dets;

    if (osMutexAcquire(g_lc.mutex, 0) != osOK) return;
    (void)lc_app_on_ai_result(&g_lc_app, &frame, timestamp_ms);
    osMutexRelease(g_lc.mutex);
}

aicam_bool_t line_counting_is_enabled(void) {
    if (!g_lc.inited) return AICAM_FALSE;
    return json_config_get_config_ro()->line_counting.enable ? AICAM_TRUE : AICAM_FALSE;
}

aicam_result_t line_counting_get_status(line_counting_status_t *out) {
    if (!out) return AICAM_ERROR_INVALID_PARAM;
    if (!g_lc.inited) return AICAM_ERROR_NOT_INITIALIZED;
    osMutexAcquire(g_lc.mutex, osWaitForever);
    lc_app_get_status(&g_lc_app, out);
    osMutexRelease(g_lc.mutex);
    return AICAM_OK;
}

aicam_result_t line_counting_get_stats(line_counting_stats_t *out) {
    if (!out) return AICAM_ERROR_INVALID_PARAM;
    if (!g_lc.inited) return AICAM_ERROR_NOT_INITIALIZED;
    osMutexAcquire(g_lc.mutex, osWaitForever);
    lc_app_get_stats(&g_lc_app, out);
    osMutexRelease(g_lc.mutex);
    return AICAM_OK;
}

aicam_result_t line_counting_get_events(line_count_event_t *out, uint16_t max_events,
                                        uint16_t *out_n) {
    if (!out || max_events == 0 || !out_n) return AICAM_ERROR_INVALID_PARAM;
    if (!g_lc.inited) return AICAM_ERROR_NOT_INITIALIZED;
    osMutexAcquire(g_lc.mutex, osWaitForever);
    *out_n = lc_app_get_events(&g_lc_app, out, max_events);
    osMutexRelease(g_lc.mutex);
    return AICAM_OK;
}

aicam_result_t line_counting_apply_config(const line_counting_config_t *candidate) {
    if (!candidate) return AICAM_ERROR_INVALID_PARAM;
    if (!g_lc.inited) return AICAM_ERROR_NOT_INITIALIZED;
    if (!line_counting_config_is_valid(candidate)) return AICAM_ERROR_INVALID_DATA;

    aicam_result_t r = json_config_set_line_counting_config(candidate);
    if (r != AICAM_OK) return r;

    osMutexAcquire(g_lc.mutex, osWaitForever);
    lc_window_summary_t closed;
    r = lc_app_apply_config(&g_lc_app, candidate, &closed);
    osMutexRelease(g_lc.mutex);
    return r;
}

aicam_result_t line_counting_reset(void) {
    if (!g_lc.inited) return AICAM_ERROR_NOT_INITIALIZED;
    osMutexAcquire(g_lc.mutex, osWaitForever);
    aicam_result_t r = lc_app_reset(&g_lc_app, osKernelGetTickCount());
    osMutexRelease(g_lc.mutex);
    return r;
}

#endif
