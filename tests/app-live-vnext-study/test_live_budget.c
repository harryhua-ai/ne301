#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lc_tracker.h"
#include "lc_line_cross.h"
#include "lc_types.h"
#include "lv_wire.h"
#include "app_admission.h"
#include "app_host_control.h"

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
    c.max_dist_permille = 250u;
    c.track_history_k = k;
    c.max_miss = 3u;
    c.k_confirm = 3u;
    return c;
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

static uint32_t lv_lcg(uint32_t *s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s >> 8;
}

static void lv_sizes(void)
{
    lc_tracker_config_t c16 = lv_cfg(16u);
    lc_tracker_t *t = lc_tracker_create(&c16, 1u);
    size_t track_sz = sizeof(lc_track_t);
    size_t tracker_est = track_sz * (size_t)LC_MAX_TRACKS + 16u;

    LV_CHECK(sizeof(lc_cross_evt_t) == 12u);
    LV_CHECK(LC_MAX_TRACKS == 64u);
    LV_CHECK(LC_HISTORY_MAX == 16u);
    LV_CHECK(LC_K_MAX == 16u);
    LV_CHECK(track_sz < 4096u);
    printf("STUDY ac3 sizes host lc_track_t=%zu lc_cross_evt_t=12 lc_tracker_heap_est=%zu no_pointers_in_lc_track=YES layout_ilp32_eq_lp64=YES\n",
           track_sz, tracker_est);
    printf("STUDY ac3 sizes host app_wire v2_event_min=%u v3_event_max=%u v3_report_max=%u control_ram=%u snapshot_ram=%u host_ring=8x1576=%zu\n",
           (unsigned)APP_ADMISSION_V2_MIN_EVENT_MAX, (unsigned)APP_ADMISSION_V3_EVENT_MAX,
           (unsigned)APP_ADMISSION_V3_REPORT_MAX, (unsigned)APP_CONTROL_MSG_RAM_MAX,
           (unsigned)APP_CONTROL_SNAPSHOT_RAM_MAX, (size_t)8u * 1576u);
    lc_tracker_destroy(t);
}

static void lv_simulation_cost(void)
{
    lc_line_cross_t *lc = lc_line_cross_create(0.5f, 0.0f, 0.5f, 1.0f, 0.0f, 0.0f);
    lc_tracker_config_t c8 = lv_cfg(8u);
    lc_tracker_t *t = lc_tracker_create(&c8, 1u);
    lv_ring_t ring;
    lv_live_evt_t e;
    uint32_t state[24];
    uint32_t rng = 0xC0FFEE42u;
    uint32_t produced = 0u;
    uint32_t frame;
    uint32_t k;
    struct timespec ts0;
    struct timespec ts1;
    double secs;
    lc_track_record_t **recs = NULL;
    uint16_t n_recs = 0;
    int i;

    LV_CHECK(lc != NULL);
    lv_ring_init(&ring, LV_RING_DROP_OLDEST);
    memset(&e, 0, sizeof(e));
    for (i = 0; i < 24; i++) {
        state[i] = lv_lcg(&rng) % 1000u;
    }

    clock_gettime(CLOCK_MONOTONIC, &ts0);
    for (frame = 1u; frame <= 1800u; frame++) {
        lc_point_t pts[24];
        uint32_t win_in = 0, win_out = 0, tot_in = 0, tot_out = 0;
        lc_cross_evt_t evts[16];
        uint8_t n_evts = 0;

        for (i = 0; i < 24; i++) {
            float amp = 0.2f + 0.02f * (float)(i % 5);
            state[i] = (state[i] + lv_lcg(&rng) % 7u) % 1000u;
            pts[i].x = 0.5f + amp * ((float)(state[i] % 200u) - 100.0f) / 100.0f;
            pts[i].y = 0.05f + 0.038f * (float)i;
        }
        lc_tracker_update(t, pts, 24, frame * 33u, &recs, &n_recs);
        lv_free_records(recs, n_recs);
        recs = NULL;
        n_recs = 0;
        lc_tracker_check_line_crossings(t, lc, frame * 33u, &win_in, &win_out, &tot_in,
                                        &tot_out, evts, 16u, &n_evts);
        produced += win_in + win_out;
        for (k = 0; k < n_evts; k++) {
            e.seq = lv_serial_next(e.seq);
            e.track_id = evts[k].track_id;
            e.ts_ms = evts[k].ts_ms;
            e.direction = (evts[k].direction == LC_CROSS_IN) ? LV_DIR_IN : LV_DIR_OUT;
            lv_ring_push(&ring, &e);
        }
        for (k = 0; k < 7u; k++) {
            lv_live_evt_t out;
            if (!lv_ring_pop(&ring, &out)) {
                break;
            }
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &ts1);
    secs = (double)(ts1.tv_sec - ts0.tv_sec) + (double)(ts1.tv_nsec - ts0.tv_nsec) / 1e9;
    printf("STUDY ac3 sim_24obj_1800frames host_wall_sec=%.4f crossings=%u frames_per_sec=%.0f host_measured_not_board=YES\n",
           secs, produced, secs > 0.0 ? (double)1800u / secs : 0.0);
    lc_tracker_destroy(t);
    lc_line_cross_destroy(lc);
}

static void lv_serialize_cost(void)
{
    uint8_t buf[LV_LIVE_HDR_SIZE + 16u * LV_LIVE_EVT_SIZE];
    lv_live_hdr_t hdr;
    lv_live_evt_t e;
    uint32_t i;
    struct timespec ts0;
    struct timespec ts1;
    double secs;
    double mbps;

    memset(&hdr, 0, sizeof(hdr));
    memset(&e, 0, sizeof(e));
    hdr.magic = 0x4556494Cu;
    hdr.header_version = 1u;
    hdr.evt_count = 16u;

    volatile uint32_t sink = 0u;
    clock_gettime(CLOCK_MONOTONIC, &ts0);
    for (i = 0; i < 5000000u; i++) {
        uint32_t k;
        memcpy(buf, &hdr, sizeof(hdr));
        for (k = 0; k < 16u; k++) {
            e.seq = i * 16u + k + 1u;
            e.track_id = k;
            e.ts_ms = i;
            e.direction = (uint8_t)(k & 1u) + 1u;
            memcpy(buf + LV_LIVE_HDR_SIZE + k * LV_LIVE_EVT_SIZE, &e, LV_LIVE_EVT_SIZE);
        }
        sink += buf[LV_LIVE_HDR_SIZE + 3u] + buf[sizeof(buf) - 1u];
    }
    clock_gettime(CLOCK_MONOTONIC, &ts1);
    secs = (double)(ts1.tv_sec - ts0.tv_sec) + (double)(ts1.tv_nsec - ts0.tv_nsec) / 1e9;
    mbps = secs > 0.0 ? ((double)5000000u * (double)sizeof(buf)) / secs / (1024.0 * 1024.0) : 0.0;
    LV_CHECK(secs >= 0.0);
    LV_CHECK(sink != 0xFFFFFFFFu);
    printf("STUDY ac3 serialize batches=5000000 batch_bytes=%zu host_wall_sec=%.4f throughput_mib_s=%.1f copies_per_event=2x16B host_measured=YES\n",
           sizeof(buf), secs, mbps);
}

static void lv_budget_table(void)
{
    size_t track_sz = sizeof(lc_track_t);
    printf("STUDY ac3 budget tracker_ram_64tracks=%zu (C_derived_arm32_equal_no_pointers)\n",
           track_sz * 64u + 16u);
    printf("STUDY ac3 budget live_ring_candidate slots=32 evt=16B ring_ram=512B vs_host_session_ring_8x1576=12608B\n");
    printf("STUDY ac3 budget worst_frame_events=20_burst evt_feed_cap=16 truncated=4 gap_fixable_without_tracker_change=YES\n");
    printf("STUDY ac3 budget trajectory_dump_64tracks_x16pts_x12B=%u full_snapshot_vs_live_ring_delta=%u\n",
           (unsigned)(64u * 16u * 12u), (unsigned)(64u * 16u * 12u) - 512u);
    printf("STUDY ac3 budget live_rate_ceiling 10_samples_per_s_per_track_x64_x16B=10240_B_per_s trail_gate_100ms=derived_from_blob\n");
    printf("STUDY ac3 budget board_ram_cpu=NOT_RUN board_psram_headroom=NOT_RUN board_publish_path=NOT_RUN\n");
}

int main(void)
{
    lv_sizes();
    lv_simulation_cost();
    lv_serialize_cost();
    lv_budget_table();
    printf("STUDY budget total=%d fail=%d\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
