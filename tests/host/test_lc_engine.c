#include <stdio.h>
#include <string.h>
#include "lc_tracker.h"
#include "lc_line_cross.h"

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
    uint32_t ids[LC_MAX_TRACKS];
    lc_point_t pos[LC_MAX_TRACKS];
    uint8_t age[LC_MAX_TRACKS];
    uint16_t count;
} track_snapshot_t;

static void snapshot_visitor(const lc_track_t *trk, void *user)
{
    track_snapshot_t *snap = (track_snapshot_t *)user;
    if (snap->count < LC_MAX_TRACKS) {
        snap->ids[snap->count] = trk->id;
        snap->pos[snap->count] = trk->last_pos;
        snap->age[snap->count] = trk->age;
        snap->count++;
    }
}

static void snapshot_tracks(const lc_tracker_t *t, track_snapshot_t *snap)
{
    memset(snap, 0, sizeof(*snap));
    lc_tracker_for_each_stable(t, snapshot_visitor, snap);
}

static void free_records(lc_track_record_t **records, uint16_t count)
{
    for (uint16_t i = 0; i < count; ++i) {
        LC_FREE(records[i]);
    }
    LC_FREE(records);
}

static void test_deterministic_greedy_matching(void)
{
    lc_tracker_t *t = lc_tracker_create(&(lc_tracker_config_t){150, 4, 2, 1}, 1);
    CHECK(t != NULL);

    lc_point_t detects[2] = { {0.10f, 0.10f}, {0.90f, 0.90f} };
    lc_tracker_update(t, detects, 2, 100, NULL, NULL);
    CHECK(lc_tracker_active_count(t) == 2);

    lc_tracker_update(t, detects, 2, 200, NULL, NULL);

    track_snapshot_t snap;
    snapshot_tracks(t, &snap);
    CHECK(snap.count == 2);
    int near_ok = 0, far_ok = 0;
    for (uint16_t i = 0; i < snap.count; ++i) {
        if (snap.pos[i].x < 0.5f) {
            near_ok = (snap.pos[i].x > 0.05f && snap.pos[i].x < 0.15f);
        } else {
            far_ok = (snap.pos[i].x > 0.85f && snap.pos[i].x < 0.95f);
        }
    }
    CHECK(near_ok);
    CHECK(far_ok);

    lc_tracker_destroy(t);
}

static void test_alternating_in_out_anti_bounce(void)
{
    lc_tracker_t *t = lc_tracker_create(&(lc_tracker_config_t){500, 4, 2, 1}, 1);
    lc_line_cross_t *lc = lc_line_cross_create(0.5f, 0.0f, 0.5f, 1.0f, 0.0f, 0.5f);
    CHECK(t != NULL);
    CHECK(lc != NULL);

    lc_point_t outside = {0.3f, 0.5f};
    lc_tracker_update(t, &outside, 1, 100, NULL, NULL);

    uint32_t win_in = 0, win_out = 0, tot_in = 0, tot_out = 0;
    lc_point_t p;
    uint32_t ts = 200;

    p.x = 0.7f; p.y = 0.5f;
    lc_tracker_update(t, &p, 1, ts, NULL, NULL); ts += 100;
    lc_tracker_check_line_crossings(t, lc, ts, &win_in, &win_out, &tot_in, &tot_out);
    CHECK(win_in == 1);
    CHECK(win_out == 0);

    p.x = 0.3f;
    lc_tracker_update(t, &p, 1, ts, NULL, NULL); ts += 100;
    lc_tracker_check_line_crossings(t, lc, ts, &win_in, &win_out, &tot_in, &tot_out);
    CHECK(win_in == 1);
    CHECK(win_out == 1);

    p.x = 0.7f;
    lc_tracker_update(t, &p, 1, ts, NULL, NULL); ts += 100;
    lc_tracker_check_line_crossings(t, lc, ts, &win_in, &win_out, &tot_in, &tot_out);
    CHECK(win_in == 2);
    CHECK(win_out == 1);
    CHECK(tot_in == 2);
    CHECK(tot_out == 1);

    p.x = 0.71f;
    lc_tracker_update(t, &p, 1, ts, NULL, NULL); ts += 100;
    win_in = 0; win_out = 0;
    lc_tracker_check_line_crossings(t, lc, ts, &win_in, &win_out, &tot_in, &tot_out);
    CHECK(win_in == 0);
    CHECK(win_out == 0);

    lc_line_cross_destroy(lc);
    lc_tracker_destroy(t);
}

static void test_zero_detection_retirement(void)
{
    lc_tracker_t *t = lc_tracker_create(&(lc_tracker_config_t){150, 4, 2, 1}, 1);

    lc_point_t detect = {0.5f, 0.5f};
    lc_tracker_update(t, &detect, 1, 100, NULL, NULL);
    CHECK(lc_tracker_active_count(t) == 1);

    lc_point_t none[1];
    uint32_t ts = 200;
    lc_track_record_t **records = NULL;
    uint16_t n_records = 0;

    lc_tracker_update(t, none, 0, ts, &records, &n_records); ts += 100;
    CHECK(records == NULL);
    CHECK(n_records == 0);
    CHECK(lc_tracker_active_count(t) == 1);
    if (records) free_records(records, n_records);

    lc_tracker_update(t, none, 0, ts, &records, &n_records); ts += 100;
    CHECK(lc_tracker_active_count(t) == 1);
    if (records) free_records(records, n_records);

    lc_tracker_update(t, none, 0, ts, &records, &n_records); ts += 100;
    CHECK(records != NULL);
    CHECK(n_records == 1);
    CHECK(records[0] != NULL);
    CHECK(records[0]->track_id == 1);
    CHECK(records[0]->seg_end_type == LC_SEG_DEPARTED);
    CHECK(lc_tracker_active_count(t) == 0);
    free_records(records, n_records);

    lc_tracker_destroy(t);
}

static void test_age_saturates_at_uint8_max(void)
{
    lc_tracker_t *t = lc_tracker_create(&(lc_tracker_config_t){150, 4, 2, 1}, 1);

    lc_point_t detect = {0.5f, 0.5f};
    uint32_t ts = 100;
    for (int i = 0; i < 300; ++i) {
        lc_tracker_update(t, &detect, 1, ts, NULL, NULL);
        ts += 100;
    }

    track_snapshot_t snap;
    snapshot_tracks(t, &snap);
    CHECK(snap.count == 1);
    CHECK(snap.age[0] == UINT8_MAX);
    CHECK(lc_tracker_active_count(t) == 1);

    lc_tracker_destroy(t);
}

static void test_record_growth_preserves_content(void)
{
    lc_tracker_t *t = lc_tracker_create(&(lc_tracker_config_t){150, 4, 2, 1}, 1);

    lc_point_t detects[3] = { {0.1f, 0.1f}, {0.5f, 0.5f}, {0.9f, 0.9f} };
    lc_tracker_update(t, detects, 3, 100, NULL, NULL);
    CHECK(lc_tracker_active_count(t) == 3);

    lc_point_t none[1];
    uint32_t ts = 200;
    lc_track_record_t **records = NULL;
    uint16_t n_records = 0;

    int fired = 0;
    for (int i = 0; i < 4; ++i) {
        lc_tracker_update(t, none, 0, ts, &records, &n_records);
        ts += 100;
        if (records) {
            fired = 1;
            CHECK(n_records == 3);
            CHECK(records[0] != NULL);
            CHECK(records[1] != NULL);
            CHECK(records[2] != NULL);
            CHECK(records[0]->track_id == 1);
            CHECK(records[1]->track_id == 2);
            CHECK(records[2]->track_id == 3);
            CHECK(records[0]->seg_end_type == LC_SEG_DEPARTED);
            free_records(records, n_records);
        }
        records = NULL;
        n_records = 0;
    }
    CHECK(fired == 1);
    CHECK(lc_tracker_active_count(t) == 0);

    lc_tracker_destroy(t);
}

static void test_line_cross_edge_cases(void)
{
    CHECK(lc_line_cross_create(0.5f, 0.5f, 0.5f, 0.5f, 0.0f, 0.0f) == NULL);

    lc_line_cross_t *lc = lc_line_cross_create(0.5f, 0.0f, 0.5f, 1.0f, 0.8f, 0.5f);
    CHECK(lc != NULL);

    lc_tracker_t *t = lc_tracker_create(&(lc_tracker_config_t){700, 4, 2, 1}, 1);
    lc_point_t inside = {0.2f, 0.5f};
    lc_tracker_update(t, &inside, 1, 100, NULL, NULL);
    lc_point_t outside = {0.8f, 0.5f};
    lc_tracker_update(t, &outside, 1, 200, NULL, NULL);

    uint32_t win_in = 0, win_out = 0, tot_in = 0, tot_out = 0;
    lc_tracker_check_line_crossings(t, lc, 300, &win_in, &win_out, &tot_in, &tot_out);
    CHECK(win_in == 0);
    CHECK(win_out == 1);
    CHECK(tot_out == 1);

    lc_line_cross_destroy(lc);
    lc_tracker_destroy(t);
}

static void test_snapshot_window_keeps_tracks(void)
{
    lc_tracker_t *t = lc_tracker_create(&(lc_tracker_config_t){150, 4, 2, 1}, 1);

    lc_point_t detect = {0.5f, 0.5f};
    lc_tracker_update(t, &detect, 1, 100, NULL, NULL);

    lc_track_record_t **records = NULL;
    uint16_t n_records = 0;
    lc_tracker_window_snapshot(t, 200, &records, &n_records);
    CHECK(records != NULL);
    CHECK(n_records == 1);
    CHECK(records[0]->seg_end_type == LC_SEG_CROSSING);
    free_records(records, n_records);
    CHECK(lc_tracker_active_count(t) == 1);

    lc_tracker_destroy(t);
}

int main(void)
{
    test_deterministic_greedy_matching();
    test_alternating_in_out_anti_bounce();
    test_zero_detection_retirement();
    test_age_saturates_at_uint8_max();
    test_record_growth_preserves_content();
    test_line_cross_edge_cases();
    test_snapshot_window_keeps_tracks();

    if (g_failures != 0) {
        printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all lc engine tests passed\n");
    return 0;
}
