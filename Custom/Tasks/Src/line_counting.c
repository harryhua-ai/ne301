#include "line_counting.h"
#include "lc_delivery_queue.h"
#include <string.h>
#include <stdio.h>
#include "cJSON.h"
#include <time.h>

static void lc_add_iso_time(cJSON *obj, const char *key, const char *iso, uint8_t valid) {
    if (valid && iso[0]) {
        cJSON_AddStringToObject(obj, key, iso);
    } else {
        cJSON_AddNullToObject(obj, key);
    }
}

static void lc_add_tracks_array(cJSON *root, const lc_report_snapshot_t *snap) {
    cJSON *tracks = cJSON_CreateArray();
    uint16_t n = snap->n_tracks < LC_REPORT_MAX_TRACKS ? snap->n_tracks : LC_REPORT_MAX_TRACKS;
    for (uint16_t i = 0; i < n && snap->tracks; i++) {
        const lc_track_record_t *r = snap->tracks[i];
        cJSON *trk = cJSON_CreateObject();
        cJSON_AddNumberToObject(trk, "track_id", (double)r->track_id);
        cJSON_AddNumberToObject(trk, "segment_id", (double)r->segment_id);
        cJSON_AddNumberToObject(trk, "entered_at_ms", (double)r->entered_at_ms);
        cJSON_AddNumberToObject(trk, "seg_start_ms", (double)r->seg_start_ms);
        cJSON_AddNumberToObject(trk, "seg_end_ms", (double)r->seg_end_ms);
        cJSON_AddStringToObject(trk, "seg_end_type",
                                r->seg_end_type == LC_SEG_CROSSING ? "crossing" : "departed");
        cJSON *evs = cJSON_CreateArray();
        if (r->events & LC_BIT_IN) cJSON_AddItemToArray(evs, cJSON_CreateString("line_cross_in"));
        if (r->events & LC_BIT_OUT) cJSON_AddItemToArray(evs, cJSON_CreateString("line_cross_out"));
        cJSON_AddItemToObject(trk, "events", evs);
        cJSON *pts = cJSON_CreateArray();
        const uint32_t *pts_ts = lc_track_record_point_ts_const(r);
        for (uint8_t p = 0; p < r->nb_points; p++) {
            cJSON *pt = cJSON_CreateArray();
            cJSON_AddItemToArray(pt, cJSON_CreateNumber((double)r->points[p].x));
            cJSON_AddItemToArray(pt, cJSON_CreateNumber((double)r->points[p].y));
            cJSON_AddItemToArray(pt, cJSON_CreateNumber((double)pts_ts[p]));
            cJSON_AddItemToArray(pts, pt);
        }
        cJSON_AddItemToObject(trk, "points", pts);
        cJSON_AddItemToArray(tracks, trk);
    }
    cJSON_AddItemToObject(root, "tracks", tracks);
}

size_t lc_report_build_v1(const lc_report_snapshot_t *snap, char *out, size_t cap) {
    if (!snap || !out || cap == 0) return 0;
    out[0] = '\0';

    cJSON *root = cJSON_CreateObject();
    if (!root) return 0;
    cJSON_AddNumberToObject(root, "schema_version", 1);
    cJSON_AddStringToObject(root, "type", "line_counting");
    cJSON_AddStringToObject(root, "device_id", snap->device_id);
    cJSON_AddNumberToObject(root, "boot_id", (double)snap->boot_id);
    cJSON_AddNumberToObject(root, "report_seq", (double)snap->report_seq);
    cJSON_AddBoolToObject(root, "clock_valid", snap->clock_valid ? 1 : 0);
    lc_add_iso_time(root, "reported_at", snap->reported_at, snap->clock_valid);

    cJSON *win = cJSON_CreateObject();
    lc_add_iso_time(win, "start_time", snap->window_start_time, snap->clock_valid);
    lc_add_iso_time(win, "end_time", snap->window_end_time, snap->clock_valid);
    cJSON_AddNumberToObject(win, "duration_sec", (double)snap->window_duration_sec);
    cJSON_AddNumberToObject(win, "in", (double)snap->window_in);
    cJSON_AddNumberToObject(win, "out", (double)snap->window_out);
    cJSON_AddItemToObject(root, "window", win);

    cJSON *tot = cJSON_CreateObject();
    cJSON_AddNumberToObject(tot, "in", (double)snap->total_in);
    cJSON_AddNumberToObject(tot, "out", (double)snap->total_out);
    cJSON_AddItemToObject(root, "total", tot);

    cJSON *counter = cJSON_CreateObject();
    cJSON_AddStringToObject(counter, "counter_name", snap->counter_name);
    cJSON_AddItemToObject(root, "counter", counter);

    cJSON *target = cJSON_CreateObject();
    cJSON_AddStringToObject(target, "class_name", snap->target_class_name);
    cJSON_AddItemToObject(root, "target", target);

    cJSON *model = cJSON_CreateObject();
    cJSON_AddStringToObject(model, "name", snap->model_name);
    cJSON_AddStringToObject(model, "version", snap->model_version);
    cJSON_AddItemToObject(root, "model", model);

    cJSON *line = cJSON_CreateObject();
    cJSON_AddNumberToObject(line, "x1", (double)snap->line_x1);
    cJSON_AddNumberToObject(line, "y1", (double)snap->line_y1);
    cJSON_AddNumberToObject(line, "x2", (double)snap->line_x2);
    cJSON_AddNumberToObject(line, "y2", (double)snap->line_y2);
    cJSON_AddNumberToObject(line, "outside_x", (double)snap->outside_x);
    cJSON_AddNumberToObject(line, "outside_y", (double)snap->outside_y);
    cJSON_AddItemToObject(root, "line", line);

    cJSON *config = cJSON_CreateObject();
    cJSON_AddNumberToObject(config, "confidence_threshold", (double)snap->confidence_threshold);
    cJSON_AddItemToObject(root, "config", config);

    if (snap->tracks_report_enable) {
        lc_add_tracks_array(root, snap);
    }
    if (snap->heat_grid_enable && snap->heat) {
        cJSON *hg = cJSON_CreateObject();
        cJSON_AddNumberToObject(hg, "width", LC_HEAT_GRID_DIM);
        cJSON_AddNumberToObject(hg, "height", LC_HEAT_GRID_DIM);
        cJSON *data = cJSON_CreateArray();
        for (int i = 0; i < (int)LC_HEAT_GRID_SIZE; i++) {
            cJSON_AddItemToArray(data, cJSON_CreateNumber((double)snap->heat[i]));
        }
        cJSON_AddItemToObject(hg, "data", data);
        cJSON_AddItemToObject(root, "heat_grid", hg);
    }

    char *printed = cJSON_PrintUnformatted(root);
    size_t len = 0;
    if (printed) {
        len = strlen(printed);
        if (len + 1 > cap) len = 0;
        else {
            memcpy(out, printed, len + 1);
        }
        cJSON_free(printed);
    }
    cJSON_Delete(root);
    return len;
}

#ifndef __LC_TEST__
#include "ai_service.h"
#include "json_config_mgr.h"
#include "storage.h"
#include "debug.h"
#include "cmsis_os2.h"
#include "common_utils.h"
#include "drtc.h"
#include "device_service.h"
#include "mqtt_service.h"
#include "webhook_service.h"
#endif

static void lc_clear_transient(lc_app_t *app) {
    if (app->tracker) {
        app->next_id_carry = lc_tracker_next_id(app->tracker);
        lc_tracker_destroy(app->tracker);
        app->tracker = NULL;
    }
    app->events_head = 0;
    app->events_count = 0;
    memset(app->heat, 0, sizeof(app->heat));
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

static void lc_heat_cb(const lc_track_t *trk, void *user) {
    uint32_t *heat = (uint32_t *)user;
    lc_point_t p = trk->last_pos;
    int gx = (int)(p.x * (float)LC_HEAT_GRID_DIM);
    int gy = (int)(p.y * (float)LC_HEAT_GRID_DIM);
    if (gx < 0) gx = 0;
    if (gy < 0) gy = 0;
    if (gx > (int)LC_HEAT_GRID_DIM - 1) gx = (int)LC_HEAT_GRID_DIM - 1;
    if (gy > (int)LC_HEAT_GRID_DIM - 1) gy = (int)LC_HEAT_GRID_DIM - 1;
    heat[gy * LC_HEAT_GRID_DIM + gx]++;
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
    app->totals_persist_epoch = 0;
    if (app->ops.load_totals &&
        app->ops.load_totals(app->ops.user, &total_in, &total_out,
                             &app->totals_persist_epoch) == AICAM_OK) {
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
    if (app->transaction_pending) return AICAM_OK;
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
    if (win_in + win_out > 0) {
        lc_app_mark_totals_dirty(app);
    }
    if (app->cfg.heat_grid_enable) {
        lc_tracker_for_each_stable(app->tracker, lc_heat_cb, app->heat);
    }
    return AICAM_OK;
}

static void lc_app_commit_config(lc_app_t *app, const line_counting_config_t *candidate,
                                 uint32_t now, uint8_t target_changed,
                                 uint8_t disable_close, uint8_t reenable,
                                 lc_window_summary_t *closed_out) {
    if (target_changed) {
        app->total_in = 0;
        app->total_out = 0;
        app->window_in = 0;
        app->window_out = 0;
        app->window_start_ms = now;
        lc_clear_transient(app);
        app->binding_valid = 0;
    }

    if (reenable) {
        app->window_start_ms = now;
    }

    if (disable_close) {
        if (closed_out) {
            closed_out->start_ms = app->window_start_ms;
            closed_out->end_ms = now;
            closed_out->in = app->window_in;
            closed_out->out = app->window_out;
        }
        app->window_in = 0;
        app->window_out = 0;
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
            app->next_id_carry = lc_tracker_next_id(app->tracker);
            lc_tracker_destroy(app->tracker);
            app->tracker = NULL;
        }
    }

    app->cfg = *candidate;

    if (target_changed) {
        app->totals_dirty = 0;
    }

    if (!app->cfg.enable) {
        app->state = LC_STATE_DISABLED;
        app->reason = LC_REASON_NONE;
    } else {
        lc_rebind(app);
    }
}

aicam_result_t lc_app_apply_config(lc_app_t *app, const line_counting_config_t *candidate,
                                   lc_window_summary_t *closed_out) {
    if (!app || !candidate) return AICAM_ERROR_INVALID_PARAM;
    if (!line_counting_config_is_valid(candidate)) return AICAM_ERROR_INVALID_DATA;
    if (app->transaction_pending) return AICAM_ERROR_TRANSACTION;
    if (!app->ops.persist_config || !app->ops.queue_clear ||
        !app->ops.txn_prepare || !app->ops.txn_get || !app->ops.txn_clear) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    uint32_t now = app->ops.now_ms ? app->ops.now_ms(app->ops.user) : 0;
    uint8_t target_changed = strcmp(candidate->target_class_name,
                                    app->cfg.target_class_name) != 0;
    uint8_t disable_close = (!candidate->enable && app->cfg.enable) ? 1u : 0u;
    uint8_t reenable = (candidate->enable && !app->cfg.enable) ? 1u : 0u;
    uint32_t old_total_in = app->total_in;
    uint32_t old_total_out = app->total_out;
    uint32_t old_epoch = app->totals_persist_epoch;
    line_counting_config_t old_cfg = app->cfg;
    aicam_result_t r;

    if (target_changed) {
        r = app->ops.txn_prepare(app->ops.user, LC_TXN_OP_TARGET_CHANGE, candidate);
        if (r != AICAM_OK) return r;
        app->transaction_pending = 1;
    }

    r = app->ops.persist_config(app->ops.user, candidate);
    if (r != AICAM_OK) {
        if (target_changed) {
            if (app->ops.persist_config(app->ops.user, &old_cfg) == AICAM_OK &&
                app->ops.txn_clear(app->ops.user) == AICAM_OK) {
                app->transaction_pending = 0;
                return r;
            }
            return AICAM_ERROR_TRANSACTION;
        }
        return r;
    }

    if (target_changed) {
        app->totals_resetting = 1;
        app->totals_persist_epoch = old_epoch + 1u;
        r = app->ops.save_totals(app->ops.user, 0, 0,
                                  app->totals_persist_epoch);
        if (r != AICAM_OK) {
            aicam_result_t rt = app->ops.save_totals(app->ops.user, old_total_in,
                                                     old_total_out, old_epoch);
            if (rt == AICAM_OK &&
                app->ops.persist_config(app->ops.user, &old_cfg) == AICAM_OK &&
                app->ops.txn_clear(app->ops.user) == AICAM_OK) {
                app->totals_persist_epoch = old_epoch;
                app->totals_resetting = 0;
                app->transaction_pending = 0;
                return r;
            }
            return AICAM_ERROR_TRANSACTION;
        }

        r = app->ops.queue_clear(app->ops.user);
        if (r != AICAM_OK) {
            aicam_result_t rt = app->ops.save_totals(app->ops.user, old_total_in,
                                                     old_total_out, old_epoch);
            aicam_result_t rc = AICAM_ERROR_IO;
            if (rt == AICAM_OK) rc = app->ops.persist_config(app->ops.user, &old_cfg);
            if (rt == AICAM_OK && rc == AICAM_OK &&
                app->ops.txn_clear(app->ops.user) == AICAM_OK) {
                app->totals_persist_epoch = old_epoch;
                app->totals_resetting = 0;
                app->transaction_pending = 0;
                return r;
            }
            return AICAM_ERROR_TRANSACTION;
        }
    }

    lc_app_commit_config(app, candidate, now, target_changed, disable_close, reenable,
                         closed_out);
    if (target_changed) {
        app->totals_resetting = 0;
        if (app->ops.txn_clear(app->ops.user) != AICAM_OK) {
            return AICAM_OK;
        }
        app->transaction_pending = 0;
    }
    return AICAM_OK;
}

static void lc_app_commit_reset_ram(lc_app_t *app, uint32_t now_ms) {
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
        app->reason = LC_REASON_NONE;
    }
}

aicam_result_t lc_app_recover_transaction(lc_app_t *app) {
    if (!app) return AICAM_ERROR_INVALID_PARAM;
    if (!app->ops.txn_prepare || !app->ops.txn_get || !app->ops.txn_clear ||
        !app->ops.persist_config || !app->ops.save_totals || !app->ops.queue_clear) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    uint32_t op = 0;
    line_counting_config_t candidate;
    aicam_result_t r = app->ops.txn_get(app->ops.user, &op, &candidate);
    if (r == AICAM_ERROR_NOT_FOUND) {
        app->transaction_pending = 0;
        return AICAM_OK;
    }
    if (r != AICAM_OK) return r;
    app->transaction_pending = 1;
    uint32_t now = app->ops.now_ms ? app->ops.now_ms(app->ops.user) : 0;

    if (op == LC_TXN_OP_MANUAL_RESET) {
        app->totals_resetting = 1;
        app->totals_persist_epoch++;
        r = app->ops.save_totals(app->ops.user, 0, 0,
                                  app->totals_persist_epoch);
        if (r != AICAM_OK) return r;
        r = app->ops.queue_clear(app->ops.user);
        if (r != AICAM_OK) return r;
        lc_app_commit_reset_ram(app, now);
        app->totals_resetting = 0;
        if (app->ops.txn_clear(app->ops.user) != AICAM_OK) return AICAM_ERROR_TRANSACTION;
        app->transaction_pending = 0;
        return AICAM_OK;
    }

    if (op != LC_TXN_OP_TARGET_CHANGE) {
        (void)app->ops.txn_clear(app->ops.user);
        app->transaction_pending = 0;
        return AICAM_ERROR_INVALID_DATA;
    }
    if (!line_counting_config_is_valid(&candidate)) {
        (void)app->ops.txn_clear(app->ops.user);
        app->transaction_pending = 0;
        return AICAM_ERROR_INVALID_DATA;
    }
    r = app->ops.persist_config(app->ops.user, &candidate);
    if (r != AICAM_OK) return r;
    app->totals_resetting = 1;
    app->totals_persist_epoch++;
    r = app->ops.save_totals(app->ops.user, 0, 0, app->totals_persist_epoch);
    if (r != AICAM_OK) return r;
    r = app->ops.queue_clear(app->ops.user);
    if (r != AICAM_OK) return r;
    lc_app_commit_config(app, &candidate, now, 1, 0, 0, NULL);
    app->totals_resetting = 0;
    if (app->ops.txn_clear(app->ops.user) != AICAM_OK) return AICAM_ERROR_TRANSACTION;
    app->transaction_pending = 0;
    return AICAM_OK;
}

aicam_bool_t lc_app_tick_window(lc_app_t *app, uint32_t now_ms,
                                lc_window_close_t *closed_out,
                                lc_track_record_t ***out_records,
                                uint16_t *out_n_records) {
    if (!app || !app->cfg.enable) return AICAM_FALSE;
    if (app->transaction_pending) return AICAM_FALSE;
    if (out_records) *out_records = NULL;
    if (out_n_records) *out_n_records = 0;
    uint32_t period_ms = (uint32_t)app->cfg.window_minutes * 60u * 1000u;
    if (period_ms == 0) return AICAM_FALSE;
    if ((uint32_t)(now_ms - app->window_start_ms) < period_ms) return AICAM_FALSE;

    if (closed_out) {
        closed_out->summary.start_ms = app->window_start_ms;
        closed_out->summary.end_ms = now_ms;
        closed_out->summary.in = app->window_in;
        closed_out->summary.out = app->window_out;
        memcpy(closed_out->heat, app->heat, sizeof(app->heat));
        closed_out->heat_valid = app->cfg.heat_grid_enable;
    }
    memset(app->heat, 0, sizeof(app->heat));

    if (out_records && app->tracker && app->cfg.tracks_report_enable) {
        lc_tracker_window_snapshot(app->tracker, now_ms, out_records, out_n_records);
    }

    app->window_in = 0;
    app->window_out = 0;
    app->window_start_ms = now_ms;
    return AICAM_TRUE;
}

aicam_result_t lc_app_reset(lc_app_t *app, uint32_t now_ms) {
    if (!app) return AICAM_ERROR_INVALID_PARAM;
    if (app->transaction_pending) return AICAM_ERROR_TRANSACTION;
    if (!app->ops.txn_prepare || !app->ops.txn_get || !app->ops.txn_clear ||
        !app->ops.save_totals || !app->ops.queue_clear) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    aicam_result_t r = app->ops.txn_prepare(app->ops.user, LC_TXN_OP_MANUAL_RESET, NULL);
    if (r != AICAM_OK) return r;
    app->transaction_pending = 1;

    app->totals_resetting = 1;
    app->totals_persist_epoch++;
    r = app->ops.save_totals(app->ops.user, 0, 0, app->totals_persist_epoch);
    if (r != AICAM_OK) return AICAM_ERROR_TRANSACTION;
    r = app->ops.queue_clear(app->ops.user);
    if (r != AICAM_OK) return AICAM_ERROR_TRANSACTION;

    lc_app_commit_reset_ram(app, now_ms);
    app->totals_resetting = 0;
    if (app->ops.txn_clear(app->ops.user) != AICAM_OK) return AICAM_OK;
    app->transaction_pending = 0;
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

aicam_bool_t lc_app_take_totals_checkpoint(lc_app_t *app, uint32_t *total_in,
                                           uint32_t *total_out,
                                           uint32_t *out_generation,
                                           uint32_t *out_persist_epoch) {
    if (!app || !total_in || !total_out) return AICAM_FALSE;
    if (app->totals_resetting) return AICAM_FALSE;
    if (!app->totals_dirty) return AICAM_FALSE;
    *total_in = app->total_in;
    *total_out = app->total_out;
    if (out_generation) *out_generation = app->totals_generation;
    if (out_persist_epoch) *out_persist_epoch = app->totals_persist_epoch;
    return AICAM_TRUE;
}

aicam_result_t lc_app_acknowledge_checkpoint(lc_app_t *app, uint32_t generation, uint32_t persist_epoch) {
    if (!app) return AICAM_ERROR_INVALID_PARAM;
    if (!app->totals_dirty) return AICAM_OK;
    if (generation == app->totals_generation && persist_epoch == app->totals_persist_epoch) {
        app->totals_dirty = 0;
    }
    return AICAM_OK;
}

void lc_app_mark_totals_dirty(lc_app_t *app) {
    if (app) {
        app->totals_dirty = 1;
        app->totals_generation++;
    }
}

void lc_app_advance_persist_epoch(lc_app_t *app) {
    if (app) {
        app->totals_persist_epoch++;
        app->totals_dirty = 0;
    }
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

#define LC_TOTALS_REC_MAGIC  0x4C545333u
#define LC_TOTALS_REC_SIZE   24u
#define LC_TOTALS_SLOT_COUNT 2u

typedef struct {
    uint32_t magic;
    uint32_t generation;
    uint32_t total_in;
    uint32_t total_out;
    uint32_t epoch;
    uint32_t crc;
} lc_totals_rec_t;

static uint32_t lc_crc32(const void *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

static aicam_bool_t lc_totals_rec_valid(const lc_totals_rec_t *r) {
    if (r->magic != LC_TOTALS_REC_MAGIC || r->generation == 0) return AICAM_FALSE;
    return (aicam_bool_t)(r->crc == lc_crc32(r, offsetof(lc_totals_rec_t, crc)));
}

aicam_result_t lc_totals_store_init(lc_totals_store_t *s, const lc_totals_io_t *io) {
    if (!s || !io || !io->read || !io->write) return AICAM_ERROR_INVALID_PARAM;
    memset(s, 0, sizeof(*s));
    s->io = *io;
    for (uint32_t slot = 0; slot < LC_TOTALS_SLOT_COUNT; slot++) {
        lc_totals_rec_t r;
        if (s->io.read(s->io.user, slot * LC_TOTALS_REC_SIZE, &r, sizeof(r)) != AICAM_OK) {
            continue;
        }
        if (!lc_totals_rec_valid(&r)) continue;
        if (!s->has_record ||
            lc_serial_newer(r.epoch, s->epoch) ||
            (r.epoch == s->epoch && lc_serial_newer(r.generation, s->generation))) {
            s->generation = r.generation;
            s->epoch = r.epoch;
            s->has_record = 1;
        }
    }
    return AICAM_OK;
}

aicam_result_t lc_totals_store_load(const lc_totals_store_t *s, uint32_t *total_in,
                                    uint32_t *total_out, uint32_t *epoch_out) {
    if (!s || !total_in || !total_out) return AICAM_ERROR_INVALID_PARAM;
    if (!s->has_record) return AICAM_ERROR_NOT_FOUND;
    uint32_t slot = s->generation & 1u;
    lc_totals_rec_t r;
    if (s->io.read(s->io.user, slot * LC_TOTALS_REC_SIZE, &r, sizeof(r)) != AICAM_OK) {
        return AICAM_ERROR_IO;
    }
    if (!lc_totals_rec_valid(&r) || r.generation != s->generation) return AICAM_ERROR_IO;
    *total_in = r.total_in;
    *total_out = r.total_out;
    if (epoch_out) *epoch_out = r.epoch;
    return AICAM_OK;
}

aicam_result_t lc_totals_store_save(lc_totals_store_t *s, uint32_t total_in,
                                    uint32_t total_out, uint32_t epoch) {
    if (!s) return AICAM_ERROR_INVALID_PARAM;
    uint32_t generation = lc_serial_next(s->generation);
    lc_totals_rec_t r;
    r.magic = LC_TOTALS_REC_MAGIC;
    r.generation = generation;
    r.total_in = total_in;
    r.total_out = total_out;
    r.epoch = epoch;
    r.crc = lc_crc32(&r, offsetof(lc_totals_rec_t, crc));
    uint32_t slot = generation & 1u;
    aicam_result_t res = s->io.write(s->io.user, slot * LC_TOTALS_REC_SIZE, &r, sizeof(r));
    if (res != AICAM_OK) return res;
    s->generation = generation;
    s->epoch = epoch;
    s->has_record = 1;
    return AICAM_OK;
}

#ifndef __LC_TEST__

#define LC_TOTALS_STORE_PATH "/config/line_counting_totals.bin"
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

static aicam_result_t lc_totals_file_read(void *user, uint32_t offset, void *buf,
                                          uint32_t len) {
    (void)user;
    void *fd = flash_lfs_fopen(LC_TOTALS_STORE_PATH, "r");
    if (!fd) return AICAM_ERROR_IO;
    if (flash_lfs_fseek(fd, (long)offset, 0) != 0) {
        flash_lfs_fclose(fd);
        return AICAM_ERROR_IO;
    }
    int n = flash_lfs_fread(fd, buf, len);
    flash_lfs_fclose(fd);
    return (n == (int)len) ? AICAM_OK : AICAM_ERROR_IO;
}

static aicam_result_t lc_totals_file_write(void *user, uint32_t offset, const void *buf,
                                           uint32_t len) {
    (void)user;
    void *fd = flash_lfs_fopen(LC_TOTALS_STORE_PATH, "r+");
    if (!fd) fd = flash_lfs_fopen(LC_TOTALS_STORE_PATH, "w");
    if (!fd) return AICAM_ERROR_IO;
    if (flash_lfs_fseek(fd, (long)offset, 0) != 0) {
        flash_lfs_fclose(fd);
        return AICAM_ERROR_IO;
    }
    if (flash_lfs_fwrite(fd, buf, len) != (int)len) {
        flash_lfs_fclose(fd);
        return AICAM_ERROR_IO;
    }
    if (flash_lfs_fflush(fd) != 0) {
        flash_lfs_fclose(fd);
        return AICAM_ERROR_IO;
    }
    if (flash_lfs_fclose(fd) != 0) return AICAM_ERROR_IO;
    return AICAM_OK;
}

static lc_totals_store_t g_lc_totals_store;
static uint32_t g_lc_totals_epoch;

static aicam_result_t lc_shell_load_totals(void *user, uint32_t *total_in,
                                           uint32_t *total_out, uint32_t *epoch) {
    (void)user;
    *total_in = 0;
    *total_out = 0;
    lc_totals_io_t io;
    io.user = NULL;
    io.read = lc_totals_file_read;
    io.write = lc_totals_file_write;
    if (lc_totals_store_init(&g_lc_totals_store, &io) != AICAM_OK) return AICAM_ERROR_IO;
    if (lc_totals_store_load(&g_lc_totals_store, total_in, total_out,
                             &g_lc_totals_epoch) == AICAM_OK) {
        if (epoch) *epoch = g_lc_totals_epoch;
        return AICAM_OK;
    }

    uint32_t legacy_in = 0;
    uint32_t legacy_out = 0;
    if (lc_read_totals_file(LC_TOTALS_PATH, &legacy_in, &legacy_out) ||
        lc_read_totals_file(LC_LEGACY_TOTALS_PATH, &legacy_in, &legacy_out)) {
        if (lc_totals_store_save(&g_lc_totals_store, legacy_in, legacy_out,
                                 g_lc_totals_epoch) != AICAM_OK) {
            return AICAM_ERROR_IO;
        }
        LOG_CORE_INFO("Line counting totals migrated from legacy json");
        if (epoch) *epoch = g_lc_totals_epoch;
        *total_in = legacy_in;
        *total_out = legacy_out;
        return AICAM_OK;
    }
    return AICAM_ERROR_NOT_FOUND;
}

static osMutexId_t g_lc_totals_io_mutex;

static aicam_result_t lc_shell_save_totals(void *user, uint32_t total_in,
                                           uint32_t total_out, uint32_t epoch) {
    (void)user;
    if (g_lc_totals_io_mutex &&
        osMutexAcquire(g_lc_totals_io_mutex, osWaitForever) != osOK) {
        return AICAM_ERROR_BUSY;
    }
    g_lc_totals_epoch = epoch;
    aicam_result_t r = lc_totals_store_save(&g_lc_totals_store, total_in, total_out,
                                            g_lc_totals_epoch);
    if (g_lc_totals_io_mutex) osMutexRelease(g_lc_totals_io_mutex);
    return r;
}

static aicam_result_t lc_shell_persist_config(void *user, const line_counting_config_t *candidate) {
    (void)user;
    return json_config_set_line_counting_config(candidate);
}

#define LC_TXN_PATH     "/config/lc_target_txn.bin"
#define LC_TXN_MAGIC    0x4C545854u
#define LC_TXN_VERSION  2u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t op;
    line_counting_config_t cfg;
    uint32_t crc;
} lc_txn_rec_t;

static aicam_result_t lc_shell_txn_prepare(void *user, uint32_t op,
                                           const line_counting_config_t *candidate) {
    (void)user;
    if (op != LC_TXN_OP_TARGET_CHANGE && op != LC_TXN_OP_MANUAL_RESET) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    lc_txn_rec_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.magic = LC_TXN_MAGIC;
    rec.version = LC_TXN_VERSION;
    rec.op = op;
    if (candidate) rec.cfg = *candidate;
    rec.crc = lc_crc32(&rec, offsetof(lc_txn_rec_t, crc));
    void *fd = flash_lfs_fopen(LC_TXN_PATH, "w");
    if (!fd) return AICAM_ERROR_IO;
    if (flash_lfs_fwrite(fd, &rec, sizeof(rec)) != (int)sizeof(rec)) {
        flash_lfs_fclose(fd);
        return AICAM_ERROR_IO;
    }
    if (flash_lfs_fflush(fd) != 0) {
        flash_lfs_fclose(fd);
        return AICAM_ERROR_IO;
    }
    if (flash_lfs_fclose(fd) != 0) return AICAM_ERROR_IO;
    return AICAM_OK;
}

static aicam_result_t lc_shell_txn_get(void *user, uint32_t *op_out,
                                       line_counting_config_t *candidate) {
    (void)user;
    lc_txn_rec_t rec;
    void *fd = flash_lfs_fopen(LC_TXN_PATH, "r");
    if (!fd) return AICAM_ERROR_NOT_FOUND;
    int n = flash_lfs_fread(fd, &rec, sizeof(rec));
    flash_lfs_fclose(fd);
    if (n != (int)sizeof(rec)) return AICAM_ERROR_NOT_FOUND;
    if (rec.magic != LC_TXN_MAGIC || rec.version != LC_TXN_VERSION) return AICAM_ERROR_NOT_FOUND;
    if (rec.crc != lc_crc32(&rec, offsetof(lc_txn_rec_t, crc))) return AICAM_ERROR_NOT_FOUND;
    if (rec.op != LC_TXN_OP_TARGET_CHANGE && rec.op != LC_TXN_OP_MANUAL_RESET) {
        return AICAM_ERROR_NOT_FOUND;
    }
    if (rec.op == LC_TXN_OP_TARGET_CHANGE && !line_counting_config_is_valid(&rec.cfg)) {
        return AICAM_ERROR_NOT_FOUND;
    }
    *op_out = rec.op;
    *candidate = rec.cfg;
    return AICAM_OK;
}

static aicam_result_t lc_shell_txn_clear(void *user) {
    (void)user;
    if (flash_lfs_remove(LC_TXN_PATH) == 0) return AICAM_OK;
    void *fd = flash_lfs_fopen(LC_TXN_PATH, "r");
    if (!fd) return AICAM_OK;
    flash_lfs_fclose(fd);
    return AICAM_ERROR_IO;
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

static volatile uint8_t g_lc_reset_requested;
static volatile uint8_t g_lc_reset_in_progress;

static struct {
    osMutexId_t     mutex;
    osTimerId_t     timer;
    osSemaphoreId_t tick_sem;
    osThreadId_t    tick_thread;
    uint8_t         inited;
} g_lc;

static uint8_t lc_tick_task_stack[16 * 1024] ALIGN_32 IN_PSRAM;

#define LC_DELIVERY_PATH "/config/lc_delivery.bin"
#define LC_REPORT_BUF_SIZE (LC_DQ_SLOT_CAPACITY)

static lc_delivery_queue_t g_lc_dq;
static aicam_result_t lc_shell_queue_clear(void *user) {
    (void)user;
    return lc_delivery_queue_clear(&g_lc_dq);
}

static uint32_t g_lc_report_seq;
static uint32_t g_lc_boot_id;
static char lc_report_buf[LC_REPORT_BUF_SIZE];

static uint32_t g_lc_dropped_reports;

static void lc_device_id_str(char *out, size_t cap) {
    device_info_config_t info;
    if (device_service_get_info(&info) == AICAM_OK && info.mac_address[0]) {
        snprintf(out, cap, "%s", info.mac_address);
        return;
    }
    snprintf(out, cap, "ne301-unknown");
}

static aicam_result_t lc_dq_file_read(void *user, uint32_t offset, void *buf, uint32_t len) {
    (void)user;
    void *fd = flash_lfs_fopen(LC_DELIVERY_PATH, "r");
    if (!fd) return AICAM_ERROR_IO;
    if (flash_lfs_fseek(fd, (long)offset, 0) != 0) {
        flash_lfs_fclose(fd);
        return AICAM_ERROR_IO;
    }
    int n = flash_lfs_fread(fd, buf, len);
    flash_lfs_fclose(fd);
    return (n == (int)len) ? AICAM_OK : AICAM_ERROR_IO;
}

static aicam_result_t lc_dq_file_write(void *user, uint32_t offset, const void *buf,
                                       uint32_t len) {
    (void)user;
    void *fd = flash_lfs_fopen(LC_DELIVERY_PATH, "r+");
    if (!fd) fd = flash_lfs_fopen(LC_DELIVERY_PATH, "w");
    if (!fd) return AICAM_ERROR_IO;
    if (flash_lfs_fseek(fd, (long)offset, 0) != 0) {
        flash_lfs_fclose(fd);
        return AICAM_ERROR_IO;
    }
    int n = flash_lfs_fwrite(fd, buf, len);
    flash_lfs_fflush(fd);
    flash_lfs_fclose(fd);
    return (n == (int)len) ? AICAM_OK : AICAM_ERROR_IO;
}

static aicam_result_t lc_dq_ensure_file(void) {
    void *fd = flash_lfs_fopen(LC_DELIVERY_PATH, "r");
    if (fd) {
        flash_lfs_fclose(fd);
        return AICAM_OK;
    }
    fd = flash_lfs_fopen(LC_DELIVERY_PATH, "w");
    if (!fd) return AICAM_ERROR_IO;
    static uint8_t pad[512];
    memset(pad, 0xFF, sizeof(pad));
    for (uint32_t off = 0; off < LC_DQ_REGION_SIZE; off += sizeof(pad)) {
        if (flash_lfs_fwrite(fd, pad, sizeof(pad)) != (int)sizeof(pad)) {
            flash_lfs_fclose(fd);
            return AICAM_ERROR_IO;
        }
    }
    flash_lfs_fclose(fd);
    return AICAM_OK;
}

static void lc_clock_iso(uint64_t ts_sec, char *out, size_t cap, uint8_t *valid) {
    time_t t = (time_t)ts_sec;
    struct tm tm_info;
    struct tm *gm = gmtime_r(&t, &tm_info);
    if (!gm || (tm_info.tm_year + 1900) < 2020 || (tm_info.tm_year + 1900) > 2099) {
        out[0] = '\0';
        *valid = 0;
        return;
    }
    snprintf(out, cap, "%04d-%02d-%02dT%02d:%02d:%02d.000Z",
             tm_info.tm_year + 1900, tm_info.tm_mon + 1, tm_info.tm_mday,
             tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec);
    *valid = 1;
}

static size_t lc_build_window_report(const lc_window_close_t *closed,
                                     lc_track_record_t **records, uint16_t n_records,
                                     char *out, size_t cap) {
    ai_model_runtime_info_t rt;
    memset(&rt, 0, sizeof(rt));
    if (ai_get_model_runtime_info(&rt) != AICAM_OK) rt.loaded = AICAM_FALSE;

    uint64_t now_sec = rtc_get_timeStamp();
    char reported_at[40];
    char start_iso[40];
    char end_iso[40];
    uint8_t clock_valid = 0;
    lc_clock_iso(now_sec, reported_at, sizeof(reported_at), &clock_valid);
    lc_clock_iso(now_sec - (closed->summary.end_ms - closed->summary.start_ms) / 1000u,
                 start_iso, sizeof(start_iso), &clock_valid);
    lc_clock_iso(now_sec, end_iso, sizeof(end_iso), &clock_valid);

    line_counting_config_t cfg;
    if (json_config_get_line_counting_config(&cfg) != AICAM_OK) {
        line_counting_config_defaults(&cfg);
    }

    lc_report_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    lc_device_id_str(snap.device_id, sizeof(snap.device_id));
    snap.boot_id = g_lc_boot_id;
    snap.report_seq = ++g_lc_report_seq;
    snap.clock_valid = clock_valid;
    snprintf(snap.reported_at, sizeof(snap.reported_at), "%s", reported_at);
    snprintf(snap.window_start_time, sizeof(snap.window_start_time), "%s", start_iso);
    snprintf(snap.window_end_time, sizeof(snap.window_end_time), "%s", end_iso);
    snap.window_duration_sec = (closed->summary.end_ms - closed->summary.start_ms) / 1000u;
    snap.window_in = closed->summary.in;
    snap.window_out = closed->summary.out;
    line_counting_stats_t stats;
    osMutexAcquire(g_lc.mutex, osWaitForever);
    lc_app_get_stats(&g_lc_app, &stats);
    osMutexRelease(g_lc.mutex);
    snap.total_in = stats.total_in;
    snap.total_out = stats.total_out;
    snprintf(snap.counter_name, sizeof(snap.counter_name), "%s", cfg.counter_name);
    snprintf(snap.target_class_name, sizeof(snap.target_class_name), "%s",
             cfg.target_class_name);
    snprintf(snap.model_name, sizeof(snap.model_name), "%s", rt.name);
    snprintf(snap.model_version, sizeof(snap.model_version), "%s", rt.version);
    snap.line_x1 = cfg.line_x1_permille / 1000.0f;
    snap.line_y1 = cfg.line_y1_permille / 1000.0f;
    snap.line_x2 = cfg.line_x2_permille / 1000.0f;
    snap.line_y2 = cfg.line_y2_permille / 1000.0f;
    snap.outside_x = cfg.outside_x_permille / 1000.0f;
    snap.outside_y = cfg.outside_y_permille / 1000.0f;
    snap.confidence_threshold = cfg.conf_threshold_permille / 1000.0f;
    snap.tracks_report_enable = cfg.tracks_report_enable ? 1 : 0;
    snap.heat_grid_enable = cfg.heat_grid_enable ? 1 : 0;
    snap.tracks = (const lc_track_record_t* const*)records;
    snap.n_tracks = n_records;
    snap.heat = closed->heat_valid ? closed->heat : NULL;

    return lc_report_build_v1(&snap, out, cap);
}

static void lc_generate_window_report(const lc_window_close_t *closed,
                                      lc_track_record_t **records, uint16_t n_records) {
    size_t len = lc_build_window_report(closed, records, n_records,
                                        lc_report_buf, sizeof(lc_report_buf));
    if (len == 0) {
        LOG_CORE_ERROR("LC_REPORT_BUILD_FAILED");
        g_lc_dropped_reports++;
        return;
    }

    line_counting_config_t cfg;
    if (json_config_get_line_counting_config(&cfg) != AICAM_OK) {
        line_counting_config_defaults(&cfg);
    }
    lc_delivery_meta_t meta;
    memset(&meta, 0, sizeof(meta));
    meta.boot_id = g_lc_boot_id;
    meta.report_seq = g_lc_report_seq;
    meta.mqtt = cfg.mqtt_report_enable ? LC_DELIVERY_PENDING : LC_DELIVERY_NOT_REQUIRED;
    meta.webhook = cfg.webhook_report_enable ? LC_DELIVERY_PENDING : LC_DELIVERY_NOT_REQUIRED;

    aicam_result_t r = lc_delivery_queue_enqueue(&g_lc_dq, &meta, lc_report_buf, len);
    if (r != AICAM_OK) {
        LOG_CORE_ERROR("LC_REPORT_ENQUEUE_FAILED r=%d", r);
        g_lc_dropped_reports++;
    }
}

static void lc_drain_transport(uint8_t transport) {
    for (;;) {
        lc_delivery_meta_t meta;
        size_t len = 0;
        aicam_result_t r = lc_delivery_queue_peek_oldest_for(&g_lc_dq, transport, &meta,
                                                             lc_report_buf,
                                                             sizeof(lc_report_buf), &len);
        if (r == AICAM_ERROR_NOT_FOUND) return;
        if (r != AICAM_OK) return;

        if (transport == 0u) {
            if (!mqtt_service_is_connected()) return;
            mqtt_service_topic_config_t tc;
            if (mqtt_service_get_topic_config(&tc) != AICAM_OK) return;
            int rc = mqtt_service_publish_json(tc.data_report_topic, lc_report_buf, 1, 0);
            if (rc <= 0) return;
            (void)lc_delivery_queue_mark_mqtt_delivered(&g_lc_dq, meta.report_seq);
        } else {
            webhook_config_t wc;
            if (json_config_get_webhook_config(&wc) != AICAM_OK) return;
            if (!wc.enable || !wc.url[0]) return;
            if (webhook_service_push_json(wc.url, lc_report_buf, len) != AICAM_OK) return;
            (void)lc_delivery_queue_mark_webhook_delivered(&g_lc_dq, meta.report_seq);
        }
    }
}

static void lc_timer_cb(void *arg) {
    (void)arg;
    if (!g_lc.inited) return;
    osSemaphoreRelease(g_lc.tick_sem);
}

static void lc_totals_checkpoint_flush(void) {
    uint32_t tin, tout, gen, epoch;
    osMutexAcquire(g_lc.mutex, osWaitForever);
    aicam_bool_t dirty = lc_app_take_totals_checkpoint(&g_lc_app, &tin, &tout, &gen, &epoch);
    osMutexRelease(g_lc.mutex);
    if (!dirty) return;
    if (lc_shell_save_totals(NULL, tin, tout, epoch) != AICAM_OK) return;

    uint8_t stale;
    uint32_t cur_in, cur_out, cur_epoch;
    osMutexAcquire(g_lc.mutex, osWaitForever);
    stale = (gen != g_lc_app.totals_generation) ||
            (epoch != g_lc_app.totals_persist_epoch);
    cur_in = g_lc_app.total_in;
    cur_out = g_lc_app.total_out;
    cur_epoch = g_lc_app.totals_persist_epoch;
    if (!stale) {
        lc_app_acknowledge_checkpoint(&g_lc_app, gen, epoch);
    }
    osMutexRelease(g_lc.mutex);
    if (stale) {
        if (lc_shell_save_totals(NULL, cur_in, cur_out, cur_epoch) != AICAM_OK) {
            osMutexAcquire(g_lc.mutex, osWaitForever);
            lc_app_mark_totals_dirty(&g_lc_app);
            osMutexRelease(g_lc.mutex);
        }
    }
}

static void lc_tick_task(void *arg) {
    (void)arg;
    for (;;) {
        if (osSemaphoreAcquire(g_lc.tick_sem, osWaitForever) != osOK) continue;
        if (!g_lc.inited) continue;

        if (g_lc_reset_requested && !g_lc_reset_in_progress) {
            aicam_result_t rr;
            g_lc_reset_requested = 0;
            g_lc_reset_in_progress = 1;
            osMutexAcquire(g_lc.mutex, osWaitForever);
            rr = lc_app_reset(&g_lc_app, osKernelGetTickCount());
            osMutexRelease(g_lc.mutex);
            g_lc_reset_in_progress = 0;
            if (rr != AICAM_OK) {
                LOG_CORE_ERROR("LC_RESET_ASYNC_FAILED r=%d", rr);
                if (rr == AICAM_ERROR_TRANSACTION) {
                    g_lc_reset_requested = 1;
                }
            }
        }

        lc_drain_transport(0);
        lc_drain_transport(1);

        osMutexAcquire(g_lc.mutex, osWaitForever);
        lc_window_close_t closed;
        lc_track_record_t **records = NULL;
        uint16_t n_records = 0;
        aicam_bool_t did_close = lc_app_tick_window(&g_lc_app, osKernelGetTickCount(),
                                                    &closed, &records, &n_records);
        osMutexRelease(g_lc.mutex);

        if (did_close) {
            LOG_CORE_INFO("LC_WINDOW_CLOSED in=%lu out=%lu",
                          (unsigned long)closed.summary.in,
                          (unsigned long)closed.summary.out);
            lc_generate_window_report(&closed, records, n_records);
        }
        lc_totals_checkpoint_flush();
        if (records) {
            for (uint16_t k = 0; k < n_records; k++) LC_FREE(records[k]);
            LC_FREE(records);
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

    g_lc_totals_io_mutex = osMutexNew(NULL);
    if (!g_lc_totals_io_mutex) return AICAM_ERROR_NO_MEMORY;

    line_counting_config_t cfg;
    if (json_config_get_line_counting_config(&cfg) != AICAM_OK) {
        line_counting_config_defaults(&cfg);
    }

    if (lc_dq_ensure_file() == AICAM_OK) {
        lc_dq_storage_t dq_ops;
        dq_ops.user = NULL;
        dq_ops.read = lc_dq_file_read;
        dq_ops.write = lc_dq_file_write;
        lc_dq_limits_t lim;
        lim.max_count = cfg.backlog_capacity > LC_DQ_MAX_SLOTS ? LC_DQ_MAX_SLOTS
                                                               : cfg.backlog_capacity;
        if (lim.max_count == 0) lim.max_count = 1;
        lim.journal_entries = 128;
        lim.max_bytes = LC_DQ_MAX_SLOTS * LC_DQ_SLOT_CAPACITY;
        if (lc_delivery_queue_init(&g_lc_dq, &dq_ops, &lim) != AICAM_OK) {
            memset(&g_lc_dq, 0, sizeof(g_lc_dq));
        }
    }
    g_lc_boot_id = osKernelGetTickCount();
    g_lc_report_seq = 0;

    lc_app_ops_t ops;
    memset(&ops, 0, sizeof(ops));
    ops.get_model_info = lc_shell_get_model_info;
    ops.get_class_name = lc_shell_get_class_name;
    ops.now_ms = lc_shell_now_ms;
    ops.load_totals = lc_shell_load_totals;
    ops.save_totals = lc_shell_save_totals;
    ops.persist_config = lc_shell_persist_config;
    ops.queue_clear = lc_shell_queue_clear;
    ops.txn_prepare = lc_shell_txn_prepare;
    ops.txn_get = lc_shell_txn_get;
    ops.txn_clear = lc_shell_txn_clear;

    lc_app_init(&g_lc_app, &ops, &cfg);

    aicam_result_t tr = lc_app_recover_transaction(&g_lc_app);
    if (tr != AICAM_OK) {
        LOG_CORE_ERROR("LC_TXN_RECOVERY_FAILED r=%d", tr);
        return tr;
    }

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

    osMutexAcquire(g_lc.mutex, osWaitForever);
    (void)lc_app_on_ai_result(&g_lc_app, &frame, timestamp_ms);
    osMutexRelease(g_lc.mutex);
}

aicam_bool_t line_counting_is_enabled(void) {
    if (!g_lc.inited) return AICAM_FALSE;
    line_counting_config_t cfg;
    if (json_config_get_line_counting_config(&cfg) != AICAM_OK) return AICAM_FALSE;
    return cfg.enable ? AICAM_TRUE : AICAM_FALSE;
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

    osMutexAcquire(g_lc.mutex, osWaitForever);
    lc_window_summary_t closed;
    aicam_result_t r = lc_app_apply_config(&g_lc_app, candidate, &closed);
    osMutexRelease(g_lc.mutex);
    return r;
}

aicam_result_t line_counting_reset(void) {
    if (!g_lc.inited) return AICAM_ERROR_NOT_INITIALIZED;
    if (g_lc_reset_requested || g_lc_reset_in_progress) return AICAM_ERROR_BUSY;
    g_lc_reset_requested = 1;
    return AICAM_OK;
}

uint8_t line_counting_is_resetting(void) {
    return (uint8_t)(g_lc_reset_requested || g_lc_reset_in_progress);
}

aicam_result_t line_counting_get_heat(uint32_t *out_grid) {
    if (!out_grid) return AICAM_ERROR_INVALID_PARAM;
    if (!g_lc.inited) return AICAM_ERROR_NOT_INITIALIZED;
    osMutexAcquire(g_lc.mutex, osWaitForever);
    memcpy(out_grid, g_lc_app.heat, sizeof(g_lc_app.heat));
    osMutexRelease(g_lc.mutex);
    return AICAM_OK;
}

aicam_result_t line_counting_get_delivery_stats(lc_delivery_stats_t *out) {
    if (!out) return AICAM_ERROR_INVALID_PARAM;
    lc_dq_stats_t qs;
    lc_delivery_queue_get_stats(&g_lc_dq, &qs);
    out->mqtt_backlog = qs.mqtt_pending;
    out->mqtt_dropped = qs.dropped_mqtt;
    out->webhook_backlog = qs.webhook_pending;
    out->webhook_dropped = qs.dropped_webhook;
    out->dropped_reports = g_lc_dropped_reports;
    return AICAM_OK;
}

#endif
