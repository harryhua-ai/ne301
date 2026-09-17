/* Custom/Tasks/Src/pc_tracker.c */
#include "pc_tracker.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

struct pc_tracker {
    pc_track_t          tracks[PC_MAX_TRACKS];
    uint16_t            n_active;
    uint32_t            next_id;
    pc_tracker_config_t cfg;
};

static uint8_t clamp_k(uint8_t k) { return k < 4u ? 4u : (k > PC_K_MAX ? PC_K_MAX : k); }
static float   max_dist_f(const pc_tracker_t* t) { return t->cfg.max_dist_permille / 1000.0f; }

pc_tracker_t* pc_tracker_create(const pc_tracker_config_t* cfg, uint32_t initial_next_id) {
    if (!cfg) return NULL;
    pc_tracker_t* t = (pc_tracker_t*)PC_MALLOC(sizeof(*t));
    if (!t) return NULL;
    memset(t, 0, sizeof(*t));
    t->cfg = *cfg;
    t->cfg.track_history_k = clamp_k(cfg->track_history_k);
    t->next_id = initial_next_id;
    return t;
}
void pc_tracker_destroy(pc_tracker_t* t) { PC_FREE(t); }
uint32_t pc_tracker_next_id(const pc_tracker_t* t) { return t ? t->next_id : 1u; }

uint16_t pc_tracker_active_count(const pc_tracker_t* t) {
    if (!t) return 0;
    uint16_t c = 0;
    for (uint16_t i = 0; i < PC_MAX_TRACKS; ++i) if (t->tracks[i].id != 0) c++;
    return c;
}

static pc_track_t* find_track_slot(pc_tracker_t* t) {
    for (uint16_t i = 0; i < PC_MAX_TRACKS; ++i) {
        if (t->tracks[i].id == 0) return &t->tracks[i];
    }
    return NULL;
}

static void archive_track(pc_tracker_t* t, uint16_t idx, pc_seg_end_t why, uint32_t now_ms,
                          pc_track_record_t*** out_records, uint16_t* out_n_records) {
    pc_track_t* trk = &t->tracks[idx];
    uint8_t k = t->cfg.track_history_k;
    /* determine sampling interval based on duration since last_report_ts */
    uint32_t from = trk->last_report_ts;
    uint32_t to   = now_ms;
    uint32_t dur  = to - from;
    uint32_t interval;
    if (dur < 1000u)        interval = 0;       /* full */
    else if (dur < 5000u)   interval = 333u;    /* ~3Hz */
    else                    interval = 1000u;   /* 1Hz */

    /* collect samples from ring buffer in (from, to] */
    /* first count them. Ring slots live in [0, k); use k (not PC_K_MAX) in the wrap. */
    uint8_t n_pts = 0;
    uint32_t next_ts = from;
    for (uint8_t i = 0; i < trk->history_used; ++i) {
        uint8_t slot = (uint8_t)((trk->history_head + k - trk->history_used + i) % k);
        if (trk->history_ts[slot] <= from) continue;
        if (trk->history_ts[slot] > to)    break;
        if (interval == 0 || trk->history_ts[slot] >= next_ts) {
            n_pts++;
            next_ts = trk->history_ts[slot] + interval;
        }
    }
    /* allocate record (header + n_pts points + n_pts timestamps) */
    pc_track_record_t* rec = (pc_track_record_t*)PC_MALLOC(PC_TRACK_RECORD_SIZE(n_pts));
    if (!rec) { /* allocation failure: drop silently */ goto done; }
    rec->track_id      = trk->id;
    rec->segment_id    = trk->segment_id;
    rec->entered_at_ms = trk->entered_at_ms;
    rec->seg_start_ms  = from;
    rec->seg_end_ms    = to;
    rec->seg_end_type  = why;
    /* events = per-segment accumulator (OR-only), NOT counted_dir (which is
     * anti-bounce state and clears the opposite bit on each cross, so it can
     * never be 0x03). segment_events accumulates every line_cross_in/out fired
     * during [last_report_ts, now]; cleared on CROSSING snapshot below. */
    rec->events        = trk->segment_events;
    rec->nb_points     = n_pts;
    uint32_t* pts_ts   = pc_track_record_point_ts(rec);
    /* fill points + parallel timestamps */
    uint8_t filled = 0;
    next_ts = from;
    for (uint8_t i = 0; i < trk->history_used; ++i) {
        uint8_t slot = (uint8_t)((trk->history_head + k - trk->history_used + i) % k);
        if (trk->history_ts[slot] <= from) continue;
        if (trk->history_ts[slot] > to)    break;
        if (interval == 0 || trk->history_ts[slot] >= next_ts) {
            rec->points[filled]  = trk->history[slot];
            pts_ts[filled]       = trk->history_ts[slot];
            filled++;
            next_ts = trk->history_ts[slot] + interval;
        }
    }

    /* append to out_records (caller frees). PC_REALLOC wraps hal_mem_realloc,
     * which allocates a fresh block and frees the old one WITHOUT copying —
     * existing entries must be carried over explicitly. */
    pc_track_record_t** grown = (pc_track_record_t**)PC_MALLOC(((*out_n_records) + 1) * sizeof(pc_track_record_t*));
    if (grown) {
        if (*out_records) {
            memcpy(grown, *out_records, (*out_n_records) * sizeof(pc_track_record_t*));
            PC_FREE(*out_records);
        }
        *out_records = grown;
        (*out_records)[*out_n_records] = rec;
        (*out_n_records)++;
    }
    else { PC_FREE(rec); }

done:
    if (why == PC_SEG_DEPARTED) {
        /* retire the slot */
        memset(trk, 0, sizeof(*trk));
    } else {
        /* CROSSING: keep alive, advance markers.
         * Only segment_events (the per-segment accumulator) resets — counted_dir
         * PERSISTS across window boundaries because it's anti-bounce state
         * (clearing it would let a track on the IN side re-fire IN next window). */
        trk->last_report_ts = to;
        trk->segment_id++;
        trk->segment_events = 0;
    }
}

void pc_tracker_update(pc_tracker_t* t, const pc_point_t* detects, uint8_t n_detects,
                       uint32_t now_ms,
                       pc_track_record_t*** out_records, uint16_t* out_n_records) {
    if (!t) return;
    uint8_t k = t->cfg.track_history_k;
    float thresh = max_dist_f(t);
    /* matched flag for detects — stack array (caller caps n_detects at 64).
     * Avoids per-frame malloc/free on the realtime camera pipeline thread. */
    uint8_t matched[64];
    if (n_detects > 64) n_detects = 64;
    memset(matched, 0, n_detects);

    /* iterate active tracks in deterministic order:
     * (last_match_ts DESC, miss_count ASC, id ASC). Build an index order. */
    uint16_t order[PC_MAX_TRACKS];
    uint16_t no = 0;
    for (uint16_t i = 0; i < PC_MAX_TRACKS; ++i) if (t->tracks[i].id != 0) order[no++] = i;
    /* simple insertion sort by the 3-key tuple */
    for (uint16_t i = 1; i < no; ++i) {
        uint16_t key = order[i]; uint16_t j = i;
        while (j > 0) {
            pc_track_t* a = &t->tracks[order[j-1]];
            pc_track_t* b = &t->tracks[key];
            int cmp = (a->last_match_ts == b->last_match_ts)
                      ? (a->miss_count == b->miss_count
                         ? (a->id == b->id ? 0 : (a->id < b->id ? -1 : 1))
                         : (a->miss_count < b->miss_count ? -1 : 1))
                      : (a->last_match_ts > b->last_match_ts ? -1 : 1);
            if (cmp > 0) { order[j] = order[j-1]; --j; } else break;
        }
        order[j] = key;
    }

    /* greedy match */
    for (uint16_t oi = 0; oi < no; ++oi) {
        pc_track_t* trk = &t->tracks[order[oi]];
        float best_d = 1e9f; int best_j = -1;
        for (uint8_t j = 0; j < n_detects; ++j) {
            if (matched[j]) continue;
            float dx = detects[j].x - trk->last_pos.x;
            float dy = detects[j].y - trk->last_pos.y;
            float d  = sqrtf(dx*dx + dy*dy);
            if (d < best_d) { best_d = d; best_j = (int)j; }
        }
        if (best_j >= 0 && best_d < thresh) {
            matched[best_j] = 1;
            uint8_t slot = trk->history_head;
            trk->history[slot]     = detects[best_j];
            trk->history_ts[slot]  = now_ms;
            trk->history_head      = (uint8_t)((slot + 1) % k);
            if (trk->history_used < k) trk->history_used++;
            trk->last_pos      = detects[best_j];
            trk->last_match_ts = now_ms;
            trk->age++;
            trk->miss_count    = 0;
        } else {
            trk->miss_count++;
        }
    }

    /* spawn new tracks for unmatched detects */
    for (uint8_t j = 0; j < n_detects; ++j) {
        if (matched[j]) continue;
        pc_track_t* slot = find_track_slot(t);
        if (!slot) break;
        memset(slot, 0, sizeof(*slot));
        slot->id             = t->next_id++;
        slot->history[0]     = detects[j];
        slot->history_ts[0]  = now_ms;
        slot->history_head   = 1;
        slot->history_used   = 1;
        slot->age            = 1;
        slot->miss_count     = 0;
        slot->last_side      = 0;
        slot->counted_dir    = 0;
        slot->entered_at_ms  = now_ms;
        slot->last_match_ts  = now_ms;
        slot->last_report_ts = now_ms;
        slot->segment_id     = 0;
        slot->last_pos       = detects[j];
    }

    /* retire tracks past max_miss */
    for (uint16_t i = 0; i < PC_MAX_TRACKS; ++i) {
        pc_track_t* trk = &t->tracks[i];
        if (trk->id == 0) continue;
        if (trk->miss_count > t->cfg.max_miss) {
            archive_track(t, i, PC_SEG_DEPARTED, now_ms, out_records, out_n_records);
        }
    }
}

void pc_tracker_window_snapshot(pc_tracker_t* t, uint32_t window_end_ms,
                                pc_track_record_t*** out_records, uint16_t* out_n_records) {
    if (!t) return;
    for (uint16_t i = 0; i < PC_MAX_TRACKS; ++i) {
        if (t->tracks[i].id == 0) continue;
        archive_track(t, i, PC_SEG_CROSSING, window_end_ms, out_records, out_n_records);
    }
}

void pc_tracker_check_line_crossings(pc_tracker_t* t, pc_line_cross_t* lc, uint32_t now_ms,
                                     uint32_t* win_in, uint32_t* win_out,
                                     uint32_t* tot_in,  uint32_t* tot_out) {
    (void)now_ms;   /* reserved for future timestamp-based logic */
    if (!t || !lc) return;
    uint8_t k = t->cfg.track_history_k;
    for (uint16_t i = 0; i < PC_MAX_TRACKS; ++i) {
        pc_track_t* trk = &t->tracks[i];
        if (trk->id == 0) continue;
        /* Need at least 2 samples to detect a crossing. Do NOT require the
         * track to be k_confirm-"stable": that would miss fast passers-by who
         * cross the line during their first couple of detections (by the time
         * they reach age==k_confirm they are already on the far side, so the
         * now-vs-prev points never straddle the line). Anti-bounce
         * (counted_dir in pc_line_cross_check) already prevents jitter
         * double-counts, so a stability gate here only loses real crossings. */
        if (trk->history_used < 2) continue;
        /* Compare the latest sample to the IMMEDIATELY previous one — this
         * catches the crossing the moment it happens, even for a young track.
         * history_head is the next WRITE slot, so latest = (head + k - 1) % k. */
        uint8_t now_idx  = (uint8_t)((trk->history_head + k - 1u) % k);
        uint8_t prev_idx = (uint8_t)((now_idx + k - 1u) % k);
        pc_cross_event_t ev = pc_line_cross_check(lc, trk, prev_idx, now_idx);
        if (ev == PC_CROSS_IN)       { if (win_in)  (*win_in)++;  if (tot_in)  (*tot_in)++; }
        else if (ev == PC_CROSS_OUT) { if (win_out) (*win_out)++; if (tot_out) (*tot_out)++; }
    }
}

void pc_tracker_for_each_stable(const pc_tracker_t* t, pc_track_visitor_t fn, void* user) {
    if (!t || !fn) return;
    for (uint16_t i = 0; i < PC_MAX_TRACKS; ++i) {
        const pc_track_t* trk = &t->tracks[i];
        if (trk->id != 0 && trk->age >= t->cfg.k_confirm) fn(trk, user);
    }
}
