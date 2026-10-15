#ifndef LV_WIRE_H
#define LV_WIRE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define LV_ABI_STUDY 0x00040000u
#define LV_LIVE_EVT_SIZE 16u
#define LV_GAP_EVT_SIZE 16u
#define LV_LIVE_HDR_SIZE 40u
#define LV_LIVE_BATCH_MAX 16u
#define LV_RING_SLOTS_STUDY 32u
#define LV_HOSTSTYLE_MODEL_SLOTS 8u
#define LV_CURSOR_NONE 0u

#define LV_DIR_NONE 0u
#define LV_DIR_IN 1u
#define LV_DIR_OUT 2u
#define LV_FLAG_GAP_BEFORE 0x1u
#define LV_FLAG_REBIND_BEFORE 0x2u
#define LV_READ_FLAG_GAPPED 0x1u
#define LV_READ_FLAG_STALE 0x2u
#define LV_GAP_KIND_SOURCE_TRUNCATION 0x1u
#define LV_GAP_KIND_RING_DROP 0x2u
#define LV_GAP_KIND_REBIND 0x4u

typedef struct {
    uint32_t seq;
    uint32_t track_id;
    uint32_t ts_ms;
    uint8_t direction;
    uint8_t flags;
    uint8_t x_q8;
    uint8_t y_q8;
} lv_live_evt_t;

typedef struct {
    uint32_t seq;
    uint32_t declared_gap;
    uint32_t ts_ms;
    uint8_t kind;
    uint8_t flags;
    uint8_t reserved0;
    uint8_t reserved1;
} lv_gap_evt_t;

typedef struct {
    uint32_t magic;
    uint16_t header_version;
    uint16_t flags;
    uint32_t batch_seq;
    uint32_t boot_generation;
    uint32_t session_generation;
    uint32_t first_seq;
    uint32_t evt_count;
    uint32_t through_seq;
    uint32_t cumulative_total;
    uint32_t cumulative_gap;
} lv_live_hdr_t;

typedef struct {
    lv_live_hdr_t header;
    lv_gap_evt_t gap;
    lv_live_evt_t events[LV_LIVE_BATCH_MAX];
} lv_read_batch_t;

_Static_assert(sizeof(lv_live_evt_t) == LV_LIVE_EVT_SIZE, "lv_live_evt_t size");
_Static_assert(sizeof(lv_gap_evt_t) == LV_GAP_EVT_SIZE, "lv_gap_evt_t size");
_Static_assert(sizeof(lv_live_hdr_t) == LV_LIVE_HDR_SIZE, "lv_live_hdr_t size");

typedef enum {
    LV_RING_DROP_OLDEST = 0,
    LV_RING_DROP_NEWEST = 1
} lv_ring_policy_t;

typedef struct {
    lv_live_evt_t slots[LV_RING_SLOTS_STUDY];
    uint32_t head;
    uint32_t count;
    uint32_t produced;
    uint32_t delivered;
    uint32_t dropped;
    uint32_t gap_declared;
    uint32_t boot_generation;
    uint32_t session_generation;
    uint32_t latest_seq;
    uint32_t source_total;
    uint32_t cumulative_gap;
    uint32_t batch_seq;
    uint32_t last_read_seq;
    uint8_t gap_kind;
    uint8_t pending_event_flags;
    uint8_t last_read_valid;
    lv_ring_policy_t policy;
} lv_ring_t;

static inline uint32_t lv_serial_next(uint32_t seq)
{
    uint32_t next = seq + 1u;
    return next ? next : 1u;
}

static inline uint32_t lv_serial_advance(uint32_t seq, uint32_t count)
{
    while (count > 0u) {
        seq = lv_serial_next(seq);
        count--;
    }
    return seq;
}

static inline uint8_t lv_serial_newer(uint32_t candidate, uint32_t current)
{
    if (candidate == current) {
        return 0u;
    }
    return ((int32_t)(candidate - current) > 0) ? 1u : 0u;
}

static inline void lv_ring_note_loss(lv_ring_t *r, uint32_t count, uint8_t kind)
{
    r->gap_declared += count;
    r->cumulative_gap += count;
    r->gap_kind |= kind;
}

static inline void lv_ring_init_at(lv_ring_t *r, lv_ring_policy_t policy,
                                   uint32_t boot_generation,
                                   uint32_t session_generation,
                                   uint32_t initial_seq)
{
    memset(r, 0, sizeof(*r));
    r->policy = policy;
    r->boot_generation = boot_generation;
    r->session_generation = session_generation;
    r->latest_seq = initial_seq;
}

static inline void lv_ring_init(lv_ring_t *r, lv_ring_policy_t policy)
{
    lv_ring_init_at(r, policy, 1u, 1u, 0u);
}

static inline void lv_ring_push(lv_ring_t *r, const lv_live_evt_t *evt)
{
    lv_live_evt_t item = *evt;
    item.flags |= r->pending_event_flags;
    r->produced++;
    r->source_total++;
    r->latest_seq = item.seq;
    if (r->policy == LV_RING_DROP_NEWEST && r->count >= LV_RING_SLOTS_STUDY) {
        r->dropped++;
        lv_ring_note_loss(r, 1u, LV_GAP_KIND_RING_DROP);
        return;
    }
    if (r->count >= LV_RING_SLOTS_STUDY) {
        r->head = (r->head + 1u) % LV_RING_SLOTS_STUDY;
        r->count--;
        r->dropped++;
        lv_ring_note_loss(r, 1u, LV_GAP_KIND_RING_DROP);
    }
    r->slots[(r->head + r->count) % LV_RING_SLOTS_STUDY] = item;
    r->count++;
    r->pending_event_flags = 0u;
}

static inline int lv_ring_publish_frame(lv_ring_t *r,
                                        const lv_live_evt_t *events,
                                        uint32_t event_count,
                                        uint32_t source_count)
{
    uint32_t i;
    uint32_t truncated;
    if (event_count > LV_LIVE_BATCH_MAX || source_count < event_count) {
        return 0;
    }
    for (i = 0u; i < event_count; i++) {
        lv_live_evt_t item = events[i];
        item.seq = lv_serial_next(r->latest_seq);
        lv_ring_push(r, &item);
    }
    truncated = source_count - event_count;
    if (truncated > 0u) {
        r->source_total += truncated;
        r->latest_seq = lv_serial_advance(r->latest_seq, truncated);
        lv_ring_note_loss(r, truncated, LV_GAP_KIND_SOURCE_TRUNCATION);
    }
    return 1;
}

static inline void lv_ring_rebind(lv_ring_t *r)
{
    if (r->count > 0u) {
        r->dropped += r->count;
        lv_ring_note_loss(r, r->count, LV_GAP_KIND_REBIND);
        r->head = 0u;
        r->count = 0u;
    }
    r->session_generation = lv_serial_next(r->session_generation);
    r->gap_kind |= LV_GAP_KIND_REBIND;
    r->pending_event_flags |= LV_FLAG_REBIND_BEFORE;
}

static inline int lv_ring_pop_private(lv_ring_t *r, lv_live_evt_t *out)
{
    if (r->count == 0u) {
        return 0;
    }
    *out = r->slots[r->head];
    r->head = (r->head + 1u) % LV_RING_SLOTS_STUDY;
    r->count--;
    r->delivered++;
    return 1;
}

static inline int lv_ring_read(lv_ring_t *r, lv_read_batch_t *out,
                               uint32_t max_events)
{
    lv_live_evt_t evt;
    if (max_events > LV_LIVE_BATCH_MAX) {
        max_events = LV_LIVE_BATCH_MAX;
    }
    memset(out, 0, sizeof(*out));
    r->batch_seq = lv_serial_next(r->batch_seq);
    out->header.header_version = 1u;
    out->header.batch_seq = r->batch_seq;
    out->header.boot_generation = r->boot_generation;
    out->header.session_generation = r->session_generation;
    out->header.through_seq = r->latest_seq;
    out->header.cumulative_total = r->source_total;
    out->header.cumulative_gap = r->cumulative_gap;
    out->gap.seq = r->latest_seq;
    out->gap.declared_gap = r->cumulative_gap;
    out->gap.kind = r->gap_kind;
    if (r->cumulative_gap > 0u || (r->gap_kind & LV_GAP_KIND_REBIND) != 0u) {
        out->header.flags |= LV_READ_FLAG_STALE;
        out->gap.flags |= LV_FLAG_GAP_BEFORE;
    }
    if (r->cumulative_gap > 0u) {
        out->header.flags |= LV_READ_FLAG_GAPPED;
    }
    while (out->header.evt_count < max_events && lv_ring_pop_private(r, &evt)) {
        if (r->last_read_valid && evt.seq != lv_serial_next(r->last_read_seq)) {
            evt.flags |= LV_FLAG_GAP_BEFORE;
            out->header.flags |= LV_READ_FLAG_GAPPED | LV_READ_FLAG_STALE;
        }
        if ((evt.flags & (LV_FLAG_GAP_BEFORE | LV_FLAG_REBIND_BEFORE)) != 0u) {
            out->header.flags |= LV_READ_FLAG_STALE;
        }
        if ((evt.flags & LV_FLAG_GAP_BEFORE) != 0u) {
            out->header.flags |= LV_READ_FLAG_GAPPED;
        }
        if (out->header.evt_count == 0u) {
            out->header.first_seq = evt.seq;
        }
        out->events[out->header.evt_count] = evt;
        out->header.evt_count++;
        r->last_read_seq = evt.seq;
        r->last_read_valid = 1u;
    }
    out->gap.flags |= (out->header.flags & LV_READ_FLAG_STALE) ? LV_FLAG_GAP_BEFORE : 0u;
    out->gap.flags |= (r->gap_kind & LV_GAP_KIND_REBIND) ? LV_FLAG_REBIND_BEFORE : 0u;
    return 1;
}

#endif
