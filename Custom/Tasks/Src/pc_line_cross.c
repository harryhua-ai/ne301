/* Custom/Tasks/Src/pc_line_cross.c */
#include "pc_line_cross.h"
#include "pc_tracker.h"
#include <math.h>
#include <string.h>

static inline float dot2(pc_point_t a, pc_point_t b) { return a.x*b.x + a.y*b.y; }
static inline pc_point_t sub2(pc_point_t a, pc_point_t b) { return (pc_point_t){a.x-b.x, a.y-b.y}; }
static inline int sign_nonzero(float v) { return v > 0.0f ? 1 : (v < 0.0f ? -1 : 0); }

pc_line_cross_t* pc_line_cross_create(float x1, float y1, float x2, float y2,
                                      float outside_x, float outside_y) {
    float dx = x2 - x1, dy = y2 - y1;
    float len = sqrtf(dx*dx + dy*dy);
    if (len < 0.01f) return NULL;   /* degenerate (spec §7.1) */

    /* unit direction */
    float ux = dx / len, uy = dy / len;
    /* candidate normal: rotate dir 90° CCW */
    pc_point_t n_cand = { -uy, ux };
    /* outside point relative to L1 */
    pc_point_t out_rel = { outside_x - x1, outside_y - y1 };
    /* if outside is on candidate-positive side, flip normal so n points INSIDE */
    pc_point_t n;
    if (n_cand.x * out_rel.x + n_cand.y * out_rel.y > 0.0f) {
        n = (pc_point_t){ -n_cand.x, -n_cand.y };
    } else {
        n = n_cand;
    }

    pc_line_cross_t* lc = (pc_line_cross_t*)PC_MALLOC(sizeof(*lc));
    if (!lc) return NULL;
    lc->L1 = (pc_point_t){x1, y1};
    lc->L2 = (pc_point_t){x2, y2};
    lc->n  = n;
    return lc;
}

void pc_line_cross_destroy(pc_line_cross_t* lc) { PC_FREE(lc); }

pc_cross_event_t pc_line_cross_check(pc_line_cross_t* lc, pc_track_t* trk,
                                     uint8_t prev_idx, uint8_t now_idx) {
    if (!lc || !trk) return PC_CROSS_NONE;

    pc_point_t p_now  = trk->history[now_idx];
    pc_point_t p_prev = trk->history[prev_idx];

    pc_point_t d_now  = sub2(p_now,  lc->L1);
    pc_point_t d_prev = sub2(p_prev, lc->L1);
    int side_now  = sign_nonzero(dot2(d_now,  lc->n));
    int side_prev = sign_nonzero(dot2(d_prev, lc->n));

    /* A crossing is detected whenever two consecutive samples are on opposite
     * sides. We do NOT require a multi-frame "sustain" — that would miss fast
     * passers-by who appear only a few frames. Single-frame detection jumps
     * (close/large targets) are instead tolerated; anti-bounce (counted_dir)
     * below prevents the same direction re-counting within one track. */
    if (side_now == 0 || side_prev == 0 || side_now == side_prev) {
        return PC_CROSS_NONE;
    }

    pc_point_t disp = sub2(p_now, p_prev);
    float proj = dot2(disp, lc->n);
    if (proj > 0.0f && !(trk->counted_dir & PC_BIT_IN)) {
        trk->counted_dir |= PC_BIT_IN;
        trk->counted_dir &= (uint8_t)~PC_BIT_OUT;
        trk->segment_events |= PC_BIT_IN;
        return PC_CROSS_IN;
    }
    if (proj < 0.0f && !(trk->counted_dir & PC_BIT_OUT)) {
        trk->counted_dir |= PC_BIT_OUT;
        trk->counted_dir &= (uint8_t)~PC_BIT_IN;
        trk->segment_events |= PC_BIT_OUT;
        return PC_CROSS_OUT;
    }
    return PC_CROSS_NONE;
}
