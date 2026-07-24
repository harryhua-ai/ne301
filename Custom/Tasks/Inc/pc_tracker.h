/* Custom/Tasks/Inc/pc_tracker.h */
#ifndef PC_TRACKER_H
#define PC_TRACKER_H

#include "pc_types.h"
#include "pc_line_cross.h"

struct pc_track_t {
    uint32_t   id;
    pc_point_t history[PC_K_MAX];
    uint32_t   history_ts[PC_K_MAX];
    uint8_t    history_head;       /* next write slot, in [0, k) */
    uint8_t    history_used;       /* min(age, k) */
    uint8_t    age;
    uint8_t    miss_count;
    int8_t     last_side;          /* -1 / +1 / 0 = uninit */
    uint8_t    counted_dir;        /* anti-bounce state bitmap (PC_BIT_IN/OUT, mutates per §3.2) */
    uint8_t    segment_events;     /* per-segment event accumulator (OR-only; cleared on each CROSSING snapshot) — source for rec->events, distinct from counted_dir per spec §3.2/§3.4 */
    uint32_t   entered_at_ms;
    uint32_t   last_match_ts;
    uint32_t   last_report_ts;
    uint32_t   segment_id;
    pc_point_t last_pos;
};

typedef struct pc_tracker_config_t {
    uint16_t max_dist_permille;    /* e.g. 150 = 0.15 */
    uint8_t  track_history_k;      /* effective k, clamped to [4, PC_K_MAX] */
    uint8_t  max_miss;
    uint8_t  k_confirm;
} pc_tracker_config_t;

typedef struct pc_tracker pc_tracker_t;   /* opaque */

pc_tracker_t* pc_tracker_create(const pc_tracker_config_t* cfg, uint32_t initial_next_id);
void          pc_tracker_destroy(pc_tracker_t* t);
/* Returns the next track ID that will be assigned (preserved across rebuilds). */
uint32_t      pc_tracker_next_id(const pc_tracker_t* t);

/* Per-frame update. detects[] are normalized centers.
 * Returns via *out_records / *out_n_records: newly archived DEPARTED records.
 * Caller frees each record with PC_FREE. */
void pc_tracker_update(pc_tracker_t* t,
                       const pc_point_t* detects, uint8_t n_detects,
                       uint32_t now_ms,
                       pc_track_record_t*** out_records, uint16_t* out_n_records);

/* Window-end snapshot: emit CROSSING records for all active tracks, advance last_report_ts.
 * Tracks stay active. */
void pc_tracker_window_snapshot(pc_tracker_t* t, uint32_t window_end_ms,
                                pc_track_record_t*** out_records, uint16_t* out_n_records);

/* Apply line crossing to all stable tracks (age >= k_confirm). */
void pc_tracker_check_line_crossings(pc_tracker_t* t, pc_line_cross_t* lc,
                                     uint32_t now_ms,
                                     uint32_t* window_in_delta, uint32_t* window_out_delta,
                                     uint32_t* total_in_delta,  uint32_t* total_out_delta);

uint16_t pc_tracker_active_count(const pc_tracker_t* t);

typedef void (*pc_track_visitor_t)(const pc_track_t* trk, void* user);
/* Visits all tracks with id != 0 AND age >= k_confirm. */
void pc_tracker_for_each_stable(const pc_tracker_t* t, pc_track_visitor_t fn, void* user);

#endif
