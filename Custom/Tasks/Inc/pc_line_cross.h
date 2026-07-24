/* Custom/Tasks/Inc/pc_line_cross.h */
#ifndef PC_LINE_CROSS_H
#define PC_LINE_CROSS_H

#include "pc_types.h"

/* Forward-decl track_t (defined in pc_tracker.h). We avoid a hard circular dep
 * by declaring the struct here; pc_tracker.h includes us. */
typedef struct pc_track_t pc_track_t;

typedef struct {
    pc_point_t L1, L2;
    pc_point_t n;          /* unit normal pointing TOWARD inside */
    int        _pad;       /* alignment */
} pc_line_cross_t;

/* Returns NULL if line is degenerate (|L2-L1| < 0.01 in normalized space). */
pc_line_cross_t* pc_line_cross_create(float x1, float y1,
                                      float x2, float y2,
                                      float outside_x, float outside_y);
void             pc_line_cross_destroy(pc_line_cross_t* lc);

/* Check a track's prev_idx/now_idx points in its ring buffer.
 * Returns PC_CROSS_IN / PC_CROSS_OUT / PC_CROSS_NONE.
 * Mutates trk->counted_dir and trk->last_side per spec §3.2 + §3.3. */
pc_cross_event_t pc_line_cross_check(pc_line_cross_t* lc,
                                     pc_track_t* trk,
                                     uint8_t prev_idx, uint8_t now_idx);

#endif
