/* Custom/Tasks/Inc/pc_types.h
 * Pure-algorithm types for people counting. NO firmware dependencies.
 * Depends only on <stdint.h> so pc_tracker.c / pc_line_cross.c compile on PC.
 */
#ifndef PC_TYPES_H
#define PC_TYPES_H

#include <stdint.h>

typedef struct { float x, y; } pc_point_t;   /* normalized [0,1] */

#define PC_BIT_IN  0x01u
#define PC_BIT_OUT 0x02u

#define PC_K_MAX         16u   /* compile-time ring buffer cap */
#define PC_MAX_TRACKS   64u   /* static array bound */

typedef enum {
    PC_SEG_DEPARTED = 0,   /* track disappeared */
    PC_SEG_CROSSING = 1    /* window boundary, track still active */
} pc_seg_end_t;

typedef enum {
    PC_CROSS_NONE = 0,
    PC_CROSS_IN   = 1,
    PC_CROSS_OUT  = 2
} pc_cross_event_t;

typedef struct {
    uint32_t       track_id;
    uint32_t       segment_id;
    uint32_t       entered_at_ms;
    uint32_t       seg_start_ms;
    uint32_t       seg_end_ms;
    pc_seg_end_t   seg_end_type;
    uint8_t        events;        /* bitmask PC_BIT_IN / PC_BIT_OUT — per-segment (cleared on each CROSSING snapshot and after DEPARTED archive) */
    uint8_t        nb_points;
    /* points[] + point_ts[] are parallel flexible arrays — caller allocates the record.
     * Layout: pc_track_record_t header, then nb_points * pc_point_t, then nb_points * uint32_t. */
    pc_point_t     points[1];
    /* point_ts[] follows immediately after points[nb_points-1]; see PC_TRACK_RECORD_SIZE */
} pc_track_record_t;

/* Convenience to size an allocation for nb_points points (header + points + point_ts): */
#define PC_TRACK_RECORD_SIZE(nb) \
    (sizeof(pc_track_record_t)                                       \
     + ((nb) > 0u ? ((nb) - 1u) * sizeof(pc_point_t) : 0u)           \
     + ((nb) > 0u ?  (nb)        * sizeof(uint32_t)   : 0u))

/* Accessor for the parallel point_ts array (since it's not a real flex member). */
static inline uint32_t* pc_track_record_point_ts(pc_track_record_t* r) {
    return (uint32_t*)((uint8_t*)r + sizeof(pc_track_record_t)
                       + (r->nb_points > 0u ? (r->nb_points - 1u) * sizeof(pc_point_t) : 0u));
}
static inline const uint32_t* pc_track_record_point_ts_const(const pc_track_record_t* r) {
    return (const uint32_t*)((const uint8_t*)r + sizeof(pc_track_record_t)
                             + (r->nb_points > 0u ? (r->nb_points - 1u) * sizeof(pc_point_t) : 0u));
}

/* Memory abstraction: libc on host test, firmware allocator in production. */
#ifdef __PC_TEST__
    /* PC unit-test build: use libc malloc */
    #include <stdlib.h>
    #define PC_MALLOC(sz)     malloc(sz)
    #define PC_REALLOC(p, sz) realloc((p), (sz))
    #define PC_FREE(p)        free(p)
#else
    /* Firmware build: use the project's memory manager (mem.h).
     * hal_mem_alloc/realloc take (size, mem_type_t); the _any wrappers pick MEM_ANY. */
    #include "mem.h"
    #define PC_MALLOC(sz)     hal_mem_alloc_any(sz)
    #define PC_REALLOC(p, sz) hal_mem_realloc((p), (sz), MEM_ANY)
    #define PC_FREE(p)        hal_mem_free(p)
#endif

#endif /* PC_TYPES_H */
