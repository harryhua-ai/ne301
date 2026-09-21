/* Custom/Tasks/Src/people_counting.c */
#include "people_counting.h"
#include "pc_tracker.h"
#include "pc_line_cross.h"
#include "json_config_mgr.h"
#include "ai_service.h"
#include "ai_draw_service.h"
#include "mqtt_service.h"
#include "webhook_service.h"
#include "pc_backlog.h"
#include "storage.h"      /* flash_lfs_* for totals persistence (Task 15) */
#include "device_service.h"
#include "debug.h"        /* LOG_CORE_INFO */
#include "cmsis_os2.h"
#include "common_utils.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>      /* snprintf */

/* CANONICAL g_pc struct definition -- Tasks 9, 10, 11, 14, 15 add members here
 * rather than re-declaring the struct. Keep all runtime state in this one place. */
static struct {
    pc_tracker_t*           tracker;
    pc_line_cross_t*        line;
    people_counting_stats_t stats;
    osTimerId_t             window_timer;
    osMutexId_t             mutex;
    aicam_bool_t            inited;
    /* Task 9: pending track-record list for window reporting */
    pc_track_record_t**     pending;
    uint16_t                pending_count;
    uint16_t                pending_cap;
    /* Task 14: static backlog-drain buffer (16KB; NOT on timer task stack) */
    char                    drain_buf[16*1024];
    /* Task 10: cached window period for the timer (ms) */
    uint32_t                window_period_ms;
    /* Cached config snapshot to detect runtime config changes and rebuild
     * tracker/line/timer accordingly (fix: config hot-reload). */
    struct {
        uint16_t line_x1, line_y1, line_x2, line_y2;
        uint16_t outside_x, outside_y;
        uint16_t max_dist_permille;
        uint16_t window_minutes;
        uint8_t  track_history_k, max_miss, k_confirm;
    } cfg_snap;
    /* Frame skip counter — process every Nth frame to reduce per-frame
     * overhead on the camera pipeline thread. At 25fps and N=5, the tracker
     * updates at 5fps which is more than enough for people tracking. */
    uint8_t                 frame_skip_cnt;
    /* Debug counters — exposed via stats API for diagnostics */
    uint32_t dbg_sub_calls;
    uint32_t dbg_matched_person;
    int      dbg_last_type;
    int      dbg_last_nb_detect;
    char     dbg_last_class[32];
    uint32_t dbg_last_det_x_permille;
    uint32_t dbg_last_det_y_permille;
} g_pc;

#define PC_FRAME_SKIP 1u

/* Task 11: JSON report buffer (separate from g_pc.drain_buf) */
static char pc_json_buf[16*1024];

/* Task 11: segment end type name mapping */
static const char* SEG_TYPE_NAME[] = { "departed", "crossing" };

/* Maximum pending track records before we start dropping (prevents unbounded
 * memory growth under heavy traffic). 256 entries × ~200 bytes ≈ 50KB cap. */
#define PC_MAX_PENDING 256

/* Helper: push track record onto pending list (Task 9) */
static void pending_push(pc_track_record_t* r) {
    if (g_pc.pending_count >= PC_MAX_PENDING) {
        /* Cap reached: drop the oldest to make room (FIFO eviction) */
        PC_FREE(g_pc.pending[0]);
        memmove(&g_pc.pending[0], &g_pc.pending[1],
                (g_pc.pending_count - 1) * sizeof(*g_pc.pending));
        g_pc.pending_count--;
    }
    if (g_pc.pending_count >= g_pc.pending_cap) {
        uint16_t newcap = g_pc.pending_cap ? g_pc.pending_cap * 2 : 16;
        if (newcap > PC_MAX_PENDING) newcap = PC_MAX_PENDING;
        pc_track_record_t** grown = (pc_track_record_t**)PC_MALLOC(newcap * sizeof(*grown));
        if (!grown) { PC_FREE(r); return; }
        if (g_pc.pending) {
            memcpy(grown, g_pc.pending, g_pc.pending_count * sizeof(*grown));
            PC_FREE(g_pc.pending);
        }
        g_pc.pending = grown; g_pc.pending_cap = newcap;
    }
    g_pc.pending[g_pc.pending_count++] = r;
}

/* Returns a stable device identifier for MQTT topic / report fields.
 * Uses mac_address from device_service when available; falls back to a
 * constant. Result is a pointer into a static buffer — do not free. */
static const char* pc_device_id_str(void) {
    static char id[32];
    device_info_config_t info;
    if (device_service_get_info(&info) == AICAM_OK && info.mac_address[0]) {
        /* copy mac, NUL-terminate within id[] bounds */
        size_t n = strlen(info.mac_address);
        if (n >= sizeof(id)) n = sizeof(id) - 1;
        memcpy(id, info.mac_address, n);
        id[n] = '\0';
        return id;
    }
    strncpy(id, "ne301-unknown", sizeof(id) - 1);
    id[sizeof(id) - 1] = '\0';
    return id;
}

/* Task 15: persist total_in/out to /config/pc_totals.json so they survive reboot.
 * File format: {"total_in":N,"total_out":N} (no whitespace, < 64 bytes). */
#define PC_TOTALS_PATH "/config/pc_totals.json"

static void pc_totals_load(void) {
    void* fd = flash_lfs_fopen(PC_TOTALS_PATH, "r");
    if (!fd) return;
    char buf[80];
    int n = flash_lfs_fread(fd, buf, sizeof(buf) - 1);
    flash_lfs_fclose(fd);
    if (n <= 0) return;
    buf[n] = '\0';
    cJSON* o = cJSON_Parse(buf);
    if (!o) return;
    cJSON* jin  = cJSON_GetObjectItem(o, "total_in");
    cJSON* jout = cJSON_GetObjectItem(o, "total_out");
    if (cJSON_IsNumber(jin))  g_pc.stats.total_in  = (uint32_t)jin->valueint;
    if (cJSON_IsNumber(jout)) g_pc.stats.total_out = (uint32_t)jout->valueint;
    cJSON_Delete(o);
}

static void pc_totals_save(void) {
    char buf[80];
    int len = snprintf(buf, sizeof(buf),
        "{\"total_in\":%lu,\"total_out\":%lu}",
        (unsigned long)g_pc.stats.total_in,
        (unsigned long)g_pc.stats.total_out);
    if (len <= 0 || (size_t)len >= sizeof(buf)) return;
    void* fd = flash_lfs_fopen(PC_TOTALS_PATH, "w");
    if (!fd) return;
    (void)flash_lfs_fwrite(fd, buf, (size_t)len);
    flash_lfs_fclose(fd);
}

/* Task 11: Builds the window report JSON into pc_json_buf. Returns length, 0 on error. */
static size_t build_report_json(uint32_t window_start, uint32_t window_end) {
    const aicam_global_config_t* cfg_g = json_config_get_config_ro();
    const people_counting_config_t* cfg = &cfg_g->people_counting;
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "people_counting");
    /* device_id — use mac_address from device_service */
    cJSON_AddStringToObject(root, "device_id", pc_device_id_str());
    cJSON_AddNumberToObject(root, "boot_id", g_pc.stats.boot_id);
    cJSON_AddStringToObject(root, "boot_id_kind", g_pc.stats.boot_id_kind);

    cJSON* win = cJSON_CreateObject();
    cJSON_AddNumberToObject(win, "start_ms", window_start);
    cJSON_AddNumberToObject(win, "end_ms", window_end);
    cJSON_AddNumberToObject(win, "duration_min", cfg->window_minutes);
    cJSON_AddNumberToObject(win, "in", g_pc.stats.window_in);
    cJSON_AddNumberToObject(win, "out", g_pc.stats.window_out);
    cJSON_AddItemToObject(root, "window", win);

    cJSON* tot = cJSON_CreateObject();
    cJSON_AddNumberToObject(tot, "in", g_pc.stats.total_in);
    cJSON_AddNumberToObject(tot, "out", g_pc.stats.total_out);
    cJSON_AddItemToObject(root, "total", tot);

    /* tracks — cap at 32 entries to bound cJSON allocation count (each entry
     * with up to 16 points creates ~70 cJSON nodes; 32 × 70 = ~2240 mallocs). */
    cJSON* tracks = cJSON_CreateArray();
    if (cfg->tracks_report_enable) {
        uint16_t track_cap = (g_pc.pending_count > 32) ? 32 : g_pc.pending_count;
        for (uint16_t i = 0; i < track_cap; ++i) {
            const pc_track_record_t* r = g_pc.pending[i];
            cJSON* trk = cJSON_CreateObject();
            cJSON_AddNumberToObject(trk, "track_id", r->track_id);
            cJSON_AddNumberToObject(trk, "segment_id", r->segment_id);
            cJSON_AddNumberToObject(trk, "entered_at_ms", r->entered_at_ms);
            cJSON_AddNumberToObject(trk, "seg_start_ms", r->seg_start_ms);
            cJSON_AddNumberToObject(trk, "seg_end_ms", r->seg_end_ms);
            cJSON_AddStringToObject(trk, "seg_end_type", SEG_TYPE_NAME[r->seg_end_type]);
            cJSON* evs = cJSON_CreateArray();
            if (r->events & PC_BIT_IN)  cJSON_AddItemToArray(evs, cJSON_CreateString("line_cross_in"));
            if (r->events & PC_BIT_OUT) cJSON_AddItemToArray(evs, cJSON_CreateString("line_cross_out"));
            cJSON_AddItemToObject(trk, "events", evs);
            cJSON* pts = cJSON_CreateArray();
            const uint32_t* pts_ts = pc_track_record_point_ts_const(r);
            for (uint8_t p = 0; p < r->nb_points; ++p) {
                cJSON* pt = cJSON_CreateArray();
                cJSON_AddItemToArray(pt, cJSON_CreateNumber((double)r->points[p].x));
                cJSON_AddItemToArray(pt, cJSON_CreateNumber((double)r->points[p].y));
                cJSON_AddItemToArray(pt, cJSON_CreateNumber((double)pts_ts[p]));
                cJSON_AddItemToArray(pts, pt);
            }
            cJSON_AddItemToObject(trk, "points", pts);
            cJSON_AddItemToArray(tracks, trk);
        }
    }
    cJSON_AddItemToObject(root, "tracks", tracks);

    /* heat_grid */
    if (cfg->heat_grid_enable) {
        cJSON* hg = cJSON_CreateObject();
        cJSON_AddNumberToObject(hg, "width", 16);
        cJSON_AddNumberToObject(hg, "height", 16);
        cJSON* data = cJSON_CreateArray();
        for (int i = 0; i < 16*16; ++i) cJSON_AddItemToArray(data, cJSON_CreateNumber(g_pc.stats.heat[i]));
        cJSON_AddItemToObject(hg, "data", data);
        cJSON_AddItemToObject(root, "heat_grid", hg);
    } else {
        cJSON_AddNullToObject(root, "heat_grid");
    }

    cJSON_AddNumberToObject(root, "dropped_windows_mqtt", g_pc.stats.dropped_windows_mqtt);
    cJSON_AddNumberToObject(root, "dropped_windows_webhook", g_pc.stats.dropped_windows_webhook);

    cJSON* line = cJSON_CreateObject();
    cJSON_AddNumberToObject(line, "x1", cfg->line_x1_permille/1000.0);
    cJSON_AddNumberToObject(line, "y1", cfg->line_y1_permille/1000.0);
    cJSON_AddNumberToObject(line, "x2", cfg->line_x2_permille/1000.0);
    cJSON_AddNumberToObject(line, "y2", cfg->line_y2_permille/1000.0);
    cJSON_AddItemToObject(root, "line", line);

    cJSON_AddStringToObject(root, "model", cfg->model_name);
    cJSON_AddStringToObject(root, "target_class", cfg->target_class_name);
    cJSON_AddNumberToObject(root, "conf_threshold_permille", cfg->conf_threshold_permille);

    char* printed = cJSON_PrintUnformatted(root);
    if (!printed) { cJSON_Delete(root); return 0; }
    size_t len = strlen(printed);
    if (len < sizeof(pc_json_buf)) memcpy(pc_json_buf, printed, len+1);
    else len = 0;
    cJSON_free(printed);
    cJSON_Delete(root);
    return len;
}

/* Task 12: heat grid accumulation callback */
static void heat_accumulate_cb(const pc_track_t* trk, void* user) {
    uint32_t* heat = (uint32_t*)user;
    /* use the latest history point (the current stable position) */
    pc_point_t p = trk->last_pos;
    int gx = (int)(p.x * 16.0f); if (gx > 15) gx = 15; if (gx < 0) gx = 0;
    int gy = (int)(p.y * 16.0f); if (gy > 15) gy = 15; if (gy < 0) gy = 0;
    heat[gy * 16 + gx]++;
}

/* window timer callback */
static void window_timer_cb(void* arg);  /* forward */

static void pc_window_report_task(void* arg);
static osThreadId_t    pc_report_thread;
static osSemaphoreId_t pc_report_sem;
static uint8_t pc_report_task_stack[8 * 1024] ALIGN_32 IN_PSRAM;

aicam_result_t people_counting_init(void) {
    if (g_pc.inited) return AICAM_OK;
    memset(&g_pc, 0, sizeof(g_pc));

    /* boot id */
    g_pc.stats.boot_id = osKernelGetTickCount();
    g_pc.stats.boot_id_kind = "monotonic";

    /* Task 15: restore persisted totals */
    pc_totals_load();

    g_pc.mutex = osMutexNew(NULL);
    if (!g_pc.mutex) return AICAM_ERROR_NO_MEMORY;

    /* Register AI result subscriber for people counting */
    ai_service_register_subscriber(people_counting_on_ai_result);

    g_pc.inited = AICAM_TRUE;

    /* Spec 7.4 log event: PC_CONFIG_LOADED */
    const aicam_global_config_t* cfg = json_config_get_config_ro();
    LOG_CORE_INFO("PC_CONFIG_LOADED window_minutes=%u total_in=%u total_out=%u",
                  (unsigned)cfg->people_counting.window_minutes,
                  (unsigned)g_pc.stats.total_in, (unsigned)g_pc.stats.total_out);
    pc_report_sem = osSemaphoreNew(4, 0, NULL);
    if (!pc_report_sem) return AICAM_ERROR_NO_MEMORY;
    {
        osThreadAttr_t report_attr = {
            .name = "pc_report",
            .stack_mem = pc_report_task_stack,
            .stack_size = sizeof(pc_report_task_stack),
            .priority = osPriorityBelowNormal,
        };
        pc_report_thread = osThreadNew(pc_window_report_task, NULL, &report_attr);
        if (!pc_report_thread) return AICAM_ERROR_NO_MEMORY;
    }
    {
        g_pc.window_period_ms = cfg->people_counting.window_minutes * 60u * 1000u;
        if (g_pc.window_period_ms == 0) g_pc.window_period_ms = 5u * 60u * 1000u;
        osTimerAttr_t attr = { .name = "pc_win" };
        g_pc.window_timer = osTimerNew(window_timer_cb, osTimerPeriodic, NULL, &attr);
        if (g_pc.window_timer) osTimerStart(g_pc.window_timer, g_pc.window_period_ms);
    }
    LOG_CORE_INFO("people_counting initialized");
    return AICAM_OK;
}

void people_counting_on_ai_result(const nn_result_t* result, uint32_t ts) {
    if (!g_pc.inited || !result) return;

    /* Debug counters for diagnostics via stats API — counted regardless of enable
     * so operators can confirm the subscriber is firing even when PC is off. */
    g_pc.dbg_sub_calls++;
    g_pc.dbg_last_type = (int)result->type;
    g_pc.dbg_last_nb_detect = (int)result->od.nb_detect;

    const aicam_global_config_t* cfg_g = json_config_get_config_ro();
    const people_counting_config_t* cfg = &cfg_g->people_counting;
    if (!cfg->enable) return;
    if (result->type != PP_TYPE_OD) return;

    /* Frame skipping — process every Nth frame to reduce per-frame overhead
     * on the camera pipeline (realtime) thread. The tracker is resilient to
     * missed frames (max_miss handles gaps). At 25fps × skip=5 → 5fps tracking. */
    if (++g_pc.frame_skip_cnt < PC_FRAME_SKIP) return;
    g_pc.frame_skip_cnt = 0;

    if (result->od.nb_detect > 0) {
        const char* cn = result->od.detects[0].class_name;
        strncpy(g_pc.dbg_last_class, cn ? cn : "(null)", sizeof(g_pc.dbg_last_class) - 1);
        g_pc.dbg_last_class[sizeof(g_pc.dbg_last_class) - 1] = '\0';
    }

    /* collect detections matching target class + threshold (pure locals) */
    pc_point_t detects[64];
    uint8_t n = 0;
    float conf_thr = cfg->conf_threshold_permille / 1000.0f;
    for (uint8_t i = 0; i < result->od.nb_detect && n < 64; ++i) {
        const od_detect_t* d = &result->od.detects[i];
        if (d->conf < conf_thr) continue;
        if (!d->class_name || strcmp(d->class_name, cfg->target_class_name) != 0) continue;
        g_pc.dbg_matched_person++;
        detects[n].x = d->x + d->width * 0.5f;
        detects[n].y = d->y + d->height * 0.5f;
        /* record the matched person's center (permille) for diagnostics */
        g_pc.dbg_last_det_x_permille = (uint32_t)(detects[n].x * 1000.0f + 0.5f);
        g_pc.dbg_last_det_y_permille = (uint32_t)(detects[n].y * 1000.0f + 0.5f);
        n++;
    }

    /* Non-blocking on the realtime camera thread: if the window-report timer
     * currently holds the mutex (snapshot + JSON build), just skip tracking
     * for this frame rather than blocking the camera pipeline. The tracker is
     * resilient to occasional dropped frames (max_miss). */
    if (osMutexAcquire(g_pc.mutex, 0) != osOK) return;

    /* Rebuild tracker / line / timer when relevant config fields change.
     * MUST run under the mutex: the window timer holds this same mutex while
     * snapshotting g_pc.tracker, so destroying/replacing tracker or line
     * outside it would race into a use-after-free. On trylock failure above
     * we simply retry the rebuild on the next frame (cfg_snap is unchanged). */
    int line_changed  = (g_pc.cfg_snap.line_x1      != cfg->line_x1_permille) ||
                        (g_pc.cfg_snap.line_y1      != cfg->line_y1_permille) ||
                        (g_pc.cfg_snap.line_x2      != cfg->line_x2_permille) ||
                        (g_pc.cfg_snap.line_y2      != cfg->line_y2_permille) ||
                        (g_pc.cfg_snap.outside_x    != cfg->outside_x_permille) ||
                        (g_pc.cfg_snap.outside_y    != cfg->outside_y_permille) ||
                        (g_pc.line == NULL);
    int trk_changed   = (g_pc.cfg_snap.max_dist_permille != cfg->max_dist_permille) ||
                        (g_pc.cfg_snap.track_history_k   != cfg->track_history_k) ||
                        (g_pc.cfg_snap.max_miss          != cfg->max_miss) ||
                        (g_pc.cfg_snap.k_confirm         != cfg->k_confirm) ||
                        (g_pc.tracker == NULL);
    int window_changed = (g_pc.cfg_snap.window_minutes != cfg->window_minutes);

    if (line_changed) {
        if (g_pc.line) pc_line_cross_destroy(g_pc.line);
        g_pc.line = pc_line_cross_create(
            cfg->line_x1_permille/1000.0f, cfg->line_y1_permille/1000.0f,
            cfg->line_x2_permille/1000.0f, cfg->line_y2_permille/1000.0f,
            cfg->outside_x_permille/1000.0f, cfg->outside_y_permille/1000.0f);
        g_pc.cfg_snap.line_x1   = cfg->line_x1_permille;
        g_pc.cfg_snap.line_y1   = cfg->line_y1_permille;
        g_pc.cfg_snap.line_x2   = cfg->line_x2_permille;
        g_pc.cfg_snap.line_y2   = cfg->line_y2_permille;
        g_pc.cfg_snap.outside_x = cfg->outside_x_permille;
        g_pc.cfg_snap.outside_y = cfg->outside_y_permille;
    }
    if (trk_changed) {
        uint32_t next_id = (g_pc.tracker) ? pc_tracker_next_id(g_pc.tracker) : 1u;
        if (g_pc.tracker) pc_tracker_destroy(g_pc.tracker);
        pc_tracker_config_t tc = {
            .max_dist_permille = cfg->max_dist_permille,
            .track_history_k   = cfg->track_history_k,
            .max_miss          = cfg->max_miss,
            .k_confirm         = cfg->k_confirm,
        };
        g_pc.tracker = pc_tracker_create(&tc, next_id);
        g_pc.cfg_snap.max_dist_permille = cfg->max_dist_permille;
        g_pc.cfg_snap.track_history_k   = cfg->track_history_k;
        g_pc.cfg_snap.max_miss          = cfg->max_miss;
        g_pc.cfg_snap.k_confirm         = cfg->k_confirm;
    }
    if (window_changed && g_pc.window_timer) {
        g_pc.window_period_ms = cfg->window_minutes * 60u * 1000u;
        if (g_pc.window_period_ms == 0) g_pc.window_period_ms = 5u * 60u * 1000u;
        osTimerStart(g_pc.window_timer, g_pc.window_period_ms);
        g_pc.cfg_snap.window_minutes = cfg->window_minutes;
    }

    if (!g_pc.tracker || !g_pc.line) {
        osMutexRelease(g_pc.mutex);
        return;
    }

    pc_track_record_t** recs = NULL; uint16_t n_recs = 0;
    pc_tracker_update(g_pc.tracker, detects, n, ts, &recs, &n_recs);
    uint32_t win_in = 0, win_out = 0, tot_in = 0, tot_out = 0;
    pc_tracker_check_line_crossings(g_pc.tracker, g_pc.line, ts, &win_in, &win_out, &tot_in, &tot_out);
    g_pc.stats.window_in += win_in;  g_pc.stats.window_out += win_out;
    g_pc.stats.total_in  += tot_in;  g_pc.stats.total_out  += tot_out;
    if (cfg->heat_grid_enable) {
        pc_tracker_for_each_stable(g_pc.tracker, heat_accumulate_cb, g_pc.stats.heat);
    }
    for (uint16_t k = 0; k < n_recs; ++k) pending_push(recs[k]);
    PC_FREE(recs);
    osMutexRelease(g_pc.mutex);
}

const people_counting_stats_t* people_counting_get_stats(void) { return &g_pc.stats; }
people_counting_debug_t people_counting_get_debug(void) {
    people_counting_debug_t d = {0};
    d.sub_calls = g_pc.dbg_sub_calls;
    d.matched_person = g_pc.dbg_matched_person;
    d.last_type = g_pc.dbg_last_type;
    d.last_nb_detect = g_pc.dbg_last_nb_detect;
    memcpy(d.last_class, g_pc.dbg_last_class, sizeof(d.last_class));
    d.last_det_x_permille = g_pc.dbg_last_det_x_permille;
    d.last_det_y_permille = g_pc.dbg_last_det_y_permille;
    return d;
}
void people_counting_reset_totals(void) {
    if (!g_pc.inited || g_pc.mutex == NULL) return;
    osMutexAcquire(g_pc.mutex, osWaitForever);
    g_pc.stats.window_in = 0;   g_pc.stats.window_out = 0;
    g_pc.stats.total_in  = 0;   g_pc.stats.total_out  = 0;
    g_pc.stats.dropped_windows_mqtt    = 0;
    g_pc.stats.dropped_windows_webhook = 0;
    memset(g_pc.stats.heat, 0, sizeof(g_pc.stats.heat));
    osMutexRelease(g_pc.mutex);
    /* Task 15: persist zeroed totals */
    pc_totals_save();
}

aicam_bool_t people_counting_is_enabled(void) {
    if (!g_pc.inited) return AICAM_FALSE;
    return json_config_get_config_ro()->people_counting.enable ? AICAM_TRUE : AICAM_FALSE;
}

void people_counting_draw_overlay(uint8_t *fb, int w, int h) {
    if (!g_pc.inited || !fb || w <= 0 || h <= 0) return;
    const aicam_global_config_t* cfg_g = json_config_get_config_ro();
    const people_counting_config_t* cfg = &cfg_g->people_counting;
    if (!cfg->enable) return;

    /* draw the counting line (normalized coords from permille config) */
    (void)ai_draw_count_line(fb, w, h,
        cfg->line_x1_permille / 1000.0f, cfg->line_y1_permille / 1000.0f,
        cfg->line_x2_permille / 1000.0f, cfg->line_y2_permille / 1000.0f,
        cfg->outside_x_permille / 1000.0f, cfg->outside_y_permille / 1000.0f);

    /* draw IN/OUT text top-left with a small margin */
    (void)ai_draw_count_text(fb, w, h, 8, 8,
        g_pc.stats.window_in, g_pc.stats.window_out);
}

static void window_timer_cb(void* arg) {
    (void)arg;
    if (!g_pc.inited) return;
    /* People counting disabled — nothing accumulates while disabled, so
     * reporting (and persisting) all-zero windows would only produce noise
     * on MQTT/webhook. */
    if (!json_config_get_config_ro()->people_counting.enable) return;
    (void)osSemaphoreRelease(pc_report_sem);
}

static void pc_window_report_task(void* arg) {
    (void)arg;
    for (;;) {
        if (osSemaphoreAcquire(pc_report_sem, osWaitForever) != osOK) continue;
        if (!g_pc.inited) continue;
        if (!json_config_get_config_ro()->people_counting.enable) continue;

    /* ---- Phase 1: under mutex — snapshot, build JSON, reset (fast) ---- */
    osMutexAcquire(g_pc.mutex, osWaitForever);
    uint32_t now = osKernelGetTickCount();
    uint32_t duration_ms = g_pc.window_period_ms ? g_pc.window_period_ms : (5u * 60u * 1000u);

    /* snapshot active tracks → CROSSING records */
    if (g_pc.tracker) {
        pc_track_record_t** snap = NULL; uint16_t n_snap = 0;
        pc_tracker_window_snapshot(g_pc.tracker, now, &snap, &n_snap);
        for (uint16_t k = 0; k < n_snap; ++k) pending_push(snap[k]);
        PC_FREE(snap);
    }

    uint32_t window_end   = now;
    uint32_t window_start = now - duration_ms;
    size_t jlen = build_report_json(window_start, window_end);

    const people_counting_config_t* cfg = &json_config_get_config_ro()->people_counting;

    g_pc.stats.window_in = 0; g_pc.stats.window_out = 0;
    g_pc.stats.window_start_ts = now;
    g_pc.stats.last_report_ts = now;
    memset(g_pc.stats.heat, 0, sizeof(g_pc.stats.heat));
    for (uint16_t k = 0; k < g_pc.pending_count; ++k) PC_FREE(g_pc.pending[k]);
    g_pc.pending_count = 0;
    osMutexRelease(g_pc.mutex);

    /* ---- Phase 2: outside mutex — dispatch reports (slow I/O) ---- */
    if (jlen == 0) {
        LOG_CORE_INFO("PC_WINDOW_REPORT_BUILD_FAILED");
    } else {
        if (cfg->mqtt_report_enable) {
            if (mqtt_service_is_connected()) {
                char topic[80];
                snprintf(topic, sizeof(topic), "device/%s/people-count", pc_device_id_str());
                mqtt_service_publish_json(topic, pc_json_buf, 1, 0);
                /* Bounded drain: backlog_pop deletes a file on every success,
                 * so the loop always terminates even if a pop fails (e.g.
                 * flash read error leaves the file in place). */
                for (int drained = 0; drained < cfg->backlog_capacity + 1; ++drained) {
                    if (backlog_pop(BACKLOG_MQTT, g_pc.drain_buf, sizeof(g_pc.drain_buf)) != AICAM_OK)
                        break;
                    mqtt_service_publish_json(topic, g_pc.drain_buf, 1, 0);
                }
            } else {
                if (backlog_push(BACKLOG_MQTT, pc_json_buf, cfg->backlog_capacity) != AICAM_OK)
                    g_pc.stats.dropped_windows_mqtt++;
            }
        }

        if (cfg->webhook_report_enable) {
            webhook_config_t wc;
            if (json_config_get_webhook_config(&wc) == AICAM_OK && wc.enable && wc.url[0]) {
                if (webhook_service_push_json(wc.url, pc_json_buf, jlen) == AICAM_OK) {
                    for (int drained = 0; drained < cfg->backlog_capacity + 1; ++drained) {
                        if (backlog_pop(BACKLOG_WEBHOOK, g_pc.drain_buf, sizeof(g_pc.drain_buf)) != AICAM_OK)
                            break;
                        webhook_service_push_json(wc.url, g_pc.drain_buf, strlen(g_pc.drain_buf));
                    }
                } else {
                    if (backlog_push(BACKLOG_WEBHOOK, pc_json_buf, cfg->backlog_capacity) != AICAM_OK)
                        g_pc.stats.dropped_windows_webhook++;
                }
            } else if (json_config_get_webhook_config(&wc) == AICAM_OK && !wc.enable) {
                /* webhook disabled — silently drop */
            }
        }
    }

        pc_totals_save();
    }
}
