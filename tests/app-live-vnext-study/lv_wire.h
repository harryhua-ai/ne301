#ifndef LV_WIRE_H
#define LV_WIRE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define LV_ABI_STUDY 0x00040000u
#define LV_LIVE_EVT_SIZE 16u
#define LV_GAP_EVT_SIZE 16u
#define LV_LIVE_HDR_SIZE 28u
#define LV_LIVE_BATCH_MAX 16u
#define LV_RING_SLOTS_STUDY 32u
#define LV_RING_SLOTS_HOSTSTYLE 8u
#define LV_CURSOR_NONE 0u

#define LV_DIR_NONE 0u
#define LV_DIR_IN 1u
#define LV_DIR_OUT 2u
#define LV_FLAG_GAP_BEFORE 0x1u
#define LV_FLAG_REBIND_BEFORE 0x2u

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
    uint16_t reserved0;
    uint32_t batch_seq;
    uint32_t boot_generation;
    uint32_t session_generation;
    uint32_t first_seq;
    uint32_t evt_count;
} lv_live_hdr_t;

_Static_assert(sizeof(lv_live_evt_t) == LV_LIVE_EVT_SIZE, "lv_live_evt_t size");
_Static_assert(sizeof(lv_gap_evt_t) == LV_GAP_EVT_SIZE, "lv_gap_evt_t size");
_Static_assert(sizeof(lv_live_hdr_t) == LV_LIVE_HDR_SIZE, "lv_live_hdr_t size");

typedef enum {
    LV_RING_DROP_OLDEST = 0,
    LV_RING_DROP_NEWEST = 1,
    LV_RING_DROP_HOSTSTYLE = 2
} lv_ring_policy_t;

typedef struct {
    lv_live_evt_t slots[LV_RING_SLOTS_STUDY];
    uint32_t head;
    uint32_t count;
    uint32_t produced;
    uint32_t delivered;
    uint32_t dropped;
    uint32_t gap_declared;
    lv_ring_policy_t policy;
} lv_ring_t;

typedef void (*lv_sink_fn_t)(void *user, const lv_live_evt_t *evt, uint32_t gap_before);

static inline void lv_ring_init(lv_ring_t *r, lv_ring_policy_t policy)
{
    memset(r, 0, sizeof(*r));
    r->policy = policy;
}

static inline uint32_t lv_serial_next(uint32_t seq)
{
    uint32_t next = seq + 1u;
    return next ? next : 1u;
}

static inline uint8_t lv_serial_newer(uint32_t candidate, uint32_t current)
{
    if (candidate == current) {
        return 0u;
    }
    return ((int32_t)(candidate - current) > 0) ? 1u : 0u;
}

static inline void lv_ring_push(lv_ring_t *r, const lv_live_evt_t *evt)
{
    r->produced++;
    if (r->policy == LV_RING_DROP_NEWEST && r->count >= LV_RING_SLOTS_STUDY) {
        r->dropped++;
        r->gap_declared++;
        return;
    }
    if (r->count >= LV_RING_SLOTS_STUDY) {
        r->head = (r->head + 1u) % LV_RING_SLOTS_STUDY;
        r->count--;
        r->dropped++;
        r->gap_declared++;
    }
    r->slots[(r->head + r->count) % LV_RING_SLOTS_STUDY] = *evt;
    r->count++;
}

static inline void lv_ring_push_hoststyle(lv_ring_t *r, const lv_live_evt_t *evt,
                                          uint32_t carried_gap)
{
    r->produced++;
    if (r->count >= LV_RING_SLOTS_HOSTSTYLE) {
        r->head = (r->head + 1u) % LV_RING_SLOTS_HOSTSTYLE;
        r->count--;
        r->dropped += 1u + carried_gap;
        r->gap_declared += 1u + carried_gap;
    }
    r->slots[(r->head + r->count) % LV_RING_SLOTS_HOSTSTYLE] = *evt;
    r->count++;
}

static inline int lv_ring_pop(lv_ring_t *r, lv_live_evt_t *out)
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

#endif
