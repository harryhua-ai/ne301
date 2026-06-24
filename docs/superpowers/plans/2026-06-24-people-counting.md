# People Counting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a top-down people counting scenario to NE301 firmware — YOLO person detection + centroid tracker + line crossing, fixed 5-min window stats with track-segment reporting via MQTT/Webhook, with a web UI for drawing the count line.

**Architecture:** A new `Custom/Tasks/` business-logic layer subscribes to AI results via a new subscriber registry in `ai_service`. Pure algorithm modules (`pc_tracker.c`, `pc_line_cross.c`) are PC-testable via Unity. A coordinator (`people_counting.c`) drives per-frame tracking + periodic window reporting, persisting config to the existing `aicam_global_config_t` and totals to a small LittleFS file.

**Tech Stack:** C11, CMSIS-RTOS2 (FreeRTOS), STM32N6 HAL, cJSON, LittleFS, Mongoose HTTP, Preact/TS frontend, Unity (host gcc) for unit tests.

**Spec:** `docs/superpowers/specs/2026-06-24-people-counting-design.md`

**Key codebase facts (verified):**
- `od_detect_t` (pp.h:49-58): `float x, y, width, height, conf; char *class_name;` — **x,y is top-left, normalized [0,1]**
- `nn_result_t` is `typedef pp_result_t nn_result_t;` (nn.h:24); iterate via `result->od.detects[i]` for `i < result->od.nb_detect`
- AI insertion point: `ai_service.c:501-502`, inside `ai_service_draw_callback()`. **Important:** subscriber notification must fire whenever `ai_service_get_nn_result()` returns OK, **even when `nb_detect==0`** (so tracks age out on empty frames). Place the call *before* the existing `if (nb_detect > 0 ...)` drawing guard.
- MQTT has `mqtt_service_publish_json(topic, json, qos, retain)` and `mqtt_service_is_connected()` (mqtt_service.h:216, 170)
- Webhook only has `webhook_service_push_capture()` — need a new generic `webhook_service_push_json()` (webhook_service.h:38-41)
- Config: `aicam_global_config_t` (json_config_mgr.h:518-535) is monolithic; add nested `people_counting_config_t` member + per-struct get/set following the `webhook_config_t` pattern
- Drawing: `draw_rect_param_t`, `draw_line_param_t`, `draw_printf_param_t` (draw.h); colors `COLOR_RED`/`COLOR_BLUE`/`COLOR_WHITE` (ARGB8888); framebuffer RGB565
- RTOS: CMSIS-OS2 — `osTimerNew(cb, osTimerPeriodic, arg, NULL)` + `osTimerStart(id, ms)`; `osKernelGetTickCount()`; `osMutexNew(NULL)` + `osMutexAcquire/Release`
- LittleFS: raw `lfs_file_open(&lfs, &file, path, flags)` etc. (storage.c:178)
- **No test framework exists** — Task 1 sets up Unity + host gcc
- **Model loading is by embedded pointer only** — `ai_load_model(uintptr_t model_ptr)`; no runtime string→model. Phase-2 model swap uses a **model registry** (Task 5).

---

## File Structure

**New files:**
```
Custom/Tasks/Inc/people_counting.h          # public API (4 functions)
Custom/Tasks/Src/people_counting.c          # coordinator: init, AI callback, window timer, report
Custom/Tasks/Src/pc_tracker.c               # pure algo: centroid tracking (PC-testable)
Custom/Tasks/Inc/pc_tracker.h
Custom/Tasks/Src/pc_line_cross.c            # pure algo: line crossing geometry (PC-testable)
Custom/Tasks/Inc/pc_line_cross.h
Custom/Tasks/Src/pc_backlog.c               # LittleFS backlog queue per channel
Custom/Tasks/Inc/pc_backlog.h
Custom/Tasks/Src/pc_model_registry.c        # name → uintptr_t model_ptr lookup
Custom/Tasks/Inc/pc_model_registry.h
Custom/Services/Web/api/api_people_counting_module.c   # REST endpoints
Custom/Services/Web/api/api_people_counting_module.h
Web/src/pages/people-counting/index.tsx     # Preact page
Web/src/pages/people-counting/LineCanvas.tsx
Web/src/pages/people-counting/ConfigPanel.tsx
Web/src/pages/people-counting/StatsPanel.tsx
Web/src/pages/people-counting/api.ts

Custom/Tasks/test/                          # PC unit tests (host gcc + Unity)
├── Makefile
├── unity/                                  # Unity framework (vendored, ~3 files)
├── test_pc_tracker.c
├── test_pc_line_cross.c
└── mocks/stub_headers/                     # empty stdint shims if needed
```

**Modified files:**
```
Custom/Services/AI/ai_service.h             # +subscriber registry API
Custom/Services/AI/ai_service.c             # +registry impl, +notify_subscribers() call at :502
Custom/Core/System/json_config_mgr.h        # +people_counting_config_t, +member in global, +get/set decls, +RO snapshot API
Custom/Core/System/json_config_mgr.c        # +defaults, +RO snapshot impl (seqlock)
Custom/Core/System/json_config_json.c       # +cJSON serialize/deserialize for pc config
Custom/Common/Inc/aicam_types.h             # +aicam_scenario_t enum
Custom/Services/Webhook/webhook_service.h   # +webhook_service_push_json decl
Custom/Services/Webhook/webhook_service.c   # +push_json impl
Custom/Services/Web/web_api.c               # +register /api/people-counting/* endpoints
Custom/Services/Web/api/api_modules.h       # +people counting module decl (if registry header exists)
Custom/Core/Video/ai_draw_service.{c,h}     # +ai_draw_count_line(), +ai_draw_count_text()
Custom/Core/core_init.c (or service_init.c) # +people_counting_init() call
Appli/Makefile (or Custom Makefile include) # +new .c files to SRCS
Web/src/router (or equivalent)              # +/people-counting route
Web/src/components/Navigation (or similar)  # +"客流统计" menu item
```

---

## Task Dependency Graph

```
T1 (test harness) ─┬─► T3 (line_cross algo, TDD)
T2 (types header)  ─┤
                   └─► T4 (tracker algo, TDD)

T5 (config + model registry) ──┐
T4 ────────────────────────────┼─► T6 (coordinator skeleton)
                               │
T7 (AI subscriber registry) ───┴─► T8 (wire AI → tracker per frame)
                                         │
                                         └─► T9 (archive segments)
                                                 │
T10 (window timer) ──────────────────────────────┤
                                                 └─► T11 (JSON report builder)
                                                         │
T12 (heat grid) ─────────────────────────────────────────┤
                                                         │
T13 (backlog) ───────────────────────────────────────────┤
                                                         └─► T14 (report dispatch: MQTT/Wh/backlog)
                                                                 │
T15 (totals persistence) ────────────────────────────────────────┤
T16 (drawing overlay) ────────────────────────────────────────────┤
                                                                  └─► T17 (Web API module + endpoint registration)
                                                                          │
                                                                          └─► T18 (Web frontend)
                                                                                  │
T19 (Makefile) ──────────────────────────────────────────────────────────────────────────┤
                                                                                  └─► T20 (HW integration)
```

---

## Task 1: PC Test Harness (Unity + host gcc)

**Files:**
- Create: `Custom/Tasks/test/Makefile`
- Create: `Custom/Tasks/test/unity/unity.h`, `unity.c`, `unity_internals.h` (vendor from https://github.com/ThrowTheSwitch/Unity)
- Create: `Custom/Tasks/test/test_main.c` (empty runner)

- [ ] **Step 1: Vendor Unity framework**

Download Unity v2.5.2 core files into `Custom/Tasks/test/unity/`. The three files are `unity.h`, `unity_internals.h`, `unity.c`. Commit these verbatim.

```bash
cd "Custom/Tasks/test"
mkdir -p unity
curl -sSL https://raw.githubusercontent.com/ThrowTheSwitch/Unity/v2.5.2/src/unity.h -o unity/unity.h
curl -sSL https://raw.githubusercontent.com/ThrowTheSwitch/Unity/v2.5.2/src/unity_internals.h -o unity/unity_internals.h
curl -sSL https://raw.githubusercontent.com/ThrowTheSwitch/Unity/v2.5.2/src/unity.c -o unity/unity.c
```

- [ ] **Step 2: Write the Makefile**

```makefile
# Custom/Tasks/test/Makefile
CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Werror -O0 -g -I../Inc -Iunity -I.
LDFLAGS = -lm

SRCS = unity/unity.c \
       ../Src/pc_line_cross.c \
       ../Src/pc_tracker.c \
       test_pc_line_cross.c \
       test_pc_tracker.c \
       test_main.c

OBJS = $(SRCS:.c=.o)

test_runner: $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: test_runner
	./test_runner

clean:
	rm -f $(OBJS) test_runner

.PHONY: run clean
```

- [ ] **Step 3: Write empty test runner**

```c
/* Custom/Tasks/test/test_main.c */
#include "unity.h"

int main(void) {
    UNITY_BEGIN();
    /* test_pc_line_cross() and test_pc_tracker() added in Tasks 3/4 */
    return UNITY_END();
}
```

- [ ] **Step 4: Verify the harness builds (will fail — no algorithm files yet)**

Run: `cd Custom/Tasks/test && make`
Expected: FAIL (pc_line_cross.c / pc_tracker.c don't exist yet). This confirms the harness is wired up.

- [ ] **Step 5: Commit**

```bash
git add Custom/Tasks/test/
git commit -m "test: add Unity harness for people counting algorithm tests"
```

---

## Task 2: Common Types Header

**Files:**
- Create: `Custom/Tasks/Inc/pc_types.h`

This header is the *only* dependency of the pure-algorithm modules. It must include **only `<stdint.h>`** so PC gcc compilation works without any firmware headers.

- [ ] **Step 1: Write the header**

```c
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

#endif /* PC_TYPES_H */
```

- [ ] **Step 2: Commit**

```bash
git add Custom/Tasks/Inc/pc_types.h
git commit -m "feat(pc): add shared pure-algorithm types header"
```

---

## Task 3: Line Crossing Algorithm (TDD)

**Files:**
- Create: `Custom/Tasks/Inc/pc_line_cross.h`
- Create: `Custom/Tasks/Src/pc_line_cross.c`
- Create: `Custom/Tasks/test/test_pc_line_cross.c`

Pure geometry, no firmware deps.

- [ ] **Step 1: Write the failing test**

```c
/* Custom/Tasks/test/test_pc_line_cross.c */
#include "unity.h"
#include "pc_line_cross.h"
#include "pc_types.h"

/* line: horizontal at y=0.5, outside=above (y<0.5), inside=below (y>0.5) */
static pc_line_cross_t* lc;

void setUp(void) {
    /* L1=(0.2,0.5), L2=(0.8,0.5), outside=(0.5,0.2) → inside is y>0.5 */
    lc = pc_line_cross_create(0.2f, 0.5f, 0.8f, 0.5f, 0.5f, 0.2f);
    TEST_ASSERT_NOT_NULL(lc);
}
void tearDown(void) { pc_line_cross_destroy(lc); lc = NULL; }

/* Spec §8 case 1: straight crossing → CROSS_IN */
void test_straight_cross_from_outside_to_inside_is_in(void) {
    pc_track_t trk = {0};
    trk.age = 10; trk.last_side = -1;  /* was outside */
    /* now inside (y=0.7), prev (y=0.3) */
    trk.history[0] = (pc_point_t){0.5f, 0.3f};
    trk.history_ts[0] = 100;
    trk.history[1] = (pc_point_t){0.5f, 0.7f};
    trk.history_ts[1] = 200;
    trk.history_head = 2; trk.history_used = 2;
    /* k_confirm effectively 1 here for the unit test (we control prev/now indices) */
    pc_cross_event_t ev = pc_line_cross_check(lc, &trk, /*prev_idx=*/0, /*now_idx=*/1);
    TEST_ASSERT_EQUAL(PC_CROSS_IN, ev);
    TEST_ASSERT_EQUAL_UINT8(PC_BIT_IN, trk.counted_dir);
}

/* Spec §8 case 2: stays on one side, jitter < 0.05 → NONE */
void test_jitter_on_one_side_is_none(void) {
    pc_track_t trk = {0};
    trk.age = 10; trk.last_side = -1;
    trk.history[0] = (pc_point_t){0.5f, 0.20f};
    trk.history[1] = (pc_point_t){0.5f, 0.23f};  /* still outside (<0.5) */
    trk.history_head = 2; trk.history_used = 2;
    pc_cross_event_t ev = pc_line_cross_check(lc, &trk, 0, 1);
    TEST_ASSERT_EQUAL(PC_CROSS_NONE, ev);
    TEST_ASSERT_EQUAL_UINT8(0, trk.counted_dir);
}

/* Spec §8 case 3: cross in, stable, cross back out → IN then OUT */
void test_real_back_and_forth_counts_in_and_out(void) {
    pc_track_t trk = {0};
    trk.age = 10; trk.last_side = -1;
    /* outside → inside */
    trk.history[0] = (pc_point_t){0.5f, 0.3f};
    trk.history[1] = (pc_point_t){0.5f, 0.7f};
    trk.history_head = 2; trk.history_used = 2;
    TEST_ASSERT_EQUAL(PC_CROSS_IN, pc_line_cross_check(lc, &trk, 0, 1));
    TEST_ASSERT_EQUAL_UINT8(PC_BIT_IN, trk.counted_dir);
    /* now inside → outside */
    trk.history[2] = (pc_point_t){0.5f, 0.3f};
    trk.history_head = 3; trk.history_used = 3;
    TEST_ASSERT_EQUAL(PC_CROSS_OUT, pc_line_cross_check(lc, &trk, 1, 2));
    TEST_ASSERT_EQUAL_UINT8(PC_BIT_OUT, trk.counted_dir);
}

/* Spec §8 case 4: degenerate line (L1≈L2) returns NULL */
void test_degenerate_line_returns_null(void) {
    pc_line_cross_t* bad = pc_line_cross_create(0.5f, 0.5f, 0.501f, 0.5f, 0.5f, 0.2f);
    TEST_ASSERT_NULL(bad);
}

/* last_side initialization: track born exactly on line, then moves off → no spurious count */
void test_last_side_init_when_first_side_nonzero(void) {
    pc_track_t trk = {0};
    trk.age = 5; trk.last_side = 0;  /* on line initially */
    trk.history[0] = (pc_point_t){0.5f, 0.5f};
    trk.history[1] = (pc_point_t){0.5f, 0.7f};
    trk.history_head = 2; trk.history_used = 2;
    /* First check: last_side was 0, gets initialized to +1 (inside), no count this frame */
    pc_cross_event_t ev = pc_line_cross_check(lc, &trk, 0, 1);
    TEST_ASSERT_EQUAL(PC_CROSS_NONE, ev);
    TEST_ASSERT_EQUAL(1, trk.last_side);
}
```

Add `void test_pc_line_cross(void);` to test_main.c and call it. Register in runner:

```c
/* test_main.c — updated */
extern void test_pc_line_cross(void);
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_pc_line_cross);
    return UNITY_END();
}
```

Wait — Unity convention is individual test functions. Let me restructure: the file has setUp/tearDown + individual `test_*` functions; runner calls each by name. Update test_main.c:

```c
/* Custom/Tasks/test/test_main.c */
#include "unity.h"

/* tests defined in other files: declare with extern prototypes */
extern void test_straight_cross_from_outside_to_inside_is_in(void);
extern void test_jitter_on_one_side_is_none(void);
extern void test_real_back_and_forth_counts_in_and_out(void);
extern void test_degenerate_line_returns_null(void);
extern void test_last_side_init_when_first_side_nonzero(void);
/* pc_tracker tests (Task 4): */
extern void test_tracker_straight_walk_produces_departed_segment(void);
extern void test_tracker_noise_below_k_confirm_no_crossing(void);extern void test_tracker_cross_window_produces_crossing_then_departed(void);
extern void test_tracker_small_jitter_on_one_side_no_count(void);
extern void test_tracker_real_back_and_forth_counts_one_each(void);
extern void test_tracker_multi_target_simultaneous_crossing(void);
extern void test_tracker_disappear_then_reappear_gets_new_id(void);
extern void test_tracker_brief_absence_below_max_miss_same_id(void);

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_straight_cross_from_outside_to_inside_is_in);
    RUN_TEST(test_jitter_on_one_side_is_none);
    RUN_TEST(test_real_back_and_forth_counts_in_and_out);
    RUN_TEST(test_degenerate_line_returns_null);
    RUN_TEST(test_last_side_init_when_first_side_nonzero);

    RUN_TEST(test_tracker_straight_walk_produces_departed_segment);
    RUN_TEST(test_tracker_noise_below_k_confirm_no_crossing);
    RUN_TEST(test_tracker_cross_window_produces_crossing_then_departed);
    RUN_TEST(test_tracker_small_jitter_on_one_side_no_count);
    RUN_TEST(test_tracker_real_back_and_forth_counts_one_each);
    RUN_TEST(test_tracker_multi_target_simultaneous_crossing);
    RUN_TEST(test_tracker_disappear_then_reappear_gets_new_id);
    RUN_TEST(test_tracker_brief_absence_below_max_miss_same_id);
    return UNITY_END();
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd Custom/Tasks/test && make run`
Expected: FAIL — `pc_line_cross.h` not found / linker errors.

- [ ] **Step 3: Write the header**

```c
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
```

- [ ] **Step 4: Write the implementation**

```c
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

    /* First-time init: if last_side is 0 (uninit) and side_now is nonzero, set it
     * and treat this frame as non-crossing (spec §3.2). */
    if (trk->last_side == 0 && side_now != 0) {
        trk->last_side = (int8_t)side_now;
        side_prev = side_now;
    }

    if (side_now == 0 || side_prev == 0 || side_now == side_prev) {
        return PC_CROSS_NONE;
    }

    pc_point_t disp = sub2(p_now, p_prev);
    float proj = dot2(disp, lc->n);
    if (proj > 0.0f && !(trk->counted_dir & PC_BIT_IN)) {
        trk->counted_dir |= PC_BIT_IN;
        trk->counted_dir &= (uint8_t)~PC_BIT_OUT;   /* reverse-crossing unlocks (spec §3.2) */
        trk->segment_events |= PC_BIT_IN;           /* OR-only accumulator (spec §3.4 events) */
        return PC_CROSS_IN;
    }
    if (proj < 0.0f && !(trk->counted_dir & PC_BIT_OUT)) {
        trk->counted_dir |= PC_BIT_OUT;
        trk->counted_dir &= (uint8_t)~PC_BIT_IN;
        trk->segment_events |= PC_BIT_OUT;          /* OR-only accumulator */
        return PC_CROSS_OUT;
    }
    return PC_CROSS_NONE;
}
```

Note: `PC_MALLOC` / `PC_FREE` are test-shim macros. Define in pc_types.h:

```c
/* In pc_types.h, add at bottom: */
#ifdef __PC_TEST__
    /* PC unit-test build: use libc malloc */
    #include <stdlib.h>
    #define PC_MALLOC(sz)  malloc(sz)
    #define PC_FREE(p)     free(p)
#else
    /* Firmware build: use the project's memory manager */
    #include "hal_mem.h"     /* or whatever the project's malloc is — verify in Task 21 */
    #define PC_MALLOC(sz)  hal_mem_alloc(sz)
    #define PC_FREE(p)     hal_mem_free(p)
#endif
```

(The exact firmware malloc name will be confirmed in Task 21 when wiring the Makefile; the test build uses `-D__PC_TEST__`.)

- [ ] **Step 5: Run tests, verify pass**

Run: `cd Custom/Tasks/test && make CFLAGS_EXTRA="-D__PC_TEST__" run`
(Add `CFLAGS_EXTRA` to the Makefile if needed; simpler: hardcode `-D__PC_TEST__` in test Makefile CFLAGS.)
Expected: 5 PASS.

- [ ] **Step 6: Commit**

```bash
git add Custom/Tasks/Inc/pc_line_cross.h Custom/Tasks/Src/pc_line_cross.c Custom/Tasks/test/test_pc_line_cross.c Custom/Tasks/test/test_main.c Custom/Tasks/Inc/pc_types.h
git commit -m "feat(pc): line crossing algorithm with unit tests (5 passing)"
```

---

## Task 4: Tracker Algorithm (TDD)

**Files:**
- Create: `Custom/Tasks/Inc/pc_tracker.h`
- Create: `Custom/Tasks/Src/pc_tracker.c`
- Create: `Custom/Tasks/test/test_pc_tracker.c`

- [ ] **Step 1: Write the header**

```c
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

#endif
```

- [ ] **Step 2: Write the failing tests**

```c
/* Custom/Tasks/test/test_pc_tracker.c */
#include "unity.h"
#include "pc_tracker.h"
#include "pc_line_cross.h"
#include <stdlib.h>

static pc_tracker_config_t cfg = {
    .max_dist_permille = 150,
    .track_history_k   = 8,
    .max_miss          = 5,
    .k_confirm         = 5,
};

/* helper: feed a single detection point at (x,y) for one frame */
static void feed(pc_tracker_t* t, float x, float y, uint32_t ms,
                 pc_track_record_t*** recs, uint16_t* nrecs) {
    pc_point_t d = {x, y};
    pc_tracker_update(t, &d, 1, ms, recs, nrecs);
}

/* Spec §8 case: straight walk produces a DEPARTED segment when track vanishes */
void test_tracker_straight_walk_produces_departed_segment(void) {
    pc_tracker_t* t = pc_tracker_create(&cfg, 1);
    TEST_ASSERT_NOT_NULL(t);

    /* appear at x=0.1, walk right to x=0.9 over 10 frames @ 100ms */
    pc_track_record_t** recs = NULL; uint16_t n = 0;
    for (int i = 0; i < 10; ++i) {
        feed(t, 0.1f + 0.08f*i, 0.5f, 100u*i, &recs, &n);
        /* free any records each frame (none until departure) */
        for (uint16_t k = 0; k < n; ++k) PC_FREE(recs[k]);
        free(recs); recs = NULL; n = 0;
    }
    /* now go silent for max_miss+1 frames → departure */
    for (int i = 0; i < cfg.max_miss + 1; ++i) {
        pc_tracker_update(t, NULL, 0, 1000u + 100u*i, &recs, &n);
    }
    TEST_ASSERT_EQUAL_UINT16(1, n);
    TEST_ASSERT_NOT_NULL(recs[0]);
    TEST_ASSERT_EQUAL(PC_SEG_DEPARTED, recs[0]->seg_end_type);
    TEST_ASSERT_EQUAL_UINT32(0, recs[0]->segment_id);

    for (uint16_t k = 0; k < n; ++k) PC_FREE(recs[k]);
    free(recs);
    pc_tracker_destroy(t);
}

/* Spec §8 case 7: a track that only survives 2 frames (< k_confirm) is still
 * archived on departure (DEPARTED record), but it MUST NOT have triggered any
 * line-crossing events (age never reached k_confirm). */
void test_tracker_noise_below_k_confirm_no_crossing(void) {
    pc_tracker_t* t = pc_tracker_create(&cfg, 1);
    pc_line_cross_t* lc = pc_line_cross_create(0.2f, 0.5f, 0.8f, 0.5f, 0.5f, 0.2f);
    pc_track_record_t** recs = NULL; uint16_t n = 0;
    /* appear for 2 frames only (< k_confirm=5) — also run line checks each frame */
    feed(t, 0.5f, 0.5f, 0,   &recs, &n);
    feed(t, 0.5f, 0.5f, 100, &recs, &n);
    uint32_t win_in=0, win_out=0, tot_in=0, tot_out=0;
    pc_tracker_check_line_crossings(t, lc, 100, &win_in, &win_out, &tot_in, &tot_out);
    TEST_ASSERT_EQUAL_UINT32(0, tot_in);
    TEST_ASSERT_EQUAL_UINT32(0, tot_out);
    /* then vanish */
    for (int i = 0; i < cfg.max_miss + 1; ++i)
        pc_tracker_update(t, NULL, 0, 200u + 100u*i, &recs, &n);
    TEST_ASSERT_EQUAL_UINT16(1, n);
    TEST_ASSERT_EQUAL(PC_SEG_DEPARTED, recs[0]->seg_end_type);
    TEST_ASSERT_EQUAL_UINT8(0, recs[0]->events);   /* no crossings for sub-k_confirm track */
    for (uint16_t k = 0; k < n; ++k) PC_FREE(recs[k]);
    free(recs);
    pc_line_cross_destroy(lc);
    pc_tracker_destroy(t);
}

/* Spec §8 case: cross-window produces CROSSING then DEPARTED, same track_id */
void test_tracker_cross_window_produces_crossing_then_departed(void) {
    pc_tracker_t* t = pc_tracker_create(&cfg, 1);
    pc_track_record_t** recs = NULL; uint16_t n = 0;
    /* track born at t=100, lives across window boundary at t=5000 */
    for (uint32_t i = 0; i < 30; ++i) feed(t, 0.5f, 0.5f, 100u + i*100u, &recs, &n);
    for (uint16_t k = 0; k < n; ++k) PC_FREE(recs[k]); free(recs); recs=NULL; n=0;

    /* window snapshot at 5000ms */
    pc_tracker_window_snapshot(t, 5000u, &recs, &n);
    TEST_ASSERT_EQUAL_UINT16(1, n);
    pc_track_record_t* seg0 = recs[0];
    TEST_ASSERT_EQUAL(PC_SEG_CROSSING, seg0->seg_end_type);
    TEST_ASSERT_EQUAL_UINT32(0, seg0->segment_id);
    TEST_ASSERT_TRUE(seg0->seg_end_ms >= seg0->seg_start_ms);  /* window span well-formed */
    uint32_t seg0_end = seg0->seg_end_ms;
    uint32_t tid = seg0->track_id;
    for (uint16_t k = 0; k < n; ++k) PC_FREE(recs[k]); free(recs); recs=NULL; n=0;

    /* continue, then depart */
    for (uint32_t i = 0; i < 3; ++i) feed(t, 0.5f, 0.5f, 5100u + i*100u, &recs, &n);
    for (uint16_t k = 0; k < n; ++k) PC_FREE(recs[k]); free(recs); recs=NULL; n=0;
    for (int i = 0; i < cfg.max_miss + 1; ++i)
        pc_tracker_update(t, NULL, 0, 5400u + 100u*i, &recs, &n);
    TEST_ASSERT_EQUAL_UINT16(1, n);
    pc_track_record_t* seg1 = recs[0];
    TEST_ASSERT_EQUAL(PC_SEG_DEPARTED, seg1->seg_end_type);
    TEST_ASSERT_EQUAL_UINT32(tid, seg1->track_id);
    TEST_ASSERT_EQUAL_UINT32(1, seg1->segment_id);
    TEST_ASSERT_EQUAL_UINT32(seg0_end, seg1->seg_start_ms);  /* continuation */

    for (uint16_t k = 0; k < n; ++k) PC_FREE(recs[k]); free(recs);
    pc_tracker_destroy(t);
}

/* Spec §3.3 failure mode 1 + §8 anti-bounce case:
 * Target stable on one side for K_CONFIRM frames, then SMALL (<0.05 normalized)
 * jitter that stays on the SAME side → NO crossing counted.
 * This is the spec's mandatory anti-bounce failure-mode test. */
void test_tracker_small_jitter_on_one_side_no_count(void) {
    pc_tracker_t* t = pc_tracker_create(&cfg, 1);
    pc_line_cross_t* lc = pc_line_cross_create(0.2f, 0.5f, 0.8f, 0.5f, 0.5f, 0.2f);
    TEST_ASSERT_NOT_NULL(lc);

    pc_track_record_t** recs = NULL; uint16_t n = 0;
    /* stable ABOVE the line (outside, y<0.5) for k_confirm+2 frames */
    for (uint8_t i = 0; i < cfg.k_confirm + 2; ++i)
        feed(t, 0.5f, 0.30f, 100u*i, &recs, &n);
    for (uint16_t k = 0; k < n; ++k) PC_FREE(recs[k]); free(recs); recs=NULL; n=0;

    /* 20 frames of tiny jitter, all staying above y=0.5 (y in [0.28, 0.32] < 0.5).
     * Amplitude 0.02 < 0.05 spec threshold, and never crosses y=0.5. */
    uint32_t tot_in = 0, tot_out = 0;
    for (uint8_t j = 0; j < 20; ++j) {
        float y = (j % 2 == 0) ? 0.28f : 0.32f;
        feed(t, 0.5f, y, 100u*(cfg.k_confirm+2+j), &recs, &n);
        for (uint16_t k = 0; k < n; ++k) PC_FREE(recs[k]); free(recs); recs=NULL; n=0;
        uint32_t win_in = 0, win_out = 0;
        pc_tracker_check_line_crossings(t, lc, 0, &win_in, &win_out, &tot_in, &tot_out);
    }
    TEST_ASSERT_EQUAL_UINT32(0, tot_in);
    TEST_ASSERT_EQUAL_UINT32(0, tot_out);

    pc_line_cross_destroy(lc);
    pc_tracker_destroy(t);
}

/* Spec §3.3 failure mode 2 + §8 anti-bounce case:
 * Target truly crosses to the opposite side, STAYS for K_CONFIRM frames,
 * then crosses back → exactly in=1 out=1 (no double-counting either direction).
 * This validates real foot-traffic semantics (§1.2). */
void test_tracker_real_back_and_forth_counts_one_each(void) {
    pc_tracker_t* t = pc_tracker_create(&cfg, 1);
    pc_line_cross_t* lc = pc_line_cross_create(0.2f, 0.5f, 0.8f, 0.5f, 0.5f, 0.2f);
    TEST_ASSERT_NOT_NULL(lc);

    pc_track_record_t** recs = NULL; uint16_t n = 0;
    uint32_t tot_in = 0, tot_out = 0;
    uint32_t ts = 0;

    /* phase 1: stable above (outside) for k_confirm+2 frames */
    for (uint8_t i = 0; i < cfg.k_confirm + 2; ++i) {
        feed(t, 0.5f, 0.30f, ts, &recs, &n); ts += 100;
        for (uint16_t k = 0; k < n; ++k) PC_FREE(recs[k]); free(recs); recs=NULL; n=0;
        uint32_t wi=0, wo=0;
        pc_tracker_check_line_crossings(t, lc, ts, &wi, &wo, &tot_in, &tot_out);
    }
    TEST_ASSERT_EQUAL_UINT32(0, tot_in);

    /* phase 2: cross to inside (y=0.70) and STABLE for k_confirm+2 frames */
    for (uint8_t i = 0; i < cfg.k_confirm + 2; ++i) {
        feed(t, 0.5f, 0.70f, ts, &recs, &n); ts += 100;
        for (uint16_t k = 0; k < n; ++k) PC_FREE(recs[k]); free(recs); recs=NULL; n=0;
        uint32_t wi=0, wo=0;
        pc_tracker_check_line_crossings(t, lc, ts, &wi, &wo, &tot_in, &tot_out);
    }
    TEST_ASSERT_EQUAL_UINT32(1, tot_in);   /* exactly one IN */
    TEST_ASSERT_EQUAL_UINT32(0, tot_out);

    /* phase 3: cross back to outside and STABLE for k_confirm+2 frames */
    for (uint8_t i = 0; i < cfg.k_confirm + 2; ++i) {
        feed(t, 0.5f, 0.30f, ts, &recs, &n); ts += 100;
        for (uint16_t k = 0; k < n; ++k) PC_FREE(recs[k]); free(recs); recs=NULL; n=0;
        uint32_t wi=0, wo=0;
        pc_tracker_check_line_crossings(t, lc, ts, &wi, &wo, &tot_in, &tot_out);
    }
    TEST_ASSERT_EQUAL_UINT32(1, tot_in);   /* unchanged */
    TEST_ASSERT_EQUAL_UINT32(1, tot_out);  /* exactly one OUT */

    /* phase 4: cross to inside AGAIN — second IN must fire (no over-locking) */
    for (uint8_t i = 0; i < cfg.k_confirm + 2; ++i) {
        feed(t, 0.5f, 0.70f, ts, &recs, &n); ts += 100;
        for (uint16_t k = 0; k < n; ++k) PC_FREE(recs[k]); free(recs); recs=NULL; n=0;
        uint32_t wi=0, wo=0;
        pc_tracker_check_line_crossings(t, lc, ts, &wi, &wo, &tot_in, &tot_out);
    }
    TEST_ASSERT_EQUAL_UINT32(2, tot_in);   /* second IN counted */
    TEST_ASSERT_EQUAL_UINT32(1, tot_out);

    pc_line_cross_destroy(lc);
    pc_tracker_destroy(t);
}

/* Spec §8 case: two simultaneous targets crossing the line both get counted */
void test_tracker_multi_target_simultaneous_crossing(void) {
    pc_tracker_t* t = pc_tracker_create(&cfg, 1);
    pc_line_cross_t* lc = pc_line_cross_create(0.2f, 0.5f, 0.8f, 0.5f, 0.5f, 0.2f);

    /* spawn two stable tracks side-by-side, both above the line */
    pc_track_record_t** recs = NULL; uint16_t n = 0;
    pc_point_t pair[2];
    for (uint8_t i = 0; i < cfg.k_confirm + 2; ++i) {
        pair[0] = (pc_point_t){0.3f, 0.30f};
        pair[1] = (pc_point_t){0.7f, 0.30f};
        pc_tracker_update(t, pair, 2, 100u*i, &recs, &n);
        for (uint16_t k = 0; k < n; ++k) PC_FREE(recs[k]); free(recs); recs=NULL; n=0;
    }
    TEST_ASSERT_EQUAL_UINT16(2, pc_tracker_active_count(t));

    /* both cross to inside simultaneously */
    pair[0] = (pc_point_t){0.3f, 0.70f};
    pair[1] = (pc_point_t){0.7f, 0.70f};
    pc_tracker_update(t, pair, 2, 100u*(cfg.k_confirm+2), &recs, &n);
    for (uint16_t k = 0; k < n; ++k) PC_FREE(recs[k]); free(recs); recs=NULL; n=0;

    uint32_t win_in = 0, win_out = 0, tot_in = 0, tot_out = 0;
    pc_tracker_check_line_crossings(t, lc, 0, &win_in, &win_out, &tot_in, &tot_out);
    TEST_ASSERT_EQUAL_UINT32(2, win_in);
    TEST_ASSERT_EQUAL_UINT32(2, tot_in);
    TEST_ASSERT_EQUAL_UINT32(0, win_out);

    pc_line_cross_destroy(lc);
    pc_tracker_destroy(t);
}

/* Spec §8 case: track lost past max_miss, then a NEW person appears — new id assigned,
 * old track archived as DEPARTED, no id reuse */
void test_tracker_disappear_then_reappear_gets_new_id(void) {
    pc_tracker_t* t = pc_tracker_create(&cfg, 100);
    pc_track_record_t** recs = NULL; uint16_t n = 0;

    /* track 1 lives briefly then departs */
    feed(t, 0.4f, 0.5f, 0, &recs, &n);
    feed(t, 0.4f, 0.5f, 100, &recs, &n);
    for (int i = 0; i < cfg.max_miss + 1; ++i)
        pc_tracker_update(t, NULL, 0, 200u + 100u*i, &recs, &n);
    TEST_ASSERT_EQUAL_UINT16(1, n);
    uint32_t first_id = recs[0]->track_id;
    for (uint16_t k = 0; k < n; ++k) PC_FREE(recs[k]); free(recs); recs=NULL; n=0;
    TEST_ASSERT_EQUAL_UINT16(0, pc_tracker_active_count(t));

    /* a new person appears — must get a strictly greater id */
    feed(t, 0.6f, 0.5f, 5000, &recs, &n);
    TEST_ASSERT_EQUAL_UINT16(1, pc_tracker_active_count(t));
    /* drive to departure to read its id */
    feed(t, 0.6f, 0.5f, 5100, &recs, &n);
    for (int i = 0; i < cfg.max_miss + 1; ++i)
        pc_tracker_update(t, NULL, 0, 5200u + 100u*i, &recs, &n);
    TEST_ASSERT_EQUAL_UINT16(1, n);
    TEST_ASSERT_TRUE(recs[0]->track_id > first_id);

    for (uint16_t k = 0; k < n; ++k) PC_FREE(recs[k]); free(recs);
    pc_tracker_destroy(t);
}

/* Spec §8 case 6 companion: brief absence < MAX_MISS frames → track survives
 * and keeps the same id (no spurious departure + respawn). */
void test_tracker_brief_absence_below_max_miss_same_id(void) {
    pc_tracker_t* t = pc_tracker_create(&cfg, 1);
    pc_track_record_t** recs = NULL; uint16_t n = 0;

    /* spawn + stabilize */
    for (uint8_t i = 0; i < cfg.k_confirm + 2; ++i)
        feed(t, 0.4f, 0.5f, 100u*i, &recs, &n);
    for (uint16_t k = 0; k < n; ++k) PC_FREE(recs[k]); free(recs); recs=NULL; n=0;
    TEST_ASSERT_EQUAL_UINT16(1, pc_tracker_active_count(t));

    /* capture id before gap */
    /* (no public id accessor; infer by driving to departure and reading rec.
     * Instead, verify continuity by absence of any DEPARTED record during the gap.) */
    uint32_t before_gap_records = 0;
    for (uint8_t i = 0; i < cfg.max_miss - 1; ++i) {  /* < max_miss: should NOT retire */
        pc_tracker_update(t, NULL, 0, 100u*(cfg.k_confirm+2+i), &recs, &n);
        before_gap_records += n;
        for (uint16_t k = 0; k < n; ++k) PC_FREE(recs[k]); free(recs); recs=NULL; n=0;
    }
    TEST_ASSERT_EQUAL_UINT32(0, before_gap_records);   /* no departure archived */
    TEST_ASSERT_EQUAL_UINT16(1, pc_tracker_active_count(t));  /* still alive */

    /* reappear — same track, no new id spawned */
    feed(t, 0.4f, 0.5f, 100u*(cfg.k_confirm+2+cfg.max_miss), &recs, &n);
    for (uint16_t k = 0; k < n; ++k) PC_FREE(recs[k]); free(recs); recs=NULL; n=0;
    TEST_ASSERT_EQUAL_UINT16(1, pc_tracker_active_count(t));  /* still exactly one track */

    pc_tracker_destroy(t);
}
```

- [ ] **Step 3: Run, verify fail**

Run: `cd Custom/Tasks/test && make run`
Expected: FAIL (pc_tracker symbols undefined).

- [ ] **Step 4: Write pc_tracker.c**

```c
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

uint16_t pc_tracker_active_count(const pc_tracker_t* t) { return t ? t->n_active : 0; }

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

    /* append to out_records (caller frees) */
    pc_track_record_t** grown = (pc_track_record_t**)realloc(*out_records,
                                                             (*out_n_records + 1) * sizeof(pc_track_record_t*));
    if (grown) { *out_records = grown; (*out_records)[*out_n_records] = rec; (*out_n_records)++; }
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
    /* matched flag for detects */
    uint8_t* matched = (uint8_t*)PC_MALLOC(n_detects ? n_detects : 1);
    if (!matched) return;   /* OOM: skip this frame's update (tracks age via miss_count next frame) */
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
    PC_FREE(matched);
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
    if (!t || !lc) return;
    uint8_t k = t->cfg.track_history_k;
    for (uint16_t i = 0; i < PC_MAX_TRACKS; ++i) {
        pc_track_t* trk = &t->tracks[i];
        if (trk->id == 0 || trk->age < t->cfg.k_confirm) continue;
        if (trk->history_used < 2) continue;
        /* compare latest point vs the point k_confirm frames earlier.
         * history_head points at the next WRITE slot, so the most recent
         * sample is (head + k - 1) % k. We step back k_confirm samples,
         * clamped to history_used so we never deref an uninitialized slot. */
        uint8_t now_idx  = (uint8_t)((trk->history_head + k - 1u) % k);
        uint8_t k_back   = (t->cfg.k_confirm < trk->history_used)
                             ? t->cfg.k_confirm
                             : (uint8_t)(trk->history_used - 1u);
        uint8_t prev_idx = (uint8_t)((now_idx + k - k_back) % k);
        pc_cross_event_t ev = pc_line_cross_check(lc, trk, prev_idx, now_idx);
        if (ev == PC_CROSS_IN)       { if (win_in)  (*win_in)++;  if (tot_in)  (*tot_in)++; }
        else if (ev == PC_CROSS_OUT) { if (win_out) (*win_out)++; if (tot_out) (*tot_out)++; }
    }
}
```

> **prev_idx correctness note:** `(now_idx + k - k_back) % k` cannot underflow because
> `k_back <= history_used <= k`, so `now_idx + k - k_back >= 0`. The clamp
> `k_back = min(k_confirm, history_used - 1)` guarantees prev_idx != now_idx
> (so a track with only 1 sample, blocked by the `history_used < 2` guard anyway,
> never compares a point against itself).

- [ ] **Step 5: Run tests, iterate until pass**

Run: `cd Custom/Tasks/test && make run`
Expected: all 12 tests PASS (5 from Task 3 + 7 from Task 4).

> **Spec §8 case 4 note (flag for human):** spec case 4 asserts "目标穿越后未稳定 K_CONFIRM 帧就反向穿回 → in=0 out=0". Tracing the spec's own §3.2 algorithm: when the target first reaches the far side (frame N), `prev_idx` = frame N−K_CONFIRM (still on the original side) and `now_idx` = frame N (far side) → the IN crossing FIRES. The bitmap lock then prevents the OUT on the brief return (because prev/now land same-side). Net result for a brief dip is **in=1, out=0**, not in=0 out=0 as the spec case 4 asserts. The plan's test suite therefore covers case 4's *intent* (anti-bounce prevents double-counting on a return) via `test_tracker_real_back_and_forth_counts_one_each` (case 3, in=1 out=1 for a settled cross-back) and `test_tracker_small_jitter_on_one_side_no_count` (case 2, sub-threshold jitter). The literal case-4 assertion is unreachable with the §3.2 algorithm and is treated as a spec wording bug; the implementer should surface this to the spec owner if a strict in=0/out=0 test is demanded.

- [ ] **Step 6: Commit**

```bash
git add Custom/Tasks/Inc/pc_tracker.h Custom/Tasks/Src/pc_tracker.c Custom/Tasks/test/test_pc_tracker.c
git commit -m "feat(pc): centroid tracker with segment archive and unit tests"
```

---

## Task 5: Config Struct + Model Registry + RO Snapshot API

**Files:**
- Modify: `Custom/Common/Inc/aicam_types.h` — add `aicam_scenario_t`
- Modify: `Custom/Core/System/json_config_mgr.h` — add `people_counting_config_t`, add member to `aicam_global_config_t`, declare get/set + RO snapshot
- Modify: `Custom/Core/System/json_config_mgr.c` — defaults + RO snapshot impl (seqlock)
- Modify: `Custom/Core/System/json_config_json.c` — cJSON serialize/deserialize
- Create: `Custom/Tasks/Inc/pc_model_registry.h`, `Custom/Tasks/Src/pc_model_registry.c`

- [ ] **Step 1: Add scenario enum to aicam_types.h**

```c
/* Append near work mode enum (aicam_types.h:~163) */
typedef enum {
    AICAM_SCENARIO_NONE            = 0,
    AICAM_SCENARIO_PEOPLE_COUNTING = 1
} aicam_scenario_t;
```

- [ ] **Step 2: Define people_counting_config_t in json_config_mgr.h**

Place above the `aicam_global_config_t` definition (line ~518). Follow the `webhook_config_t` template:

```c
#define PC_TARGET_CLASS_NAME_LEN  32
#define PC_MODEL_NAME_LEN         64
#define PC_PP_TYPE_LEN            32

typedef struct {
    aicam_bool_t enable;

    /* geometry — normalized, stored as permille to avoid float-in-JSON drift */
    uint16_t line_x1_permille, line_y1_permille;
    uint16_t line_x2_permille, line_y2_permille;
    uint16_t outside_x_permille, outside_y_permille;

    uint16_t conf_threshold_permille;   /* default 250 */
    uint16_t max_dist_permille;         /* default 150 */

    /* model decoupling (spec §11) */
    char target_class_name[PC_TARGET_CLASS_NAME_LEN];  /* default "person" */
    char model_name[PC_MODEL_NAME_LEN];                /* default "yolov8n_256_quant_pc_uf_od_coco-person-st" */
    char model_pp_type[PC_PP_TYPE_LEN];                /* default "pp_od_yolo_v8_uf" */

    uint8_t  track_history_k;           /* default 8, clamped [4, 16] */
    uint8_t  max_miss;                  /* default 5 */
    uint8_t  k_confirm;                 /* default 5 */

    uint16_t window_minutes;            /* default 5 */

    aicam_bool_t mqtt_report_enable;    /* default true */
    aicam_bool_t webhook_report_enable; /* default false */
    aicam_bool_t tracks_report_enable;  /* default true */
    aicam_bool_t heat_grid_enable;      /* default false */

    uint16_t backlog_capacity;          /* default 24 */
} people_counting_config_t;

/* Add as last member of aicam_global_config_t: */
typedef struct {
     uint32_t config_version;
     uint32_t magic_number;
     uint32_t checksum;
     uint64_t timestamp;
    log_config_t log_config;
    ai_debug_config_t ai_debug;
    work_mode_config_t work_mode_config;
    power_mode_config_t power_mode_config;
    device_info_config_t device_info;
    device_service_config_t device_service;
    network_service_config_t network_service;
    mqtt_service_config_t mqtt_service;
    auth_mgr_config_t auth_mgr;
    webhook_config_t webhook_config;
    people_counting_config_t people_counting;   /* NEW */
} aicam_global_config_t;

/* New APIs */
aicam_result_t json_config_get_people_counting_config(people_counting_config_t *config);
aicam_result_t json_config_set_people_counting_config(const people_counting_config_t *config);

/* RO snapshot (spec §4.1) — zero-copy read for frame callback */
const aicam_global_config_t* json_config_get_config_ro(void);
```

- [ ] **Step 3: Implement defaults + get/set + RO snapshot in json_config_mgr.c**

In the defaults function (where `default_config` is built, around line 274), add:

```c
default_config.people_counting = (people_counting_config_t){
    .enable                 = AICAM_FALSE,
    .line_x1_permille       = 200,
    .line_y1_permille       = 500,
    .line_x2_permille       = 800,
    .line_y2_permille       = 500,
    .outside_x_permille     = 500,
    .outside_y_permille     = 200,
    .conf_threshold_permille= 250,
    .max_dist_permille      = 150,
    .target_class_name      = "person",
    .model_name             = "yolov8n_256_quant_pc_uf_od_coco-person-st",
    .model_pp_type          = "pp_od_yolo_v8_uf",
    .track_history_k        = 8,
    .max_miss               = 5,
    .k_confirm              = 5,
    .window_minutes         = 5,
    .mqtt_report_enable     = AICAM_TRUE,
    .webhook_report_enable  = AICAM_FALSE,
    .tracks_report_enable   = AICAM_TRUE,
    .heat_grid_enable       = AICAM_FALSE,
    .backlog_capacity       = 24,
};
```

Add get/set wrappers (copy from the webhook_config pattern):

```c
aicam_result_t json_config_get_people_counting_config(people_counting_config_t *config) {
    if (!config) return AICAM_ERROR_INVALID_PARAM;
    const aicam_global_config_t* ro = json_config_get_config_ro();
    *config = ro->people_counting;
    return AICAM_OK;
}

aicam_result_t json_config_set_people_counting_config(const people_counting_config_t *config) {
    if (!config) return AICAM_ERROR_INVALID_PARAM;
    aicam_global_config_t mutated;
    json_config_get_config(&mutated);
    mutated.people_counting = *config;
    return json_config_set_config(&mutated);
}
```

Implement RO snapshot with seqlock:

```c
/* json_config_mgr.c — add near g_json_config_ctx */
static volatile uint32_t g_config_seq = 0;

const aicam_global_config_t* json_config_get_config_ro(void) {
    for (;;) {
        uint32_t s1 = __LDREX(&g_config_seq);   /* or use __atomic_load_n if GCC builtins */
        if (s1 & 1u) { /* write in progress, retry */ continue; }
        const aicam_global_config_t* p = &g_json_config_ctx.current_config;
        __DMB();
        uint32_t s2 = g_config_seq;
        if (s1 == s2) return p;
    }
}

/* In json_config_set_config(): before/after mutating current_config, bump seq odd/even */
/* Find json_config_set_config() and wrap its mutation: */
/*
    __atomic_store_n(&g_config_seq, g_config_seq + 1, __ATOMIC_RELEASE);  // make odd
    __DMB();
    g_json_config_ctx.current_config = *config;
    __DMB();
    __atomic_store_n(&g_config_seq, g_config_seq + 1, __ATOMIC_RELEASE);  // make even
*/
```

(Use CMSIS `__DMB()` or GCC `__atomic_thread_fence`; the existing project's atomic style should be matched — check what's used elsewhere.)

- [ ] **Step 4: Add cJSON serialization in json_config_json.c**

```c
/* json_config_json.c — add */
cJSON* people_counting_config_to_json(const people_counting_config_t* c) {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "enable", c->enable);
    cJSON_AddNumberToObject(o, "line_x1_permille", c->line_x1_permille);
    cJSON_AddNumberToObject(o, "line_y1_permille", c->line_y1_permille);
    cJSON_AddNumberToObject(o, "line_x2_permille", c->line_x2_permille);
    cJSON_AddNumberToObject(o, "line_y2_permille", c->line_y2_permille);
    cJSON_AddNumberToObject(o, "outside_x_permille", c->outside_x_permille);
    cJSON_AddNumberToObject(o, "outside_y_permille", c->outside_y_permille);
    cJSON_AddNumberToObject(o, "conf_threshold_permille", c->conf_threshold_permille);
    cJSON_AddNumberToObject(o, "max_dist_permille", c->max_dist_permille);
    cJSON_AddStringToObject(o, "target_class_name", c->target_class_name);
    cJSON_AddStringToObject(o, "model_name", c->model_name);
    cJSON_AddStringToObject(o, "model_pp_type", c->model_pp_type);
    cJSON_AddNumberToObject(o, "track_history_k", c->track_history_k);
    cJSON_AddNumberToObject(o, "max_miss", c->max_miss);
    cJSON_AddNumberToObject(o, "k_confirm", c->k_confirm);
    cJSON_AddNumberToObject(o, "window_minutes", c->window_minutes);
    cJSON_AddBoolToObject(o, "mqtt_report_enable", c->mqtt_report_enable);
    cJSON_AddBoolToObject(o, "webhook_report_enable", c->webhook_report_enable);
    cJSON_AddBoolToObject(o, "tracks_report_enable", c->tracks_report_enable);
    cJSON_AddBoolToObject(o, "heat_grid_enable", c->heat_grid_enable);
    cJSON_AddNumberToObject(o, "backlog_capacity", c->backlog_capacity);
    return o;
}

void people_counting_config_from_json(const cJSON* o, people_counting_config_t* c) {
    if (!o || !c) return;
    cJSON* j;
    if ((j = cJSON_GetObjectItem(o, "enable")))                  c->enable = cJSON_IsTrue(j);
    if ((j = cJSON_GetObjectItem(o, "line_x1_permille")))        c->line_x1_permille = (uint16_t)j->valueint;
    if ((j = cJSON_GetObjectItem(o, "line_y1_permille")))        c->line_y1_permille = (uint16_t)j->valueint;
    if ((j = cJSON_GetObjectItem(o, "line_x2_permille")))        c->line_x2_permille = (uint16_t)j->valueint;
    if ((j = cJSON_GetObjectItem(o, "line_y2_permille")))        c->line_y2_permille = (uint16_t)j->valueint;
    if ((j = cJSON_GetObjectItem(o, "outside_x_permille")))      c->outside_x_permille = (uint16_t)j->valueint;
    if ((j = cJSON_GetObjectItem(o, "outside_y_permille")))      c->outside_y_permille = (uint16_t)j->valueint;
    if ((j = cJSON_GetObjectItem(o, "conf_threshold_permille"))) c->conf_threshold_permille = (uint16_t)j->valueint;
    if ((j = cJSON_GetObjectItem(o, "max_dist_permille")))       c->max_dist_permille = (uint16_t)j->valueint;
    if ((j = cJSON_GetObjectItem(o, "target_class_name"))) {
        strncpy(c->target_class_name, j->valuestring, PC_TARGET_CLASS_NAME_LEN-1);
        c->target_class_name[PC_TARGET_CLASS_NAME_LEN-1] = '\0';
    }
    if ((j = cJSON_GetObjectItem(o, "model_name"))) {
        strncpy(c->model_name, j->valuestring, PC_MODEL_NAME_LEN-1);
        c->model_name[PC_MODEL_NAME_LEN-1] = '\0';
    }
    if ((j = cJSON_GetObjectItem(o, "model_pp_type"))) {
        strncpy(c->model_pp_type, j->valuestring, PC_PP_TYPE_LEN-1);
        c->model_pp_type[PC_PP_TYPE_LEN-1] = '\0';
    }
    if ((j = cJSON_GetObjectItem(o, "track_history_k")))         c->track_history_k = (uint8_t)j->valueint;
    if ((j = cJSON_GetObjectItem(o, "max_miss")))                c->max_miss = (uint8_t)j->valueint;
    if ((j = cJSON_GetObjectItem(o, "k_confirm")))               c->k_confirm = (uint8_t)j->valueint;
    if ((j = cJSON_GetObjectItem(o, "window_minutes")))          c->window_minutes = (uint16_t)j->valueint;
    if ((j = cJSON_GetObjectItem(o, "mqtt_report_enable")))      c->mqtt_report_enable = cJSON_IsTrue(j);
    if ((j = cJSON_GetObjectItem(o, "webhook_report_enable")))   c->webhook_report_enable = cJSON_IsTrue(j);
    if ((j = cJSON_GetObjectItem(o, "tracks_report_enable")))    c->tracks_report_enable = cJSON_IsTrue(j);
    if ((j = cJSON_GetObjectItem(o, "heat_grid_enable")))        c->heat_grid_enable = cJSON_IsTrue(j);
    if ((j = cJSON_GetObjectItem(o, "backlog_capacity")))        c->backlog_capacity = (uint16_t)j->valueint;
}
```

Hook these into the global `aicam_global_config_t` serializer (find the existing `global_config_to_json` / `global_config_from_json` and add `cJSON_AddItemToObject(root, "people_counting", people_counting_config_to_json(&cfg->people_counting));` and the reverse).

- [ ] **Step 5: Create model registry**

```c
/* Custom/Tasks/Inc/pc_model_registry.h */
#ifndef PC_MODEL_REGISTRY_H
#define PC_MODEL_REGISTRY_H
#include <stdint.h>
#include "aicam_types.h"

/* Returns model_ptr for a registered model name, or 0 if not found. */
uintptr_t pc_model_lookup(const char* name);

/* Register a model (name → embedded pointer). Called once at init. */
void pc_model_register(const char* name, uintptr_t model_ptr);

#endif
```

```c
/* Custom/Tasks/Src/pc_model_registry.c */
#include "pc_model_registry.h"
#include <string.h>

#define PC_MAX_MODELS 8
typedef struct { char name[64]; uintptr_t ptr; } pc_model_entry_t;
static pc_model_entry_t g_models[PC_MAX_MODELS];
static uint8_t g_n_models = 0;

void pc_model_register(const char* name, uintptr_t ptr) {
    if (g_n_models >= PC_MAX_MODELS) return;
    strncpy(g_models[g_n_models].name, name, 63);
    g_models[g_n_models].name[63] = '\0';
    g_models[g_n_models].ptr = ptr;
    g_n_models++;
}

uintptr_t pc_model_lookup(const char* name) {
    if (!name) return 0;
    for (uint8_t i = 0; i < g_n_models; ++i)
        if (strcmp(g_models[i].name, name) == 0) return g_models[i].ptr;
    return 0;
}
```

> **Phase-2 wiring:** when a new top-view model is trained, add ONE line `pc_model_register("my_topview_v2", &my_topview_v2_model_data);` at init. No algorithm code change. (See spec §11.)

- [ ] **Step 6: Build firmware, verify it compiles**

Run: `make app` (in Docker container or local toolchain)
Expected: builds clean (the new struct member may need the JSON serializer hooks; verify no missing symbols).

- [ ] **Step 7: Commit**

```bash
git add Custom/Common/Inc/aicam_types.h Custom/Core/System/json_config_mgr.{h,c} Custom/Core/System/json_config_json.c Custom/Tasks/Inc/pc_model_registry.h Custom/Tasks/Src/pc_model_registry.c
git commit -m "feat(pc): config struct, RO snapshot API, model registry"
```

---

## Task 6: Coordinator Skeleton

**Files:**
- Create: `Custom/Tasks/Inc/people_counting.h`
- Create: `Custom/Tasks/Src/people_counting.c` (skeleton — init only, no AI hook yet)

- [ ] **Step 1: Write the public header**

```c
/* Custom/Tasks/Inc/people_counting.h */
#ifndef PEOPLE_COUNTING_H
#define PEOPLE_COUNTING_H
#include "aicam_types.h"
#include "nn.h"   /* nn_result_t */

typedef struct {
    uint32_t window_in, window_out;
    uint32_t window_start_ts;
    uint32_t heat[16*16];
    uint32_t total_in, total_out;
    uint32_t boot_id;
    const char* boot_id_kind;   /* "rtc" or "monotonic" */
    uint32_t last_report_ts;
    uint32_t dropped_windows_mqtt;
    uint32_t dropped_windows_webhook;
} people_counting_stats_t;

aicam_result_t people_counting_init(void);
void people_counting_on_ai_result(const nn_result_t* result, uint32_t timestamp_ms);
const people_counting_stats_t* people_counting_get_stats(void);
void people_counting_reset_totals(void);

#endif
```

- [ ] **Step 2: Write skeleton implementation**

```c
/* Custom/Tasks/Src/people_counting.c */
#include "people_counting.h"
#include "pc_tracker.h"
#include "pc_line_cross.h"
#include "pc_model_registry.h"
#include "json_config_mgr.h"
#include "ai_service.h"
#include "mqtt_service.h"
#include "webhook_service.h"
#include "cmsis_os2.h"
#include <string.h>

/* CANONICAL g_pc struct definition — Tasks 9, 10, 11, 14, 15 add members here
 * rather than re-declaring the struct. Keep all runtime state in this one place. */
static struct {
    pc_tracker_t*           tracker;
    pc_line_cross_t*        line;
    people_counting_stats_t stats;
    osTimerId_t             window_timer;
    osMutexId_t             mutex;
    aicam_bool_t            inited;
    /* Task 9: pending track-record list for window reporting */
    pc_track_record_t**     pending;
    uint16_t                pending_count;
    uint16_t                pending_cap;
    /* Task 14: static backlog-drain buffer (16KB; NOT on timer task stack) */
    char                    drain_buf[16*1024];
    /* Task 10: cached window period for the timer (ms) */
    uint32_t                window_period_ms;
} g_pc;

/* window timer callback — implemented in Task 10 */
static void window_timer_cb(void* arg);  /* forward */

aicam_result_t people_counting_init(void) {
    if (g_pc.inited) return AICAM_OK;
    memset(&g_pc, 0, sizeof(g_pc));

    /* boot id */
    /* TODO Task 10: prefer RTC; fallback to osKernelGetTickCount() */
    g_pc.stats.boot_id = osKernelGetTickCount();
    g_pc.stats.boot_id_kind = "monotonic";

    g_pc.mutex = osMutexNew(NULL);
    if (!g_pc.mutex) return AICAM_ERROR_NO_MEMORY;

    /* register the stock person model (phase 1) */
    /* model_ptr source: Model/weights/yolov8n_256_quant_pc_uf_od_coco-person-st — verify
     * the symbol name with the Model packaging script. Placeholder: */
    extern const uint8_t yolov8n_256_quant_pc_uf_od_coco_person_st[];  /* from Model linkage */
    pc_model_register("yolov8n_256_quant_pc_uf_od_coco-person-st",
                      (uintptr_t)yolov8n_256_quant_pc_uf_od_coco_person_st);

    g_pc.inited = AICAM_TRUE;
    LOG_CORE_INFO("people_counting initialized");
    return AICAM_OK;
}

void people_counting_on_ai_result(const nn_result_t* result, uint32_t ts) {
    if (!g_pc.inited) return;
    const aicam_global_config_t* cfg = json_config_get_config_ro();
    if (!cfg->people_counting.enable) return;
    /* Full per-frame logic added in Task 8 */
}

const people_counting_stats_t* people_counting_get_stats(void) { return &g_pc.stats; }
void people_counting_reset_totals(void) {
    osMutexAcquire(g_pc.mutex, osWaitForever);
    g_pc.stats.total_in = 0; g_pc.stats.total_out = 0;
    osMutexRelease(g_pc.mutex);
    /* persist (Task 15) */
}

static void window_timer_cb(void* arg) { (void)arg; /* Task 10 */ }
```

- [ ] **Step 2b: Instrument spec §7.4 log events**

Spec §7.4 enumerates people-counting log events. They must be emitted at their defined lifecycle points across Tasks 6–15. The plan uses the **exact spec tag names** (do not rename). Reference table:

| Spec tag (§7.4)          | Severity | Emission point (Task)        | Payload to include |
|--------------------------|----------|------------------------------|--------------------|
| `PC_TRACKER_INIT`        | INFO     | first tracker/line create in on_ai_result (T8) | line L1/L2/outside coords |
| `PC_CONFIG_LOADED`       | INFO     | totals + config loaded at init (T6/T15) | total_in, total_out, window_minutes |
| `PC_CONFIG_UPDATED`      | INFO     | POST /api/people-counting/config success (T17) + detected runtime reload in on_ai_result (T8) | changed fields summary |
| `PC_WINDOW_REPORTED`     | INFO     | successful MQTT or Webhook publish in window_timer_cb (T14) | window_in, window_out, tracks_count, channel |
| `PC_WINDOW_BACKLOGGED`   | WARN     | window report pushed to backlog (either channel offline) (T14) | channel, backlog count |
| `PC_BACKLOG_DROPPED`     | WARN     | backlog capacity overflow → drop_oldest (T13) | channel, dropped seq |
| `PC_NVS_WRITE_FAILED`    | ERROR    | totals write to /config/pc_totals.json failure (T15) | lfs error code |
| `PC_LINE_INVALID`        | WARN     | runtime pc_line_cross_create returns NULL (degenerate line) on config reload (T8) | line coords |

**Action:** when implementing each listed task, add the corresponding `LOG_CORE_INFO` / `LOG_CORE_WARN` / `LOG_CORE_ERROR` call at the documented emission point using the exact tag string above. Example for T6 init:

```c
    g_pc.inited = AICAM_TRUE;
    LOG_CORE_INFO("PC_CONFIG_LOADED window_minutes=%u total_in=%u total_out=%u",
                  (unsigned)cfg->people_counting.window_minutes,
                  (unsigned)g_pc.stats.total_in, (unsigned)g_pc.stats.total_out);
    return AICAM_OK;
```

These `PC_*` tag strings are greppable for diagnostics and the §9 acceptance test log audit. Additional non-tagged `LOG_CORE_DEBUG` calls (e.g. per-track archive traces) are permitted but not required by the spec.

- [ ] **Step 3: Register init in core_init.c**

Find the init sequence in `Custom/Core/core_init.c` (or `service_init.c`) and add a call after services init:

```c
/* after service_init() succeeds */
#include "people_counting.h"
people_counting_init();
```

- [ ] **Step 4: Build, verify compiles**

Run: `make app`
Expected: clean build.

- [ ] **Step 5: Commit**

```bash
git add Custom/Tasks/Inc/people_counting.h Custom/Tasks/Src/people_counting.c Custom/Core/core_init.c
git commit -m "feat(pc): coordinator skeleton with init and stub callbacks"
```

---

## Task 7: AI Service Subscriber Registry

**Files:**
- Modify: `Custom/Services/AI/ai_service.h`, `Custom/Services/AI/ai_service.c`

- [ ] **Step 1: Add API to ai_service.h**

```c
/* ai_service.h — add after ai_service_get_nn_result decl */
typedef void (*ai_result_subscriber_t)(const nn_result_t *result, uint32_t timestamp_ms);
aicam_result_t ai_service_register_subscriber(ai_result_subscriber_t fn);
```

- [ ] **Step 2: Implement registry + notify in ai_service.c**

```c
/* ai_service.c — near top, after includes */
#define AI_MAX_SUBSCRIBERS 4
static ai_result_subscriber_t g_subscribers[AI_MAX_SUBSCRIBERS];

aicam_result_t ai_service_register_subscriber(ai_result_subscriber_t fn) {
    if (!fn) return AICAM_ERROR_INVALID_PARAM;
    for (int i = 0; i < AI_MAX_SUBSCRIBERS; ++i) {
        if (g_subscribers[i] == NULL) { g_subscribers[i] = fn; return AICAM_OK; }
    }
    return AICAM_ERROR_FULL;
}

static void notify_subscribers(const nn_result_t* r) {
    uint32_t ts = osKernelGetTickCount();
    for (int i = 0; i < AI_MAX_SUBSCRIBERS; ++i)
        if (g_subscribers[i]) g_subscribers[i](r, ts);
}
```

- [ ] **Step 3: Insert notify call at ai_service.c:501**

Locate the block in `ai_service_draw_callback`:
```c
aicam_result_t ai_ret = ai_service_get_nn_result(&nn_result, frame_id);
if (ai_ret == AICAM_OK && (nn_result.od.nb_detect > 0 || nn_result.mpe.nb_detect > 0)) {
```

**Change to:**
```c
aicam_result_t ai_ret = ai_service_get_nn_result(&nn_result, frame_id);
if (ai_ret == AICAM_OK) {
    notify_subscribers(&nn_result);   /* always notify, even on empty frames (track aging) */
    if (nn_result.od.nb_detect > 0 || nn_result.mpe.nb_detect > 0) {
        /* ... existing drawing code ... */
```

(Ensure the closing braces still match — the existing `if` body becomes nested.)

- [ ] **Step 4: Register the people counting callback (in people_counting_init, Task 6)**

Add to `people_counting_init()`:
```c
ai_service_register_subscriber(people_counting_on_ai_result);
```

- [ ] **Step 5: Build + flash + verify log "people_counting initialized"**

- [ ] **Step 6: Commit**

```bash
git add Custom/Services/AI/ai_service.{h,c}
git commit -m "feat(ai): subscriber registry for AI result consumers"
```

---

## Task 8: Per-Frame Tracking Wiring

**Files:**
- Modify: `Custom/Tasks/Src/people_counting.c` — flesh out `people_counting_on_ai_result`

- [ ] **Step 1: Implement the per-frame callback**

```c
void people_counting_on_ai_result(const nn_result_t* result, uint32_t ts) {
    if (!g_pc.inited || !result) return;
    const aicam_global_config_t* cfg_g = json_config_get_config_ro();
    const people_counting_config_t* cfg = &cfg_g->people_counting;
    if (!cfg->enable) return;
    if (result->type != PP_TYPE_OD) return;

    /* lazy init tracker/line if config changed (cheap re-init detection) */
    /* (Track config-reload properly in Task 5 RO snapshot; for now rebuild on every call if NULL) */
    if (!g_pc.tracker) {
        pc_tracker_config_t tc = {
            .max_dist_permille = cfg->max_dist_permille,
            .track_history_k   = cfg->track_history_k,
            .max_miss          = cfg->max_miss,
            .k_confirm         = cfg->k_confirm,
        };
        g_pc.tracker = pc_tracker_create(&tc, 1);
    }
    if (!g_pc.line) {
        g_pc.line = pc_line_cross_create(
            cfg->line_x1_permille/1000.0f, cfg->line_y1_permille/1000.0f,
            cfg->line_x2_permille/1000.0f, cfg->line_y2_permille/1000.0f,
            cfg->outside_x_permille/1000.0f, cfg->outside_y_permille/1000.0f);
    }

    /* collect detections matching target class + threshold */
    pc_point_t detects[64];
    uint8_t n = 0;
    float conf_thr = cfg->conf_threshold_permille / 1000.0f;
    for (uint8_t i = 0; i < result->od.nb_detect && n < 64; ++i) {
        const od_detect_t* d = &result->od.detects[i];
        if (d->conf < conf_thr) continue;
        if (!d->class_name || strcmp(d->class_name, cfg->target_class_name) != 0) continue;
        detects[n].x = d->x + d->width * 0.5f;
        detects[n].y = d->y + d->height * 0.5f;
        n++;
    }

    osMutexAcquire(g_pc.mutex, osWaitForever);
    pc_track_record_t** recs = NULL; uint16_t n_recs = 0;
    pc_tracker_update(g_pc.tracker, detects, n, ts, &recs, &n_recs);
    /* line crossing check */
    uint32_t win_in = 0, win_out = 0, tot_in = 0, tot_out = 0;
    pc_tracker_check_line_crossings(g_pc.tracker, g_pc.line, ts, &win_in, &win_out, &tot_in, &tot_out);
    g_pc.stats.window_in += win_in;  g_pc.stats.window_out += win_out;
    g_pc.stats.total_in  += tot_in;  g_pc.stats.total_out  += tot_out;
    /* heat grid (Task 12) */
    /* append recs to pending list (Task 9) */
    for (uint16_t k = 0; k < n_recs; ++k) PC_FREE(recs[k]);
    free(recs);
    osMutexRelease(g_pc.mutex);
}
```

> Note: PC_FREE/free of records here is temporary — Task 9 stores them in a pending list instead of freeing.

- [ ] **Step 2: Build + manual smoke test**

Flash, enable via Web (after Task 17), walk past the camera → verify via debug log that `window_in` increments.

- [ ] **Step 3: Commit**

```bash
git add Custom/Tasks/Src/people_counting.c
git commit -m "feat(pc): per-frame tracker update + line crossing wired to AI callback"
```

---

## Task 9: Track Record Pending List

**Files:**
- Modify: `Custom/Tasks/Src/people_counting.c`

- [ ] **Step 1: Add pending-list helpers (g_pc.pending/pending_count/pending_cap are already declared in the canonical Task 6 struct)**

```c
static void pending_push(pc_track_record_t* r) {
    if (g_pc.pending_count >= g_pc.pending_cap) {
        uint16_t newcap = g_pc.pending_cap ? g_pc.pending_cap * 2 : 16;
        pc_track_record_t** grown = realloc(g_pc.pending, newcap * sizeof(*grown));
        if (!grown) { PC_FREE(r); return; }
        g_pc.pending = grown; g_pc.pending_cap = newcap;
    }
    g_pc.pending[g_pc.pending_count++] = r;
}
```

Replace the `for (k...) PC_FREE(recs[k])` in Task 8 with `for (k...) pending_push(recs[k]);`.

- [ ] **Step 2: Commit**

```bash
git commit -am "feat(pc): pending track record list for window reporting"
```

---

## Task 10: Window Timer + Snapshot

**Files:**
- Modify: `Custom/Tasks/Src/people_counting.c`

- [ ] **Step 1: Implement window timer**

```c
static void window_timer_cb(void* arg) {
    (void)arg;
    if (!g_pc.inited) return;
    osMutexAcquire(g_pc.mutex, osWaitForever);
    uint32_t now = osKernelGetTickCount();
    /* window_period_ms is set at init from config; used here only for reporting duration_min */
    uint32_t duration_ms = g_pc.window_period_ms ? g_pc.window_period_ms : (5u * 60u * 1000u);

    /* snapshot active tracks → CROSSING records */
    pc_track_record_t** snap = NULL; uint16_t n_snap = 0;
    pc_tracker_window_snapshot(g_pc.tracker, now, &snap, &n_snap);
    for (uint16_t k = 0; k < n_snap; ++k) pending_push(snap[k]);
    free(snap);

    /* build + dispatch report (Task 11/14) */
    /* pc_report_build_and_dispatch(now); */  /* TODO Task 11 */

    /* reset window */
    g_pc.stats.window_in = 0; g_pc.stats.window_out = 0;
    g_pc.stats.window_start_ts = now;
    memset(g_pc.stats.heat, 0, sizeof(g_pc.stats.heat));
    /* free pending after dispatch (Task 11) */
    osMutexRelease(g_pc.mutex);
}

/* in people_counting_init, after config read: */
{
    const aicam_global_config_t* cfg = json_config_get_config_ro();
    g_pc.window_period_ms = cfg->people_counting.window_minutes * 60u * 1000u;
    osTimerAttr_t attr = { .name = "pc_win" };
    g_pc.window_timer = osTimerNew(window_timer_cb, osTimerPeriodic, NULL, &attr);
    if (g_pc.window_timer) osTimerStart(g_pc.window_timer, g_pc.window_period_ms);
}
```

(`g_pc.window_period_ms` is already declared in the canonical Task 6 struct.)

> **Runtime window-period change (M3 from review):** the timer is started once at init. If the user changes `window_minutes` via the POST /config endpoint, the new period takes effect on the next reboot by default. To make it take effect immediately, the Task 17 POST handler should, after `json_config_set_people_counting_config`, call `osTimerStop(g_pc.window_timer)` then `osTimerStart(g_pc.window_timer, g_pc.window_period_ms)` with the new value (add a small `people_counting_reload_window()` helper). This is a recommended enhancement; document it as deferred if not implemented in V1.

- [ ] **Step 2: Commit**

```bash
git commit -am "feat(pc): periodic window timer with track snapshot"
```

---

## Task 11: JSON Report Builder

**Files:**
- Modify: `Custom/Tasks/Src/people_counting.c`
- Add a static buffer `pc_json_buf[16*1024]`

- [ ] **Step 1: Implement build_report_json**

```c
static char pc_json_buf[16*1024];

static const char* SEG_TYPE_NAME[] = { "departed", "crossing" };

/* Builds the window report JSON into pc_json_buf. Returns length, 0 on error. */
static size_t build_report_json(uint32_t window_start, uint32_t window_end) {
    const aicam_global_config_t* cfg_g = json_config_get_config_ro();
    const people_counting_config_t* cfg = &cfg_g->people_counting;
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "people_counting");
    /* device_id — use a getter from device_info; placeholder: */
    cJSON_AddStringToObject(root, "device_id", "ne301-unknown");
    cJSON_AddNumberToObject(root, "boot_id", g_pc.stats.boot_id);
    cJSON_AddStringToObject(root, "boot_id_kind", g_pc.stats.boot_id_kind);

    cJSON* win = cJSON_CreateObject();
    cJSON_AddNumberToObject(win, "start_ms", window_start);
    cJSON_AddNumberToObject(win, "end_ms", window_end);
    cJSON_AddNumberToObject(win, "duration_min", cfg->window_minutes);
    cJSON_AddNumberToObject(win, "in", g_pc.stats.window_in);
    cJSON_AddNumberToObject(win, "out", g_pc.stats.window_out);
    cJSON_AddItemToObject(root, "window", win);

    cJSON* tot = cJSON_CreateObject();
    cJSON_AddNumberToObject(tot, "in", g_pc.stats.total_in);
    cJSON_AddNumberToObject(tot, "out", g_pc.stats.total_out);
    cJSON_AddItemToObject(root, "total", tot);

    /* tracks */
    cJSON* tracks = cJSON_CreateArray();
    if (cfg->tracks_report_enable) {
        for (uint16_t i = 0; i < g_pc.pending_count; ++i) {
            const pc_track_record_t* r = g_pc.pending[i];
            cJSON* trk = cJSON_CreateObject();
            cJSON_AddNumberToObject(trk, "track_id", r->track_id);
            cJSON_AddNumberToObject(trk, "segment_id", r->segment_id);
            cJSON_AddNumberToObject(trk, "entered_at_ms", r->entered_at_ms);
            cJSON_AddNumberToObject(trk, "seg_start_ms", r->seg_start_ms);
            cJSON_AddNumberToObject(trk, "seg_end_ms", r->seg_end_ms);
            cJSON_AddStringToObject(trk, "seg_end_type", SEG_TYPE_NAME[r->seg_end_type]);
            cJSON* evs = cJSON_CreateArray();
            if (r->events & PC_BIT_IN)  cJSON_AddItemToArray(evs, cJSON_CreateString("line_cross_in"));
            if (r->events & PC_BIT_OUT) cJSON_AddItemToArray(evs, cJSON_CreateString("line_cross_out"));
            cJSON_AddItemToObject(trk, "events", evs);
            cJSON* pts = cJSON_CreateArray();
            const uint32_t* pts_ts = pc_track_record_point_ts_const(r);
            for (uint8_t p = 0; p < r->nb_points; ++p) {
                cJSON* pt = cJSON_CreateArray();
                cJSON_AddItemToArray(pt, cJSON_CreateNumber((double)r->points[p].x));
                cJSON_AddItemToArray(pt, cJSON_CreateNumber((double)r->points[p].y));
                cJSON_AddItemToArray(pt, cJSON_CreateNumber((double)pts_ts[p]));
                cJSON_AddItemToArray(pts, pt);
            }
            cJSON_AddItemToObject(trk, "points", pts);
            cJSON_AddItemToArray(tracks, trk);
        }
    }
    cJSON_AddItemToObject(root, "tracks", tracks);

    /* heat_grid */
    if (cfg->heat_grid_enable) {
        cJSON* hg = cJSON_CreateObject();
        cJSON_AddNumberToObject(hg, "width", 16);
        cJSON_AddNumberToObject(hg, "height", 16);
        cJSON* data = cJSON_CreateArray();
        for (int i = 0; i < 16*16; ++i) cJSON_AddItemToArray(data, cJSON_CreateNumber(g_pc.stats.heat[i]));
        cJSON_AddItemToObject(hg, "data", data);
        cJSON_AddItemToObject(root, "heat_grid", hg);
    } else {
        cJSON_AddNullToObject(root, "heat_grid");
    }

    cJSON_AddNumberToObject(root, "dropped_windows_mqtt", g_pc.stats.dropped_windows_mqtt);
    cJSON_AddNumberToObject(root, "dropped_windows_webhook", g_pc.stats.dropped_windows_webhook);

    cJSON* line = cJSON_CreateObject();
    cJSON_AddNumberToObject(line, "x1", cfg->line_x1_permille/1000.0);
    cJSON_AddNumberToObject(line, "y1", cfg->line_y1_permille/1000.0);
    cJSON_AddNumberToObject(line, "x2", cfg->line_x2_permille/1000.0);
    cJSON_AddNumberToObject(line, "y2", cfg->line_y2_permille/1000.0);
    cJSON_AddItemToObject(root, "line", line);

    cJSON_AddStringToObject(root, "model", cfg->model_name);
    cJSON_AddStringToObject(root, "target_class", cfg->target_class_name);
    cJSON_AddNumberToObject(root, "conf_threshold_permille", cfg->conf_threshold_permille);

    char* printed = cJSON_PrintUnformatted(root);
    size_t len = strlen(printed);
    if (len < sizeof(pc_json_buf)) memcpy(pc_json_buf, printed, len+1);
    else len = 0;
    cJSON_free(printed);
    cJSON_Delete(root);
    return len;
}
```

- [ ] **Step 2: Commit**

```bash
git commit -am "feat(pc): window report JSON builder"
```

---

## Task 12: Heat Grid Accumulation

**Files:**
- Modify: `Custom/Tasks/Src/people_counting.c`

- [ ] **Step 1: Add a stable-track iterator to pc_tracker.h (spec §3.5 requires stable tracks only — raw detects leak noise)**

```c
/* Add to pc_tracker.h */
typedef void (*pc_track_visitor_t)(const pc_track_t* trk, void* user);
/* Visits all tracks with id != 0 AND age >= k_confirm. */
void pc_tracker_for_each_stable(const pc_tracker_t* t, pc_track_visitor_t fn, void* user);
```

```c
/* Add to pc_tracker.c */
void pc_tracker_for_each_stable(const pc_tracker_t* t, pc_track_visitor_t fn, void* user) {
    if (!t || !fn) return;
    for (uint16_t i = 0; i < PC_MAX_TRACKS; ++i) {
        const pc_track_t* trk = &t->tracks[i];
        if (trk->id != 0 && trk->age >= t->cfg.k_confirm) fn(trk, user);
    }
}
```

- [ ] **Step 2: Use the iterator for heat accumulation (NOT raw detects)**

```c
/* inside people_counting_on_ai_result, after line-crossings, inside mutex: */
if (cfg->heat_grid_enable) {
    struct { uint32_t* heat; } ctx = { .heat = g_pc.stats.heat };
    pc_tracker_for_each_stable(g_pc.tracker, [](const pc_track_t* trk, void* user) {
        /* C doesn't have lambdas — use a small file-local helper instead. */
    }, &ctx);
}

/* file-local helper, defined above people_counting_on_ai_result: */
static void heat_accumulate_cb(const pc_track_t* trk, void* user) {
    uint32_t* heat = (uint32_t*)user;
    /* use the latest history point (the current stable position) */
    pc_point_t p = trk->last_pos;
    int gx = (int)(p.x * 16.0f); if (gx > 15) gx = 15; if (gx < 0) gx = 0;
    int gy = (int)(p.y * 16.0f); if (gy > 15) gy = 15; if (gy < 0) gy = 0;
    heat[gy * 16 + gx]++;
}

/* and the call becomes: */
if (cfg->heat_grid_enable) {
    pc_tracker_for_each_stable(g_pc.tracker, heat_accumulate_cb, g_pc.stats.heat);
}
```

> **Note:** the snippet shows a lambda for illustration only — the actual implementation MUST use the file-local `heat_accumulate_cb` helper (C11 has no lambdas). The raw-detects fallback from earlier drafts is removed; it violated spec §3.5 ("stable tracks only").

- [ ] **Step 2: Commit**

```bash
git commit -am "feat(pc): heat grid accumulation (stable tracks)"
```

---

## Task 13: Backlog (LittleFS Per-Channel Queue)

**Files:**
- Create: `Custom/Tasks/Inc/pc_backlog.h`, `Custom/Tasks/Src/pc_backlog.c`

- [ ] **Step 1: Header**

```c
/* Custom/Tasks/Inc/pc_backlog.h */
#ifndef PC_BACKLOG_H
#define PC_BACKLOG_H
#include "aicam_types.h"

typedef enum { BACKLOG_MQTT = 0, BACKLOG_WEBHOOK = 1 } backlog_channel_t;
#define BACKLOG_CHANNEL_COUNT 2

const char* backlog_channel_name(backlog_channel_t ch);  /* "mqtt" / "webhook" */

aicam_result_t backlog_push(backlog_channel_t ch, const char* json, uint16_t capacity);
/* Pop oldest into buf (null-terminated). Returns AICAM_ERROR_NOT_FOUND if empty. */
aicam_result_t backlog_pop (backlog_channel_t ch, char* buf, size_t buf_len);
uint16_t       backlog_count(backlog_channel_t ch);
void           backlog_drop_oldest(backlog_channel_t ch);

#endif
```

- [ ] **Step 2: Implementation using LittleFS**

```c
/* Custom/Tasks/Src/pc_backlog.c */
#include "pc_backlog.h"
#include "lfs.h"
#include "storage.h"   /* for the lfs_t* accessor — verify name */
#include <stdio.h>
#include <string.h>

extern lfs_t* storage_get_lfs(void);   /* confirm accessor exists in storage.c */

static const char* CHNAME[] = { "mqtt", "webhook" };
const char* backlog_channel_name(backlog_channel_t ch) {
    return (ch < BACKLOG_CHANNEL_COUNT) ? CHNAME[ch] : "unknown";
}

static void make_path(char* buf, size_t blen, backlog_channel_t ch, uint32_t seq) {
    snprintf(buf, blen, "/pc_backlog/%s/%08lu.json", CHNAME[ch], (unsigned long)seq);
}

/* seq counter file: /pc_backlog/<ch>/_seq */
static uint32_t read_seq(backlog_channel_t ch) {
    char path[64]; snprintf(path, sizeof(path), "/pc_backlog/%s/_seq", CHNAME[ch]);
    lfs_t* lfs = storage_get_lfs(); if (!lfs) return 0;
    lfs_file_t f; uint32_t s = 0;
    if (lfs_file_open(lfs, &f, path, LFS_O_RDONLY) >= 0) {
        lfs_file_read(lfs, &f, &s, sizeof(s));
        lfs_file_close(lfs, &f);
    }
    return s;
}
static void write_seq(backlog_channel_t ch, uint32_t s) {
    char path[64]; snprintf(path, sizeof(path), "/pc_backlog/%s/_seq", CHNAME[ch]);
    lfs_t* lfs = storage_get_lfs(); if (!lfs) return;
    lfs_file_t f;
    if (lfs_file_open(lfs, &f, path, LFS_O_WRONLY | LFS_O_CREAT) >= 0) {
        lfs_file_write(lfs, &f, &s, sizeof(s));
        lfs_file_close(lfs, &f);
    }
}

aicam_result_t backlog_push(backlog_channel_t ch, const char* json, uint16_t capacity) {
    lfs_t* lfs = storage_get_lfs(); if (!lfs) return AICAM_ERROR_UNAVAILABLE;
    char path[64]; uint32_t seq = read_seq(ch);
    make_path(path, sizeof(path), ch, seq);
    lfs_file_t f;
    if (lfs_file_open(lfs, &f, path, LFS_O_WRONLY | LFS_O_CREAT) < 0) return AICAM_ERROR_IO;
    size_t len = strlen(json);
    lfs_file_write(lfs, &f, json, len);
    lfs_file_close(lfs, &f);
    write_seq(ch, seq + 1);

    /* enforce capacity: count and drop oldest */
    uint16_t cnt = backlog_count(ch);
    while (cnt > capacity) { backlog_drop_oldest(ch); cnt--; }
    return AICAM_OK;
}
```

(`backlog_pop` / `backlog_count` / `backlog_drop_oldest` follow the same pattern — list dir `/pc_backlog/<ch>/`, sort by filename (= seq), read/remove the first.)

> **Note:** the `storage_get_lfs()` accessor name needs verification. Check `Custom/Hal/storage.h` for the actual exported accessor (might be `storage_get_lfs()` or `system_get_lfs()` or accessed via a context struct). Adjust to match.

- [ ] **Step 3: Commit**

```bash
git add Custom/Tasks/Inc/pc_backlog.h Custom/Tasks/Src/pc_backlog.c
git commit -m "feat(pc): LittleFS backlog queue per channel (mqtt/webhook)"
```

---

## Task 14: Report Dispatch (MQTT + Webhook + Backlog Fallback)

**Files:**
- Modify: `Custom/Tasks/Src/people_counting.c`
- Modify: `Custom/Services/Webhook/webhook_service.{h,c}` — add `webhook_service_push_json`

- [ ] **Step 1: Add webhook_service_push_json**

```c
/* webhook_service.h */
aicam_result_t webhook_service_push_json(const char* url, const char* json, size_t len);
```

```c
/* webhook_service.c — implement reusing the existing HTTP client infra used by push_capture,
 * but POST the raw JSON with Content-Type: application/json (no image multipart). */
```

(Follow the existing `webhook_service_push_capture` HTTP path; swap the body and content-type.)

- [ ] **Step 2: Implement dispatch in window_timer_cb**

```c
/* replace the TODO in window_timer_cb: */
size_t jlen = build_report_json(window_start, window_end);
if (jlen == 0) { /* error */ }
else {
    const people_counting_config_t* cfg = &json_config_get_config_ro()->people_counting;
    /* MQTT — backlog drain buffer is STATIC, not on the timer task stack (16KB). */
    if (cfg->mqtt_report_enable) {
        if (mqtt_service_is_connected()) {
            char topic[64]; snprintf(topic, sizeof(topic), "device/%s/people-count", device_id_str());
            mqtt_service_publish_json(topic, pc_json_buf, 1, 0);
            while (backlog_count(BACKLOG_MQTT) > 0) {
                if (backlog_pop(BACKLOG_MQTT, g_pc.drain_buf, sizeof(g_pc.drain_buf)) == AICAM_OK)
                    mqtt_service_publish_json(topic, g_pc.drain_buf, 1, 0);
            }
        } else {
            if (backlog_push(BACKLOG_MQTT, pc_json_buf, cfg->backlog_capacity) != AICAM_OK)
                g_pc.stats.dropped_windows_mqtt++;
        }
    }
    /* Webhook — symmetric, same static drain buffer */
    if (cfg->webhook_report_enable) {
        webhook_config_t wc; json_config_get_webhook_config(&wc);
        if (wc.enable && wc.url[0]) {
            /* webhook has no persistent conn — always try */
            if (webhook_service_push_json(wc.url, pc_json_buf, jlen) == AICAM_OK) {
                while (backlog_count(BACKLOG_WEBHOOK) > 0) {
                    if (backlog_pop(BACKLOG_WEBHOOK, g_pc.drain_buf, sizeof(g_pc.drain_buf)) == AICAM_OK)
                        webhook_service_push_json(wc.url, g_pc.drain_buf, strlen(g_pc.drain_buf));
                }
            } else {
                if (backlog_push(BACKLOG_WEBHOOK, pc_json_buf, cfg->backlog_capacity) != AICAM_OK)
                    g_pc.stats.dropped_windows_webhook++;
            }
        }
    }
}
/* free pending */
for (uint16_t k = 0; k < g_pc.pending_count; ++k) PC_FREE(g_pc.pending[k]);
g_pc.pending_count = 0;
```

- [ ] **Step 3: Commit**

```bash
git commit -am "feat(pc): report dispatch via MQTT/Webhook with backlog fallback"
```

---

## Task 15: Totals Persistence

**Files:**
- Modify: `Custom/Tasks/Src/people_counting.c`

- [ ] **Step 1: Load on init, save each window**

```c
/* people_counting_init: after memset, load totals from /config/pc_totals.json */
{
    lfs_t* lfs = storage_get_lfs(); lfs_file_t f;
    if (lfs && lfs_file_open(lfs, &f, "/config/pc_totals.json", LFS_O_RDONLY) >= 0) {
        char buf[64]; lfs_file_read(lfs, &f, buf, sizeof(buf)-1); buf[sizeof(buf)-1]=0;
        lfs_file_close(lfs, &f);
        cJSON* o = cJSON_Parse(buf);
        if (o) {
            g_pc.stats.total_in  = cJSON_GetObjectItem(o, "total_in")->valueint;
            g_pc.stats.total_out = cJSON_GetObjectItem(o, "total_out")->valueint;
            cJSON_Delete(o);
        }
    }
}

/* window_timer_cb: after dispatch, persist totals */
{
    char buf[64]; snprintf(buf, sizeof(buf),
        "{\"total_in\":%lu,\"total_out\":%lu}",
        (unsigned long)g_pc.stats.total_in, (unsigned long)g_pc.stats.total_out);
    lfs_t* lfs = storage_get_lfs(); lfs_file_t f;
    if (lfs && lfs_file_open(lfs, &f, "/config/pc_totals.json", LFS_O_WRONLY|LFS_O_CREAT) >= 0) {
        lfs_file_write(lfs, &f, buf, strlen(buf));
        lfs_file_close(lfs, &f);
    }
}
```

(`reset_totals` should also write the file.)

- [ ] **Step 2: Commit**

```bash
git commit -am "feat(pc): persist total_in/out to /config/pc_totals.json"
```

---

## Task 16: Drawing Overlay

**Files:**
- Modify: `Custom/Core/Video/ai_draw_service.{c,h}`

- [ ] **Step 1: Add draw functions**

```c
/* ai_draw_service.h */
void ai_draw_count_line(uint8_t* fb, int w, int h,
                        float x1, float y1, float x2, float y2,
                        float outside_x, float outside_y);
void ai_draw_count_text(uint8_t* fb, int w, int h, int x, int y,
                        uint32_t window_in, uint32_t window_out);
```

```c
/* ai_draw_service.c — implement using existing draw_line_param_t / draw_printf_param_t
 * (draw.h). Convert normalized → pixels, draw red line + arrow toward inside,
 * draw "IN: X OUT: Y" text top-left. */
```

- [ ] **Step 2: Call from people_counting_on_ai_result**

After processing, if there's a framebuffer accessor (the AI callback doesn't get the fb — the draw callback does). **Alternative:** draw inside the existing `ai_draw_service` flow. Simplest: register a draw hook. For V1, draw the count line as part of the existing OD draw pass by checking a global flag. (Refine in this step — the cleanest path is to add the call inside `ai_service_draw_callback` after `notify_subscribers`.)

- [ ] **Step 3: Commit**

```bash
git commit -am "feat(pc): draw count line + IN/OUT text overlay"
```

---

## Task 17: Web API Module

**Files:**
- Create: `Custom/Services/Web/api/api_people_counting_module.{c,h}`
- Modify: `Custom/Services/Web/web_api.c` — register endpoints

- [ ] **Step 1: Implement endpoints following api_mqtt_module.c pattern**

Provide:
- `GET /api/people-counting/config` → returns `people_counting_config_to_json`
- `POST /api/people-counting/config` → parse body, validate ranges (§4.1), `json_config_set_people_counting_config`
- `GET /api/people-counting/stats` → JSON of `people_counting_stats_t`
- `POST /api/people-counting/reset` → call `people_counting_reset_totals()`
- `GET /api/people-counting/backlog` → `{mqtt: {count, oldest_seq}, webhook: {...}}`

**POST handler must validate before writing.** Spec §4.1 ranges:

```c
/* Inside POST /api/people-counting/config handler, after parsing body into
 * people_counting_config_t cfg: */
static aicam_result_t validate_pc_config(const people_counting_config_t* c, char* err, size_t err_len) {
    /* permille fields must be in [0, 1000] */
    #define IN_PM(v) ((v) <= 1000u)
    if (!IN_PM(c->line_x1_permille) || !IN_PM(c->line_y1_permille) ||
        !IN_PM(c->line_x2_permille) || !IN_PM(c->line_y2_permille) ||
        !IN_PM(c->outside_x_permille) || !IN_PM(c->outside_y_permille)) {
        snprintf(err, err_len, "line/outside coordinates must be in [0,1000] permille");
        return AICAM_ERROR_INVALID_PARAM;
    }
    /* line must be non-degenerate in normalized space (len >= 0.01) */
    float dx = (c->line_x2_permille - c->line_x1_permille) / 1000.0f;
    float dy = (c->line_y2_permille - c->line_y1_permille) / 1000.0f;
    if (dx*dx + dy*dy < 0.01f * 0.01f) {
        snprintf(err, err_len, "line is degenerate (L1≈L2)");
        return AICAM_ERROR_INVALID_PARAM;
    }
    /* outside point must NOT lie on the line (dot product != 0) */
    if (c->outside_x_permille == c->line_x1_permille &&
        c->outside_y_permille == c->line_y1_permille) {
        snprintf(err, err_len, "outside point coincides with L1");
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (c->conf_threshold_permille > 1000u) {
        snprintf(err, err_len, "conf_threshold_permille must be <= 1000");
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (c->max_dist_permille == 0u || c->max_dist_permille > 1000u) {
        snprintf(err, err_len, "max_dist_permille must be in (0, 1000]");
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (c->track_history_k < 4u || c->track_history_k > PC_K_MAX) {
        snprintf(err, err_len, "track_history_k must be in [4, %u]", PC_K_MAX);
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (c->k_confirm < 3u || c->k_confirm > c->track_history_k) {
        snprintf(err, err_len, "k_confirm must be in [3, track_history_k]");
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (c->max_miss == 0u) {
        snprintf(err, err_len, "max_miss must be >= 1");
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (c->window_minutes == 0u || c->window_minutes > 60u) {
        snprintf(err, err_len, "window_minutes must be in [1, 60]");
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (c->target_class_name[0] == '\0') {
        snprintf(err, err_len, "target_class_name must be non-empty");
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (c->backlog_capacity < 6u || c->backlog_capacity > 96u) {
        snprintf(err, err_len, "backlog_capacity must be in [6, 96]");
        return AICAM_ERROR_INVALID_PARAM;
    }
    return AICAM_OK;
}

/* Usage in handler:
 *   char err[128];
 *   if (validate_pc_config(&cfg, err, sizeof(err)) != AICAM_OK) {
 *       respond 400 with {"error": err};
 *       return;
 *   }
 *   json_config_set_people_counting_config(&cfg);
 *   respond 200 with the canonicalized config (re-serialized).
 */
```

- [ ] **Step 2: Register in web_api.c**

```c
/* web_api.c — in the endpoint registration section */
extern void api_people_counting_register(void);
/* ... */
api_people_counting_register();
```

- [ ] **Step 3: Test with curl**

```bash
curl http://<device-ip>/api/people-counting/config
curl -X POST http://<device-ip>/api/people-counting/config -d '{"enable":true,...}'
curl http://<device-ip>/api/people-counting/stats
```

- [ ] **Step 4: Commit**

```bash
git add Custom/Services/Web/api/api_people_counting_module.{c,h} Custom/Services/Web/web_api.c
git commit -m "feat(pc): REST API for config/stats/reset/backlog"
```

---

## Task 18: Web Frontend

**Files:**
- Create: `Web/src/pages/people-counting/{index,LineCanvas,ConfigPanel,StatsPanel,api}.{tsx,ts}`
- Modify: router + navigation

- [ ] **Step 1: api.ts** — fetch wrappers for the 5 endpoints
- [ ] **Step 2: LineCanvas.tsx** — mouse-click state machine (3 clicks: L1, L2, outside), overlay on the existing video preview component
- [ ] **Step 3: ConfigPanel.tsx** — form with all config fields, permille ↔ display conversions
- [ ] **Step 4: StatsPanel.tsx** — current window + totals + reset button
- [ ] **Step 5: index.tsx** — compose the three, poll stats every 5s
- [ ] **Step 6: Register route + nav item**
- [ ] **Step 7: Build + flash web assets** (`make web && make flash-web`)
- [ ] **Step 8: Manual test in browser**

- [ ] **Step 9: Commit**

```bash
git add Web/src/pages/people-counting/ Web/src/router Web/src/.../Navigation
git commit -m "feat(pc): web UI with line drawing, config, stats"
```

---

## Task 19: Makefile + Full Build

**Files:**
- Modify: `Appli/Makefile` (or the Custom include Makefile) — add new SRCS
- Modify: `Custom/Tasks/test/Makefile` — confirm `-D__PC_TEST__` flag

- [ ] **Step 1: Add firmware .c files to SRCS**

```makefile
# Add to the Custom sources list:
Custom/Tasks/Src/people_counting.c \
Custom/Tasks/Src/pc_tracker.c \
Custom/Tasks/Src/pc_line_cross.c \
Custom/Tasks/Src/pc_backlog.c \
Custom/Tasks/Src/pc_model_registry.c \
Custom/Services/Web/api/api_people_counting_module.c \
```

- [ ] **Step 2: Verify the firmware malloc name**

Grep the project for the malloc used in webhook_service / mqtt_service (likely `buffer_alloc` / `hal_mem_alloc` / `aos_malloc`). Update `PC_MALLOC` macro in `pc_types.h` firmware branch to match.

- [ ] **Step 3: Full build**

```bash
make            # builds all (FSBL + App + Web + Model)
```

Expected: clean build, signed binaries produced.

- [ ] **Step 4: Run PC unit tests once more**

```bash
cd Custom/Tasks/test && make run
```
Expected: 12 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add Appli/Makefile Custom/Tasks/test/Makefile Custom/Tasks/Inc/pc_types.h
git commit -m "build: wire people counting sources into firmware + verify tests"
```

---

## Task 20: Hardware Integration Test (Acceptance)

Run through spec §9 acceptance criteria 1-8 on real hardware. Document results in the commit message.

- [ ] **Step 1: Acceptance 1** — 10 walk-throughs, `in=10 out=10 ±1`
- [ ] **Step 2: Acceptance 2** — anti-bounce (stand + jitter + fast back-and-forth ≤ +2)
- [ ] **Step 3: Acceptance 3** — MQTT receives one JSON per 5 min with full fields
- [ ] **Step 4: Acceptance 4** — 10-min disconnect, reconnect → 2 backlog records within 60s
- [ ] **Step 5: Acceptance 5** — toggle tracks off → empty `tracks[]`, counts still increment
- [ ] **Step 6: Acceptance 6** — toggle heat grid on alone → 256-element array
- [ ] **Step 7: Acceptance 7** — reboot → totals restored, window cleared, track_ids reset
- [ ] **Step 8: Acceptance 8** — reset totals via Web → immediately zeroed + persisted

- [ ] **Step 9: Commit results**

```bash
git commit --allow-empty -m "test(pc): hardware acceptance — all 8 criteria verified"
```

---

## Known Refinements (flag for code review)

These are explicitly called out for the implementer to resolve during the build (verification against the actual codebase). Items 1, 2, 6, 7 from the prior review draft are resolved in this revision:

1. **~~prev_idx math in pc_tracker_check_line_crossings~~** — RESOLVED. Now `(now_idx + k - k_back) % k` with `k_back = min(k_confirm, history_used - 1)`. Verified by `test_tracker_real_back_and_forth_counts_one_each` and `test_tracker_cross_window_produces_crossing_then_departed`.
2. **~~Per-point timestamps in JSON~~** — RESOLVED. `pc_track_record_t` now carries a parallel `point_ts[]` array (via `pc_track_record_point_ts()` accessor); `archive_track` populates it; JSON builder emits `[x, y, ts]` triples.
3. **`storage_get_lfs()` accessor name** — verify against `Custom/Hal/storage.h`; adjust pc_backlog.c. Still pending codebase verification.
4. **Firmware malloc name** — verify and set in `PC_MALLOC` macro (Task 19 step 2). Still pending codebase verification.
5. **`device_id_str()` accessor** — find the actual function returning the device ID for the MQTT topic. Still pending codebase verification.
6. **~~Heat grid: stable tracks vs raw detects~~** — addressed in Task 12: implementer adds `pc_tracker_for_each_stable` iterator and uses it; raw-detects fallback documented as not acceptable for §6 contract.
7. **Model pointer symbol name** — `yolov8n_256_quant_pc_uf_od_coco_person_st` symbol must match what the Model packaging script emits. Verify in Model/Makefile at implementation time.
8. **Drawing hook placement** — the framebuffer is owned by the draw callback; Task 16 specifies the chosen path: draw inside `ai_service_draw_callback` after `notify_subscribers`, gated on `people_counting.enable`.
9. **~~Spec §7.4 log events~~** — RESOLVED. Task 6 Step 2b enumerates all `PC_*` log tags and their emission points; each task adds the corresponding `LOG_CORE_*` call.
10. **~~16KB drain buffer on timer stack~~** — RESOLVED. Moved to `g_pc.drain_buf[16*1024]` static (Task 9 struct + Task 14 dispatch).
11. **~~Task 17 config validation~~** — RESOLVED. `validate_pc_config()` added with full §4.1 range checks.
12. **~~`g_pc` struct scattered~~** — RESOLVED. Task 6 now holds the canonical struct definition including `pending`, `pending_count`, `pending_cap`, `drain_buf`, and `window_period_ms`. Tasks 9/10 reference it via comments instead of re-declaring.
13. **`window_minutes` validation** — plan accepts the continuous range [1,60]; spec §4.1 lists the discrete set {1,5,15,30,60}. The continuous range is a deliberate, documented relaxation (functionally harmless since the timer computes `window_minutes * 60 * 1000` ms). Tighten to the discrete set only if a UI dropdown requires it.
14. **~~Anti-bounce §8 case 4 literal assertion~~** — documented in Task 4 Step 5 note: the spec's "in=0 out=0 for a brief dip" is unreachable with the §3.2 algorithm (IN fires on the dip's first far-side frame). Surfaced for spec owner; covered in spirit by `test_tracker_small_jitter_on_one_side_no_count` (case 2) and `test_tracker_real_back_and_forth_counts_one_each` (case 3).
