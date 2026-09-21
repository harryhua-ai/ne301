#include "line_counting.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);            \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

static void snap_init(lc_report_snapshot_t *snap) {
    memset(snap, 0, sizeof(*snap));
    snprintf(snap->device_id, sizeof(snap->device_id), "AA:BB:CC:DD:EE:FF");
    snap->boot_id = 386271;
    snap->report_seq = 128;
    snap->clock_valid = 1;
    snprintf(snap->reported_at, sizeof(snap->reported_at), "2026-09-17T10:42:15.123Z");
    snprintf(snap->window_start_time, sizeof(snap->window_start_time),
             "2026-09-17T10:37:15.000Z");
    snprintf(snap->window_end_time, sizeof(snap->window_end_time), "2026-09-17T10:42:15.000Z");
    snap->window_duration_sec = 300;
    snap->window_in = 38;
    snap->window_out = 21;
    snap->total_in = 1264;
    snap->total_out = 1189;
    snprintf(snap->counter_name, sizeof(snap->counter_name), "客流统计");
    snprintf(snap->target_class_name, sizeof(snap->target_class_name), "person");
    snprintf(snap->model_name, sizeof(snap->model_name), "person_yolo_v2");
    snprintf(snap->model_version, sizeof(snap->model_version), "2.0.0");
    snap->line_x1 = 0.20f;
    snap->line_y1 = 0.50f;
    snap->line_x2 = 0.80f;
    snap->line_y2 = 0.50f;
    snap->outside_x = 0.50f;
    snap->outside_y = 0.20f;
    snap->confidence_threshold = 0.50f;
    snap->tracks_report_enable = 0;
    snap->heat_grid_enable = 0;
}

static void test_schema_v1_fields(void) {
    lc_report_snapshot_t snap;
    snap_init(&snap);
    static char buf[8192];
    size_t len = lc_report_build_v1(&snap, buf, sizeof(buf));
    CHECK(len > 0);

    cJSON *root = cJSON_Parse(buf);
    CHECK(root != NULL);
    if (!root) return;
    cJSON *sv = cJSON_GetObjectItem(root, "schema_version");
    CHECK(cJSON_IsNumber(sv) && sv->valueint == 1);
    cJSON *type = cJSON_GetObjectItem(root, "type");
    CHECK(cJSON_IsString(type) && strcmp(type->valuestring, "line_counting") == 0);
    cJSON *dev = cJSON_GetObjectItem(root, "device_id");
    CHECK(cJSON_IsString(dev) && strcmp(dev->valuestring, "AA:BB:CC:DD:EE:FF") == 0);
    cJSON *boot = cJSON_GetObjectItem(root, "boot_id");
    CHECK(cJSON_IsNumber(boot) && boot->valueint == 386271);
    cJSON *seq = cJSON_GetObjectItem(root, "report_seq");
    CHECK(cJSON_IsNumber(seq) && seq->valueint == 128);
    cJSON *clock_valid = cJSON_GetObjectItem(root, "clock_valid");
    CHECK(cJSON_IsBool(clock_valid) && cJSON_IsTrue(clock_valid));

    cJSON *win = cJSON_GetObjectItem(root, "window");
    CHECK(cJSON_IsObject(win));
    cJSON *in = cJSON_GetObjectItem(win, "in");
    cJSON *out = cJSON_GetObjectItem(win, "out");
    cJSON *dur = cJSON_GetObjectItem(win, "duration_sec");
    CHECK(cJSON_IsNumber(in) && in->valueint == 38);
    CHECK(cJSON_IsNumber(out) && out->valueint == 21);
    CHECK(cJSON_IsNumber(dur) && dur->valueint == 300);
    cJSON *tot = cJSON_GetObjectItem(root, "total");
    CHECK(cJSON_IsNumber(cJSON_GetObjectItem(tot, "in")));
    CHECK(cJSON_GetObjectItem(tot, "in")->valueint == 1264);
    CHECK(cJSON_GetObjectItem(tot, "out")->valueint == 1189);

    cJSON *counter = cJSON_GetObjectItem(root, "counter");
    cJSON *name = cJSON_GetObjectItem(counter, "counter_name");
    CHECK(cJSON_IsString(name) && strcmp(name->valuestring, "客流统计") == 0);

    cJSON *target = cJSON_GetObjectItem(root, "target");
    CHECK(cJSON_IsString(cJSON_GetObjectItem(target, "class_name")));
    CHECK(strcmp(cJSON_GetObjectItem(target, "class_name")->valuestring, "person") == 0);

    cJSON *model = cJSON_GetObjectItem(root, "model");
    CHECK(strcmp(cJSON_GetObjectItem(model, "name")->valuestring, "person_yolo_v2") == 0);
    CHECK(strcmp(cJSON_GetObjectItem(model, "version")->valuestring, "2.0.0") == 0);

    cJSON *line = cJSON_GetObjectItem(root, "line");
    CHECK(line != NULL);
    CHECK(cJSON_GetObjectItem(line, "x1")->valuedouble > 0.199);
    CHECK(cJSON_GetObjectItem(line, "x1")->valuedouble < 0.201);
    CHECK(cJSON_GetObjectItem(line, "outside_y")->valuedouble > 0.199);
    CHECK(cJSON_GetObjectItem(line, "outside_y")->valuedouble < 0.201);

    cJSON *config = cJSON_GetObjectItem(root, "config");
    CHECK(cJSON_GetObjectItem(config, "confidence_threshold")->valuedouble > 0.499);
    CHECK(cJSON_GetObjectItem(config, "confidence_threshold")->valuedouble < 0.501);
    cJSON_Delete(root);
    CHECK(strstr(buf, "permille") == NULL);
}

static void test_iso8601_and_invalid_clock(void) {
    lc_report_snapshot_t snap;
    snap_init(&snap);
    static char buf[8192];
    CHECK(lc_report_build_v1(&snap, buf, sizeof(buf)) > 0);
    cJSON *root = cJSON_Parse(buf);
    CHECK(root != NULL);
    cJSON *ra = cJSON_GetObjectItem(root, "reported_at");
    CHECK(cJSON_IsString(ra));
    CHECK(strcmp(ra->valuestring, "2026-09-17T10:42:15.123Z") == 0);
    cJSON *win = cJSON_GetObjectItem(root, "window");
    CHECK(strcmp(cJSON_GetObjectItem(win, "start_time")->valuestring,
                 "2026-09-17T10:37:15.000Z") == 0);
    CHECK(strcmp(cJSON_GetObjectItem(win, "end_time")->valuestring,
                 "2026-09-17T10:42:15.000Z") == 0);
    cJSON_Delete(root);

    snap.clock_valid = 0;
    snap.reported_at[0] = '\0';
    snap.window_start_time[0] = '\0';
    snap.window_end_time[0] = '\0';
    CHECK(lc_report_build_v1(&snap, buf, sizeof(buf)) > 0);
    root = cJSON_Parse(buf);
    CHECK(root != NULL);
    cJSON *ra2 = cJSON_GetObjectItem(root, "reported_at");
    CHECK(cJSON_IsNull(ra2));
    cJSON *win2 = cJSON_GetObjectItem(root, "window");
    CHECK(cJSON_IsNull(cJSON_GetObjectItem(win2, "start_time")));
    CHECK(cJSON_IsNull(cJSON_GetObjectItem(win2, "end_time")));
    cJSON *dur2 = cJSON_GetObjectItem(win2, "duration_sec");
    CHECK(cJSON_IsNumber(dur2) && dur2->valueint == 300);
    cJSON *cv = cJSON_GetObjectItem(root, "clock_valid");
    CHECK(cJSON_IsBool(cv) && cJSON_IsFalse(cv));
    cJSON_Delete(root);
    CHECK(strstr(buf, "1970-01-01") == NULL);
}

static void test_tracks_and_heat_optional(void) {
    lc_report_snapshot_t snap;
    snap_init(&snap);
    static char buf[16384];

    CHECK(lc_report_build_v1(&snap, buf, sizeof(buf)) > 0);
    cJSON *root = cJSON_Parse(buf);
    CHECK(root != NULL);
    CHECK(cJSON_GetObjectItem(root, "tracks") == NULL);
    CHECK(cJSON_GetObjectItem(root, "heat_grid") == NULL);
    cJSON_Delete(root);

    lc_track_record_t *rec = (lc_track_record_t *)malloc(LC_TRACK_RECORD_SIZE(2));
    CHECK(rec != NULL);
    memset(rec, 0, LC_TRACK_RECORD_SIZE(2));
    rec->track_id = 7;
    rec->segment_id = 3;
    rec->entered_at_ms = 1000;
    rec->seg_start_ms = 2000;
    rec->seg_end_ms = 3000;
    rec->seg_end_type = LC_SEG_CROSSING;
    rec->events = LC_BIT_IN | LC_BIT_OUT;
    rec->nb_points = 2;
    rec->points[0].x = 0.5f;
    rec->points[0].y = 0.4f;
    rec->points[1].x = 0.5f;
    rec->points[1].y = 0.6f;
    uint32_t ts[2] = { 2500, 2900 };
    memcpy(lc_track_record_point_ts(rec), ts, sizeof(ts));

    snap.tracks_report_enable = 1;
    const lc_track_record_t *tracks[1] = { rec };
    snap.tracks = tracks;
    snap.n_tracks = 1;

    static uint32_t heat[LC_HEAT_GRID_SIZE];
    heat[0] = 5;
    heat[LC_HEAT_GRID_SIZE - 1] = 2;
    snap.heat_grid_enable = 1;
    snap.heat = heat;

    CHECK(lc_report_build_v1(&snap, buf, sizeof(buf)) > 0);
    root = cJSON_Parse(buf);
    CHECK(root != NULL);
    cJSON *tracks_j = cJSON_GetObjectItem(root, "tracks");
    CHECK(cJSON_IsArray(tracks_j));
    cJSON *trk0 = cJSON_GetArrayItem(tracks_j, 0);
    CHECK(trk0 != NULL);
    CHECK(cJSON_GetObjectItem(trk0, "track_id")->valueint == 7);
    CHECK(strcmp(cJSON_GetObjectItem(trk0, "seg_end_type")->valuestring, "crossing") == 0);
    cJSON *evs = cJSON_GetObjectItem(trk0, "events");
    CHECK(cJSON_GetArraySize(evs) == 2);
    cJSON *pts = cJSON_GetObjectItem(trk0, "points");
    CHECK(cJSON_GetArraySize(pts) == 2);
    cJSON *pt0 = cJSON_GetArrayItem(pts, 0);
    CHECK(cJSON_GetArraySize(pt0) == 3);
    CHECK(cJSON_GetArrayItem(pt0, 2)->valueint == 2500);

    cJSON *hg = cJSON_GetObjectItem(root, "heat_grid");
    CHECK(hg != NULL);
    CHECK(cJSON_GetObjectItem(hg, "width")->valueint == 16);
    cJSON *data = cJSON_GetObjectItem(hg, "data");
    CHECK(cJSON_GetArraySize(data) == 256);
    CHECK(cJSON_GetArrayItem(data, 0)->valueint == 5);
    CHECK(cJSON_GetArrayItem(data, 255)->valueint == 2);
    cJSON_Delete(root);
    free(rec);
}

static void test_buffer_too_small_returns_zero(void) {
    lc_report_snapshot_t snap;
    snap_init(&snap);
    char small[64];
    CHECK(lc_report_build_v1(&snap, small, sizeof(small)) == 0);
    char null_buf_arg[4096];
    CHECK(lc_report_build_v1(NULL, null_buf_arg, sizeof(null_buf_arg)) == 0);
    CHECK(lc_report_build_v1(&snap, NULL, 4096) == 0);
}

int main(void) {
    test_schema_v1_fields();
    test_iso8601_and_invalid_clock();
    test_tracks_and_heat_optional();
    test_buffer_too_small_returns_zero();

    if (g_failures) {
        printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all lc report tests passed\n");
    return 0;
}
