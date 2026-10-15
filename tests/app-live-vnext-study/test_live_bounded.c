#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lc_tracker.h"
#include "lc_line_cross.h"
#include "lc_types.h"
#include "lv_wire.h"

#define LV_DT_MS 33u
#define LV_MAX_DIST_PERMILLE 250u
#define LV_K 8u
#define LV_MAX_MISS 3u
#define LV_K_CONFIRM 3u

static int g_checks;
static int g_fails;

#define LV_CHECK(cond) \
    do { \
        g_checks++; \
        if (!(cond)) { \
            g_fails++; \
            printf("STUDY FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

static lc_tracker_config_t lv_cfg(uint8_t k)
{
    lc_tracker_config_t c;
    c.max_dist_permille = LV_MAX_DIST_PERMILLE;
    c.track_history_k = k;
    c.max_miss = LV_MAX_MISS;
    c.k_confirm = LV_K_CONFIRM;
    return c;
}

static lc_tracker_t *lv_mk(uint8_t k, uint32_t first_id)
{
    lc_tracker_config_t c = lv_cfg(k);
    return lc_tracker_create(&c, first_id);
}

static lc_line_cross_t *lv_line(void)
{
    return lc_line_cross_create(0.5f, 0.0f, 0.5f, 1.0f, 0.0f, 0.0f);
}

static void lv_free_records(lc_track_record_t **recs, uint16_t n)
{
    uint16_t i;
    for (i = 0; i < n; i++) {
        LC_FREE(recs[i]);
    }
    if (recs) {
        LC_FREE(recs);
    }
}

static void lv_visit_count(const lc_track_t *trk, void *user)
{
    (void)trk;
    (*(uint32_t *)user)++;
}

static uint32_t lv_lcg(uint32_t *s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s >> 8;
}

static void lv_e1_crossing_counts(void)
{
    lc_line_cross_t *lc = lv_line();
    lc_tracker_t *t;
    lc_point_t pts[24];
    lc_cross_evt_t evts[16];
    lc_track_record_t **recs = NULL;
    uint16_t n_recs = 0;
    uint8_t n_evts = 0;
    uint32_t win_in = 0, win_out = 0, tot_in = 0, tot_out = 0;
    uint32_t i;

    LV_CHECK(lc != NULL);

    t = lv_mk(LV_K, 1u);
    lc_tracker_update(t, NULL, 0, LV_DT_MS, &recs, &n_recs);
    lv_free_records(recs, n_recs);
    recs = NULL;
    n_recs = 0;
    lc_tracker_check_line_crossings(t, lc, 0u, &win_in, &win_out, &tot_in, &tot_out,
                                    evts, 16u, &n_evts);
    LV_CHECK(n_evts == 0 && win_in == 0 && win_out == 0 && tot_in == 0 && tot_out == 0);
    lc_tracker_destroy(t);
    printf("STUDY ac2 e1_zero_crossings events=0 win=0 total=0\n");

    tot_in = tot_out = 0;
    t = lv_mk(LV_K, 1u);
    pts[0].x = 0.40f;
    pts[0].y = 0.50f;
    lc_tracker_update(t, pts, 1, 33u, &recs, &n_recs);
    lv_free_records(recs, n_recs);
    recs = NULL;
    n_recs = 0;
    pts[0].x = 0.60f;
    lc_tracker_update(t, pts, 1, 66u, &recs, &n_recs);
    lv_free_records(recs, n_recs);
    recs = NULL;
    n_recs = 0;
    lc_tracker_check_line_crossings(t, lc, 66u, &win_in, &win_out, &tot_in, &tot_out,
                                    evts, 16u, &n_evts);
    LV_CHECK(n_evts == 1 && win_in == 1 && tot_in == 1 && win_out == 0 && tot_out == 0);
    LV_CHECK(evts[0].direction == LC_CROSS_IN && evts[0].track_id == 1u &&
             evts[0].ts_ms == 66u);
    lc_tracker_destroy(t);
    printf("STUDY ac2 e1_one_crossing events=1 win_in=1 total_in=1 dir=IN track_id=1 ts=66\n");

    win_in = win_out = 0;
    tot_in = tot_out = 0;
    t = lv_mk(LV_K, 1u);
    for (i = 0; i < 16; i++) {
        pts[i].x = 0.40f;
        pts[i].y = 0.05f + 0.05f * (float)i;
    }
    lc_tracker_update(t, pts, 16, 33u, &recs, &n_recs);
    lv_free_records(recs, n_recs);
    recs = NULL;
    n_recs = 0;
    for (i = 0; i < 16; i++) {
        pts[i].x = 0.60f;
    }
    lc_tracker_update(t, pts, 16, 66u, &recs, &n_recs);
    lv_free_records(recs, n_recs);
    recs = NULL;
    n_recs = 0;
    lc_tracker_check_line_crossings(t, lc, 66u, &win_in, &win_out, &tot_in, &tot_out,
                                    evts, 16u, &n_evts);
    LV_CHECK(n_evts == 16 && win_in == 16 && tot_in == 16);
    lc_tracker_destroy(t);
    printf("STUDY ac2 e1_sixteen_crossing delivered=16 win_in=16 total_in=16 truncated=0\n");

    tot_in = tot_out = 0;
    t = lv_mk(LV_K, 1u);
    for (i = 0; i < 20; i++) {
        pts[i].x = 0.40f;
        pts[i].y = 0.03f + 0.03f * (float)i;
    }
    lc_tracker_update(t, pts, 20, 33u, &recs, &n_recs);
    lv_free_records(recs, n_recs);
    recs = NULL;
    n_recs = 0;
    for (i = 0; i < 20; i++) {
        pts[i].x = 0.60f;
    }
    lc_tracker_update(t, pts, 20, 66u, &recs, &n_recs);
    lv_free_records(recs, n_recs);
    recs = NULL;
    n_recs = 0;
    n_evts = 0;
    win_in = win_out = 0;
    lc_tracker_check_line_crossings(t, lc, 66u, &win_in, &win_out, &tot_in, &tot_out,
                                    evts, 16u, &n_evts);
    LV_CHECK(n_evts == 16);
    LV_CHECK(win_in == 20 && tot_in == 20);
    {
        uint32_t produced = win_in + win_out;
        uint32_t delivered = n_evts;
        uint32_t gap = produced - delivered;
        LV_CHECK(produced == delivered + gap);
        LV_CHECK(gap == 4u);
        printf("STUDY ac2 e1_burst20 produced=%u delivered=%u gap_declared=%u total_in=%u silent_loss_without_wrapper=YES\n",
               produced, delivered, gap, tot_in);
    }
    lc_tracker_destroy(t);

    win_in = win_out = 0;
    tot_in = tot_out = 0;
    t = lv_mk(LV_K, 1u);
    for (i = 0; i < 20; i++) {
        pts[i].x = 0.40f;
        pts[i].y = (i < 10) ? (0.05f + 0.04f * (float)i) : (3.0f + 0.04f * (float)(i - 10));
    }
    lc_tracker_update(t, pts, 20, 33u, &recs, &n_recs);
    lv_free_records(recs, n_recs);
    recs = NULL;
    n_recs = 0;
    for (i = 0; i < 10; i++) {
        pts[i].x = 0.60f;
    }
    lc_tracker_update(t, pts, 10, 66u, &recs, &n_recs);
    lv_free_records(recs, n_recs);
    recs = NULL;
    n_recs = 0;
    win_in = win_out = 0;
    n_evts = 0;
    lc_tracker_check_line_crossings(t, lc, 66u, &win_in, &win_out, &tot_in, &tot_out,
                                    evts, 16u, &n_evts);
    LV_CHECK(win_in == 10 && n_evts == 10);
    for (i = 10; i < 20; i++) {
        pts[i].x = 0.60f;
    }
    lc_tracker_update(t, pts + 10, 10, 99u, &recs, &n_recs);
    lv_free_records(recs, n_recs);
    recs = NULL;
    n_recs = 0;
    win_in = win_out = 0;
    n_evts = 0;
    lc_tracker_check_line_crossings(t, lc, 99u, &win_in, &win_out, &tot_in, &tot_out,
                                    evts, 16u, &n_evts);
    LV_CHECK(win_in == 10 && tot_in == 20 && n_evts == 10);
    printf("STUDY ac2 e1_split_frames total_in=20 delivered=20 truncated=0 burst_is_per_frame=YES\n");
    lc_tracker_destroy(t);
    lc_line_cross_destroy(lc);
}

static void lv_e2_track_lifecycle(void)
{
    lc_line_cross_t *lc = lv_line();
    lc_tracker_t *t = lv_mk(LV_K, 1u);
    lc_point_t p;
    lc_track_record_t **recs = NULL;
    uint16_t n_recs = 0;
    uint32_t carried;
    uint32_t stable = 0u;
    uint32_t ms = 0u;
    int i;

    LV_CHECK(lc != NULL);
    p.x = 0.30f;
    p.y = 0.50f;
    lc_tracker_update(t, &p, 1, ms += LV_DT_MS, &recs, &n_recs);
    LV_CHECK(n_recs == 0 && lc_tracker_active_count(t) == 1);
    stable = 0;
    lc_tracker_for_each_stable(t, lv_visit_count, &stable);
    LV_CHECK(stable == 0u);
    for (i = 0; i < 2; i++) {
        p.x += 0.01f;
        lc_tracker_update(t, &p, 1, ms += LV_DT_MS, &recs, &n_recs);
        lv_free_records(recs, n_recs);
        recs = NULL;
        n_recs = 0;
    }
    stable = 0;
    lc_tracker_for_each_stable(t, lv_visit_count, &stable);
    LV_CHECK(stable == 1u);
    LV_CHECK(lc_tracker_active_count(t) == 1);
    printf("STUDY ac2 e2_confirm stable_after_k_confirm=YES stable=%u\n", stable);

    for (i = 0; i <= (int)LV_MAX_MISS; i++) {
        lc_tracker_update(t, NULL, 0, ms += LV_DT_MS, &recs, &n_recs);
        if (n_recs > 0) {
            LV_CHECK(recs[0]->track_id == 1u);
            LV_CHECK(recs[0]->seg_end_type == LC_SEG_DEPARTED);
            LV_CHECK(recs[0]->nb_points >= 1u);
            lv_free_records(recs, n_recs);
            recs = NULL;
            n_recs = 0;
        }
    }
    LV_CHECK(lc_tracker_active_count(t) == 0);
    carried = lc_tracker_next_id(t);
    LV_CHECK(carried == 2u);
    printf("STUDY ac2 e2_depart archived=1 seg_end=departed points>=1 next_id_carried=%u\n",
           carried);

    {
        lc_tracker_t *t2 = lv_mk(LV_K, carried);
        p.x = 0.40f;
        p.y = 0.50f;
        lc_tracker_update(t2, &p, 1, ms += LV_DT_MS, &recs, &n_recs);
        lv_free_records(recs, n_recs);
        recs = NULL;
        n_recs = 0;
        LV_CHECK(lc_tracker_next_id(t2) == carried + 1u);
        printf("STUDY ac2 e2_rebind next_id_continuity %u->%u ids_monotonic=YES\n",
               carried, lc_tracker_next_id(t2) - 1u);
        lc_tracker_destroy(t2);
    }
    lc_tracker_destroy(t);
    lc_line_cross_destroy(lc);
}

static void lv_e3_capacity_0_to_64(void)
{
    lc_point_t pts[65];
    uint32_t n;
    uint32_t i;
    lc_tracker_t *t;
    lc_line_cross_t *lc = lv_line();
    lc_cross_evt_t evts[16];
    uint8_t n_evts = 0;
    uint32_t win_in, win_out, tot_in, tot_out;
    lc_track_record_t **recs = NULL;
    uint16_t n_recs = 0;
    uint32_t archived_total = 0u;

    LV_CHECK(lc != NULL);
    for (n = 0; n <= 64; n++) {
        t = lv_mk(LV_K, 1u);
        for (i = 0; i < n; i++) {
            pts[i].x = 0.30f;
            pts[i].y = 0.01f + 0.014f * (float)i;
        }
        lc_tracker_update(t, pts, (uint8_t)(n & 0xFFu), LV_DT_MS, &recs, &n_recs);
        LV_CHECK(n_recs == 0);
        lv_free_records(recs, n_recs);
        recs = NULL;
        n_recs = 0;
        LV_CHECK(lc_tracker_active_count(t) == n);
        lc_tracker_destroy(t);
    }
    printf("STUDY ac2 e3_ramp active_count==n for n=0..64 PASS\n");

    t = lv_mk(LV_K, 1u);
    for (i = 0; i < 65; i++) {
        pts[i].x = 0.30f;
        pts[i].y = 0.01f + 0.014f * (float)i;
    }
    lc_tracker_update(t, pts, 65, LV_DT_MS, &recs, &n_recs);
    lv_free_records(recs, n_recs);
    recs = NULL;
    n_recs = 0;
    LV_CHECK(lc_tracker_active_count(t) == 64);
    lc_tracker_update(t, pts, 65, 2u * LV_DT_MS, &recs, &n_recs);
    lv_free_records(recs, n_recs);
    recs = NULL;
    n_recs = 0;
    LV_CHECK(lc_tracker_active_count(t) == 64);
    win_in = win_out = tot_in = tot_out = 0;
    lc_tracker_check_line_crossings(t, lc, 2u * LV_DT_MS, &win_in, &win_out, &tot_in,
                                    &tot_out, evts, 16u, &n_evts);
    LV_CHECK(n_evts == 0 && tot_in == 0 && tot_out == 0);
    printf("STUDY ac2 e3_overflow input_clamped_to=64 active=64 missing_object_unreported=YES visible_counter=NONE\n");

    for (i = 0; i <= LV_MAX_MISS; i++) {
        lc_tracker_update(t, NULL, 0, (uint32_t)(3u + (uint32_t)i) * LV_DT_MS, &recs, &n_recs);
        archived_total += n_recs;
        lv_free_records(recs, n_recs);
        recs = NULL;
        n_recs = 0;
    }
    LV_CHECK(archived_total == 64u);
    LV_CHECK(lc_tracker_active_count(t) == 0);
    LV_CHECK(lc_tracker_next_id(t) == 65u);
    printf("STUDY ac2 e3_decay active=0 archived=%u next_id=65\n", archived_total);
    lc_tracker_destroy(t);
    lc_line_cross_destroy(lc);
}

static void lv_e4_history_bound(void)
{
    lc_tracker_t *t = lv_mk(16u, 1u);
    lc_point_t p;
    float dir = 0.05f;
    uint32_t ms = 0u;
    int i;
    lc_track_record_t **recs = NULL;
    uint16_t n_recs = 0;

    p.x = 0.10f;
    p.y = 0.50f;
    for (i = 0; i < 200; i++) {
        lc_tracker_update(t, &p, 1, ms += LV_DT_MS, &recs, &n_recs);
        LV_CHECK(n_recs == 0);
        lv_free_records(recs, n_recs);
        recs = NULL;
        n_recs = 0;
        p.x += dir;
        if (p.x > 0.9f || p.x < 0.1f) {
            dir = -dir;
            p.x += 2.0f * dir;
        }
    }
    lc_tracker_window_snapshot(t, ms, &recs, &n_recs);
    LV_CHECK(n_recs == 1);
    LV_CHECK(recs[0]->nb_points >= 1u && recs[0]->nb_points <= 16u);
    printf("STUDY ac2 e4_history frames=200 k=16 history_capped=YES snapshot_points=%u bound=16\n",
           recs[0]->nb_points);
    lv_free_records(recs, n_recs);
    lc_tracker_destroy(t);
}

static void lv_e5_timestamp_wrap(void)
{
    lc_line_cross_t *lc = lv_line();
    lc_tracker_t *t = lv_mk(16u, 1u);
    lc_point_t p;
    lc_cross_evt_t evts[16];
    uint8_t n_evts = 0;
    uint32_t win_in = 0, win_out = 0, tot_in = 0, tot_out = 0;
    lc_track_record_t **recs = NULL;
    uint16_t n_recs = 0;
    uint32_t t0 = 0xFFFFFFF0u;
    uint32_t t1 = 0xFFFFFFC0u;
    uint32_t t2 = 20u;
    uint32_t t3 = 60u;

    LV_CHECK(lc != NULL);
    p.x = 0.40f;
    p.y = 0.50f;
    lc_tracker_update(t, &p, 1, t0, &recs, &n_recs);
    lv_free_records(recs, n_recs);
    recs = NULL;
    n_recs = 0;
    p.x = 0.45f;
    lc_tracker_update(t, &p, 1, t1, &recs, &n_recs);
    lv_free_records(recs, n_recs);
    recs = NULL;
    n_recs = 0;
    p.x = 0.60f;
    lc_tracker_update(t, &p, 1, t2, &recs, &n_recs);
    lv_free_records(recs, n_recs);
    recs = NULL;
    n_recs = 0;
    lc_tracker_check_line_crossings(t, lc, t2, &win_in, &win_out, &tot_in, &tot_out,
                                    evts, 16u, &n_evts);
    LV_CHECK(n_evts == 1 && tot_in == 1 && evts[0].ts_ms == t2);
    printf("STUDY ac2 e5_wrap_crossing crossing_across_u32_wrap=YES ts %u->%u events=1\n",
           t1, t2);

    lc_tracker_window_snapshot(t, t3, &recs, &n_recs);
    LV_CHECK(n_recs == 1);
    LV_CHECK(recs[0]->nb_points == 0u);
    printf("STUDY ac2 e5_wrap_snapshot snapshot_points=0 pre_wrap_points_lost=YES from=%u to=%u\n",
           t0, t3);
    lv_free_records(recs, n_recs);
    recs = NULL;
    n_recs = 0;

    p.x = 0.40f;
    lc_tracker_update(t, &p, 1, 100u, &recs, &n_recs);
    lv_free_records(recs, n_recs);
    recs = NULL;
    n_recs = 0;
    win_in = win_out = 0;
    n_evts = 0;
    tot_in = tot_out = 0;
    lc_tracker_check_line_crossings(t, lc, 100u, &win_in, &win_out, &tot_in, &tot_out,
                                    evts, 16u, &n_evts);
    LV_CHECK(n_evts == 1 && win_out == 1);
    p.x = 0.60f;
    lc_tracker_update(t, &p, 1, 133u, &recs, &n_recs);
    lv_free_records(recs, n_recs);
    recs = NULL;
    n_recs = 0;
    win_in = win_out = 0;
    n_evts = 0;
    lc_tracker_check_line_crossings(t, lc, 133u, &win_in, &win_out, &tot_in, &tot_out,
                                    evts, 16u, &n_evts);
    LV_CHECK(n_evts == 1 && win_in == 1);
    printf("STUDY ac2 e5_wrap_post continuing_ops=YES out_then_in=YES direction_latch_survives_wrap=YES events=2\n");

    lc_tracker_destroy(t);
    lc_line_cross_destroy(lc);

    LV_CHECK(lc_serial_newer(5u, 0xFFFFFFFFu) == 1u);
    LV_CHECK(lc_serial_newer(0xFFFFFFFFu, 5u) == 0u);
    LV_CHECK(lc_serial_newer(7u, 7u) == 0u);
    LV_CHECK(lc_serial_newer(1u, 0x80000000u) == 0u);
    printf("STUDY ac2 e5_serial_newer wrap_safe_cursor_comparison=YES\n");
}

static void lv_e6_queue_policies(void)
{
    lv_ring_t ring;
    lv_live_evt_t e;
    uint32_t frame;
    uint32_t k;
    uint32_t seq = 0u;

    memset(&e, 0, sizeof(e));
    lv_ring_init(&ring, LV_RING_DROP_OLDEST);
    for (frame = 0; frame < 50u; frame++) {
        for (k = 0; k < 100u; k++) {
            e.seq = seq = lv_serial_next(seq);
            lv_ring_push(&ring, &e);
        }
        for (k = 0; k < 10u; k++) {
            lv_live_evt_t out;
            if (!lv_ring_pop_private(&ring, &out)) {
                break;
            }
        }
    }
    LV_CHECK(ring.produced == 5000u);
    LV_CHECK(ring.produced == ring.delivered + ring.dropped + ring.count);
    LV_CHECK(ring.gap_declared == ring.dropped);
    printf("STUDY ac2 e6_drop_oldest_producer_accounting produced=%u delivered=%u dropped=%u in_ring=%u producer_gap=%u consumer_visibility=NOT_TESTED\n",
           ring.produced, ring.delivered, ring.dropped, ring.count, ring.gap_declared);

    lv_ring_init(&ring, LV_RING_DROP_NEWEST);
    seq = 0u;
    for (frame = 0; frame < 50u; frame++) {
        for (k = 0; k < 100u; k++) {
            e.seq = seq = lv_serial_next(seq);
            lv_ring_push(&ring, &e);
        }
        for (k = 0; k < 10u; k++) {
            lv_live_evt_t out;
            if (!lv_ring_pop_private(&ring, &out)) {
                break;
            }
        }
    }
    LV_CHECK(ring.produced == 5000u);
    LV_CHECK(ring.produced == ring.delivered + ring.dropped + ring.count);
    LV_CHECK(ring.gap_declared == ring.dropped);
    printf("STUDY ac2 e6_drop_newest_producer_accounting produced=%u delivered=%u dropped=%u in_ring=%u producer_gap=%u consumer_visibility=NOT_TESTED\n",
           ring.produced, ring.delivered, ring.dropped, ring.count, ring.gap_declared);
}

typedef struct {
    lv_live_evt_t e;
    uint32_t carried;
} lv_host_slot_t;

static void lv_e6_hoststyle(void)
{
    lv_host_slot_t slots[LV_HOSTSTYLE_MODEL_SLOTS];
    uint32_t head = 0u, count = 0u;
    uint32_t produced = 0u, delivered = 0u, delivered_gap = 0u, pending_lost = 0u;
    uint32_t gapped_deliveries = 0u;
    uint32_t seq = 0u;
    uint32_t frame;
    uint32_t k;

    for (frame = 0; frame < 50u; frame++) {
        uint32_t push_n = (frame % 2u == 0u) ? 5u : 3u;
        for (k = 0; k < push_n; k++) {
            produced++;
            if (count >= LV_HOSTSTYLE_MODEL_SLOTS) {
                pending_lost += 1u + slots[head].carried;
                head = (head + 1u) % LV_HOSTSTYLE_MODEL_SLOTS;
                count--;
            }
            slots[(head + count) % LV_HOSTSTYLE_MODEL_SLOTS].e.seq =
                seq = lv_serial_next(seq);
            slots[(head + count) % LV_HOSTSTYLE_MODEL_SLOTS].carried = pending_lost;
            pending_lost = 0u;
            count++;
        }
        for (k = 0; k < 2u; k++) {
            if (count == 0u) {
                break;
            }
            delivered++;
            delivered_gap += slots[head].carried;
            if (slots[head].carried > 0u) {
                gapped_deliveries++;
            }
            head = (head + 1u) % LV_HOSTSTYLE_MODEL_SLOTS;
            count--;
        }
    }
    while (count > 0u) {
        delivered++;
        delivered_gap += slots[head].carried;
        if (slots[head].carried > 0u) {
            gapped_deliveries++;
        }
        head = (head + 1u) % LV_HOSTSTYLE_MODEL_SLOTS;
        count--;
    }
    LV_CHECK(produced == delivered + delivered_gap + pending_lost);
    LV_CHECK(gapped_deliveries > 0u);
    printf("STUDY ac2 e6_hoststyle_local_model produced=%u delivered=%u delivered_gap=%u gapped_deliveries=%u pending_lost=%u accounting_matches_local_model=YES candidate_wire=NOT_TESTED\n",
           produced, delivered, delivered_gap, gapped_deliveries, pending_lost);
}

static void lv_e7_feed_total_consistency(void)
{
    lc_line_cross_t *lc = lv_line();
    lc_tracker_t *t = lv_mk(LV_K, 1u);
    lv_ring_t ring;
    lv_live_evt_t e;
    uint32_t seq = 0u;
    uint32_t last_delivered_seq = 0u;
    uint32_t produced_total = 0u;
    uint32_t truncated_total = 0u;
    uint32_t frame;
    uint32_t state[24];
    uint32_t rng = 0x12345678u;
    uint32_t rebinds = 0u;
    lc_track_record_t **recs = NULL;
    uint16_t n_recs = 0;
    int i;

    LV_CHECK(lc != NULL);
    lv_ring_init(&ring, LV_RING_DROP_OLDEST);
    memset(&e, 0, sizeof(e));
    for (i = 0; i < 24; i++) {
        state[i] = lv_lcg(&rng) % 1000u;
    }

    for (frame = 1u; frame <= 200u; frame++) {
        lc_point_t pts[24];
        uint32_t win_in = 0, win_out = 0, tot_in = 0, tot_out = 0;
        lc_cross_evt_t evts[16];
        uint8_t n_evts = 0;
        uint32_t produced_frame;
        uint32_t k;

        for (i = 0; i < 24; i++) {
            float amp = 0.2f + 0.02f * (float)(i % 5);
            state[i] = (state[i] + lv_lcg(&rng) % 7u) % 1000u;
            pts[i].x = 0.5f + amp * ((float)(state[i] % 200u) - 100.0f) / 100.0f;
            pts[i].y = 0.05f + 0.038f * (float)i;
        }

        if (frame == 100u) {
            uint32_t carry = lc_tracker_next_id(t);
            lc_tracker_destroy(t);
            t = lv_mk(LV_K, carry);
            rebinds++;
        }

        lc_tracker_update(t, pts, 24, frame * LV_DT_MS, &recs, &n_recs);
        lv_free_records(recs, n_recs);
        recs = NULL;
        n_recs = 0;
        lc_tracker_check_line_crossings(t, lc, frame * LV_DT_MS, &win_in, &win_out,
                                        &tot_in, &tot_out, evts, 16u, &n_evts);
        produced_frame = win_in + win_out;
        produced_total += produced_frame;
        truncated_total += produced_frame - n_evts;
        for (k = 0; k < n_evts; k++) {
            e.seq = seq = lv_serial_next(seq);
            e.track_id = evts[k].track_id;
            e.ts_ms = evts[k].ts_ms;
            e.direction = (evts[k].direction == LC_CROSS_IN) ? LV_DIR_IN : LV_DIR_OUT;
            lv_ring_push(&ring, &e);
        }
        for (k = 0; k < 7u; k++) {
            lv_live_evt_t out;
            if (!lv_ring_pop_private(&ring, &out)) {
                break;
            }
            LV_CHECK(out.seq == last_delivered_seq + 1u || last_delivered_seq == 0u);
            last_delivered_seq = out.seq;
        }
        LV_CHECK(produced_total ==
                 ring.delivered + ring.dropped + ring.count + truncated_total);
    }

    LV_CHECK(produced_total ==
             ring.delivered + ring.dropped + ring.count + truncated_total);
    LV_CHECK(produced_total > 0u);
    LV_CHECK(rebinds == 1u);
    printf("STUDY ac2 e7_producer_harness frames=200 produced=%u delivered=%u ring_dropped=%u in_ring=%u per_frame_truncated=%u rebinds=%u producer_identity=Held consumer_gap_visibility=NOT_TESTED\n",
           produced_total, ring.delivered, ring.dropped, ring.count, truncated_total,
           rebinds);
    printf("STUDY ac2 e7_producer_harness_consistency private_counters_only=YES consumer_reconstruction=NOT_TESTED\n");

    lc_tracker_destroy(t);
    lc_line_cross_destroy(lc);
}

typedef struct {
    uint32_t received;
    uint32_t last_seq;
    uint32_t last_total;
    uint32_t last_gap;
    uint32_t session_generation;
    uint32_t rebind_marked_events;
    uint8_t has_last_seq;
    uint8_t stale;
    uint8_t gapped;
} lv_receiver_probe_t;

static void lv_receiver_accept(lv_receiver_probe_t *receiver,
                               const lv_read_batch_t *batch)
{
    uint32_t i;
    LV_CHECK(batch->gap.declared_gap == batch->header.cumulative_gap);
    LV_CHECK(batch->gap.seq == batch->header.through_seq);
    if ((batch->header.flags & LV_READ_FLAG_STALE) != 0u) {
        receiver->stale = 1u;
    }
    if ((batch->header.flags & LV_READ_FLAG_GAPPED) != 0u) {
        receiver->stale = 1u;
        receiver->gapped = 1u;
    }
    if (receiver->session_generation != 0u &&
        receiver->session_generation != batch->header.session_generation) {
        receiver->stale = 1u;
    }
    for (i = 0u; i < batch->header.evt_count; i++) {
        const lv_live_evt_t *evt = &batch->events[i];
        if (receiver->has_last_seq) {
            LV_CHECK(lv_serial_newer(evt->seq, receiver->last_seq));
            if (evt->seq != lv_serial_next(receiver->last_seq)) {
                receiver->stale = 1u;
                receiver->gapped = 1u;
            }
        }
        if ((evt->flags & LV_FLAG_GAP_BEFORE) != 0u) {
            receiver->stale = 1u;
            receiver->gapped = 1u;
        }
        if ((evt->flags & LV_FLAG_REBIND_BEFORE) != 0u) {
            receiver->stale = 1u;
            receiver->rebind_marked_events++;
        }
        receiver->received++;
        receiver->last_seq = evt->seq;
        receiver->has_last_seq = 1u;
    }
    if (batch->gap.declared_gap > 0u) {
        receiver->stale = 1u;
        receiver->gapped = 1u;
    }
    receiver->last_total = batch->header.cumulative_total;
    receiver->last_gap = batch->gap.declared_gap;
    receiver->session_generation = batch->header.session_generation;
}

static void lv_receiver_read_all(lv_ring_t *ring, lv_receiver_probe_t *receiver,
                                 uint32_t max_events, lv_read_batch_t *last)
{
    for (;;) {
        lv_read_batch_t batch;
        lv_ring_read(ring, &batch, max_events);
        lv_receiver_accept(receiver, &batch);
        *last = batch;
        if (batch.header.evt_count == 0u) {
            return;
        }
    }
}

static void lv_publish_count(lv_ring_t *ring, uint32_t count)
{
    while (count > 0u) {
        lv_live_evt_t events[LV_LIVE_BATCH_MAX];
        uint32_t chunk = count > LV_LIVE_BATCH_MAX ? LV_LIVE_BATCH_MAX : count;
        uint32_t i;
        memset(events, 0, sizeof(events));
        for (i = 0u; i < chunk; i++) {
            events[i].track_id = i + 1u;
            events[i].ts_ms = i;
        }
        LV_CHECK(lv_ring_publish_frame(ring, events, chunk, chunk));
        count -= chunk;
    }
}

static void lv_e8_consumer_visible_gaps(void)
{
    lv_ring_t ring;
    lv_receiver_probe_t receiver;
    lv_read_batch_t last;
    lv_live_evt_t events[LV_LIVE_BATCH_MAX];
    uint32_t i;

    memset(&receiver, 0, sizeof(receiver));
    memset(events, 0, sizeof(events));
    lv_ring_init(&ring, LV_RING_DROP_OLDEST);
    for (i = 0u; i < LV_LIVE_BATCH_MAX; i++) {
        events[i].track_id = i + 1u;
        events[i].ts_ms = i;
    }
    LV_CHECK(lv_ring_publish_frame(&ring, events, LV_LIVE_BATCH_MAX, 20u));
    lv_receiver_read_all(&ring, &receiver, LV_LIVE_BATCH_MAX, &last);
    LV_CHECK(receiver.received == 16u);
    LV_CHECK(receiver.last_gap == 4u);
    LV_CHECK(receiver.received + receiver.last_gap == receiver.last_total);
    LV_CHECK(receiver.stale && receiver.gapped);
    LV_CHECK(last.header.evt_count == 0u);
    LV_CHECK((last.header.flags & LV_READ_FLAG_GAPPED) != 0u);
    LV_CHECK((last.gap.kind & LV_GAP_KIND_SOURCE_TRUNCATION) != 0u);
    printf("STUDY ac2 e8_consumer_frame_truncation received=%u gap=%u total=%u stale_gapped=%u\n",
           receiver.received, receiver.last_gap, receiver.last_total, receiver.stale);

    memset(&receiver, 0, sizeof(receiver));
    lv_ring_init(&ring, LV_RING_DROP_OLDEST);
    lv_publish_count(&ring, 40u);
    lv_receiver_read_all(&ring, &receiver, LV_LIVE_BATCH_MAX, &last);
    LV_CHECK(receiver.received == 32u);
    LV_CHECK(receiver.last_gap == 8u);
    LV_CHECK(receiver.received + receiver.last_gap == receiver.last_total);
    LV_CHECK(receiver.stale && receiver.gapped);
    LV_CHECK((last.gap.kind & LV_GAP_KIND_RING_DROP) != 0u);
    printf("STUDY ac2 e8_consumer_drop_oldest received=%u gap=%u total=%u stale_gapped=%u\n",
           receiver.received, receiver.last_gap, receiver.last_total, receiver.stale);

    memset(&receiver, 0, sizeof(receiver));
    lv_ring_init(&ring, LV_RING_DROP_NEWEST);
    lv_publish_count(&ring, 40u);
    lv_receiver_read_all(&ring, &receiver, LV_LIVE_BATCH_MAX, &last);
    LV_CHECK(receiver.received == 32u);
    LV_CHECK(receiver.last_gap == 8u);
    LV_CHECK(receiver.received + receiver.last_gap == receiver.last_total);
    LV_CHECK(receiver.stale && receiver.gapped);
    LV_CHECK(last.header.evt_count == 0u);
    LV_CHECK(last.header.through_seq == 40u);
    LV_CHECK((last.gap.kind & LV_GAP_KIND_RING_DROP) != 0u);
    printf("STUDY ac2 e8_consumer_drop_newest received=%u gap=%u total=%u through=%u stale_gapped=%u\n",
           receiver.received, receiver.last_gap, receiver.last_total,
           last.header.through_seq, receiver.stale);

    memset(&receiver, 0, sizeof(receiver));
    lv_ring_init_at(&ring, LV_RING_DROP_OLDEST, 7u, 1u, 0xFFFFFFFCu);
    lv_ring_read(&ring, &last, 8u);
    lv_receiver_accept(&receiver, &last);
    LV_CHECK(last.header.evt_count == 0u);
    LV_CHECK((last.header.flags & LV_READ_FLAG_STALE) == 0u);
    lv_publish_count(&ring, 16u);
    lv_receiver_read_all(&ring, &receiver, 8u, &last);
    LV_CHECK(receiver.received == 16u);
    LV_CHECK(receiver.last_gap == 0u);
    lv_publish_count(&ring, 16u);
    lv_ring_rebind(&ring);
    lv_ring_read(&ring, &last, 8u);
    lv_receiver_accept(&receiver, &last);
    LV_CHECK(last.header.evt_count == 0u);
    LV_CHECK(last.header.session_generation == 2u);
    LV_CHECK(last.header.through_seq == 29u);
    LV_CHECK(last.gap.declared_gap == 16u);
    LV_CHECK((last.gap.kind & LV_GAP_KIND_REBIND) != 0u);
    LV_CHECK(receiver.stale && receiver.gapped);
    lv_publish_count(&ring, 8u);
    lv_receiver_read_all(&ring, &receiver, 8u, &last);
    LV_CHECK(receiver.received == 24u);
    LV_CHECK(receiver.last_gap == 16u);
    LV_CHECK(receiver.received + receiver.last_gap == receiver.last_total);
    LV_CHECK(receiver.rebind_marked_events == 1u);
    LV_CHECK(receiver.stale && receiver.gapped);
    printf("STUDY ac2 e8_consumer_poll_drain_rollover_rebind received=%u gap=%u total=%u through=%u session=%u stale_gapped=%u\n",
           receiver.received, receiver.last_gap, receiver.last_total,
           last.header.through_seq, last.header.session_generation,
           receiver.stale);
}

int main(void)
{
    lv_e1_crossing_counts();
    lv_e2_track_lifecycle();
    lv_e3_capacity_0_to_64();
    lv_e4_history_bound();
    lv_e5_timestamp_wrap();
    lv_e6_queue_policies();
    lv_e6_hoststyle();
    lv_e7_feed_total_consistency();
    lv_e8_consumer_visible_gaps();
    printf("STUDY bounded total=%d fail=%d\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
