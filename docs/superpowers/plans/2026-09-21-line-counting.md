# Line Counting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the People Counting-specific implementation with a generic single-target Line Counting application that consumes the current OD model, exposes normalized REST/UI contracts, and reliably persists immutable MQTT/Webhook reports across reboot.

**Architecture:** Extend the existing NN/video AI/AI Service runtime instead of creating a second model subsystem. Keep tracking/line-crossing as a model-agnostic `lc_*` engine, put target/model/window/report semantics in the Line Counting app layer, and use one crash-consistent persistent delivery queue with per-transport delivery state. Preserve old People Counting storage/API/routes only as one-way migration or compatibility adapters.

**Tech Stack:** STM32N6 C firmware, CMSIS-RTOS2, cJSON, LittleFS/storage APIs, existing MQTT/Webhook services, React + TypeScript + Vite + Vitest.

**Spec:** `docs/superpowers/specs/2026-09-21-line-counting-design.md`

## Global Constraints

- V1 has one Line Counting instance and one selected target class.
- Only `PP_TYPE_OD` models with usable selectable class metadata are supported.
- Line Counting never loads or switches models; current model ownership stays in existing AI Runtime/Model Management.
- Every successful inference, including zero detections, reaches Line Counting.
- Inference hot path must not parse model JSON; unchanged-model path performs only O(1) generation comparison before filtering/tracking.
- Model replacement is not a business/session boundary: preserve current window, totals, window start, and report schedule when target remains valid.
- Changing target class is a complete session reset and durably clears the delivery queue.
- Public REST uses normalized `[0,1]` values; permille remains an internal representation.
- MQTT/Webhook report time is ISO 8601 UTC `YYYY-MM-DDTHH:mm:ss.sssZ`; internal timing remains monotonic.
- A report is immutable after creation and is shared by MQTT and Webhook; only per-transport delivery state changes.
- MQTT topic and Webhook endpoint come from their existing transport configuration; Line Counting must not define another topic/endpoint.
- Delivery backlog is persistent, crash-consistent, count-bounded and byte-bounded.
- Storage/reporting failure must not stop counting.
- Do not introduce `LC_REALLOC` or assume `hal_mem_realloc` copies old contents.
- “过线统计” lives under “应用管理”, at the same level as MQTT/MQTTS and Webhook.

## Review Focus

- A successful OD inference with zero detections must increment misses and retire stale tracks rather than leaving them immortal; pin this in Task 3.
- A model reload to another OD model containing the same target must reset tracker state without resetting the active window/totals; pin this in Task 5.
- Power loss while appending or updating persistent delivery state must recover only complete CRC-valid records and must not resend already delivered transports; pin this in Task 6.
- A config POST that fails validation or resource preparation must leave the old persisted and active configuration intact; pin this in Task 8.
- A report generated while wall-clock time is invalid must contain `clock_valid=false` and null UTC fields, never uptime or epoch-zero placeholders; pin this in Task 7.

---

## File Structure / Ownership Map

Firmware runtime/model:
- Modify `Custom/Hal/nn.h`, `Custom/Hal/nn.c`: intrinsic model metadata only.
- Modify `Custom/Core/Video/video_ai_node.h`, `Custom/Core/Video/video_ai_node.c`: stable current-model class metadata and successful-inference fan-out source.
- Modify `Custom/Services/AI/ai_service.h`, `Custom/Services/AI/ai_service.c`: read-only runtime model view, class-list copy APIs, generation, subscribers.

Generic counting engine:
- Create `Custom/Tasks/Inc/lc_types.h`, `lc_tracker.h`, `lc_line_cross.h`.
- Create `Custom/Tasks/Src/lc_tracker.c`, `lc_line_cross.c`.
- Retire `pc_types.*`, `pc_tracker.*`, `pc_line_cross.*` after consumers migrate.

Application/reporting:
- Create `Custom/Tasks/Inc/line_counting.h`, `lc_delivery_queue.h`.
- Create `Custom/Tasks/Src/line_counting.c`, `lc_delivery_queue.c`.
- Retire `people_counting.*`, `pc_model_registry.*`, and `pc_backlog.*` after adapters/migration no longer need them.
- Modify existing task/build source lists wherever the current `pc_*` sources are registered.

Configuration/API:
- Modify the existing JSON config declarations/manager/NVS files containing `people_counting_config_t` and its save/load/default code; locate exact declaration with `rg "people_counting_config_t" Custom/Core/System`.
- Create `Custom/Services/Web/api/api_line_counting_module.c/.h`.
- Modify `Custom/Services/Web/api/api_people_counting_module.c/.h` into a compatibility adapter.
- Modify API registration/build lists to register the new canonical routes.

Web:
- Create `Web/src/pages/applicationManagement/line-counting-module.tsx` and focused child components under `Web/src/pages/applicationManagement/lineCounting/`.
- Create `Web/src/services/api/line-counting.ts`.
- Modify `Web/src/pages/applicationManagement/index.tsx` for the sibling tabs.
- Modify `Web/src/router/index.tsx` and navigation/layout definitions so old `/people-counting` redirects and the top-level People Counting entry disappears.
- Retire `Web/src/pages/peopleCounting/*` once equivalent functionality is covered.
- Add `Web/src/test/lineCounting*.test.tsx` / `.test.ts`.

Host firmware tests:
- Reuse the repository's existing People Counting host-test harness/build target. Before Task 2, locate it with `find . -iname '*people*count*test*' -o -iname '*pc_*test*'` and migrate it to `lc_*`; do not invent a second test framework.

---

### Task 1: Extend AI Runtime model metadata and class ownership

**Files:**
- Modify: `Custom/Hal/nn.h`
- Modify: `Custom/Hal/nn.c`
- Modify: `Custom/Core/Video/video_ai_node.h`
- Modify: `Custom/Core/Video/video_ai_node.c`
- Modify: `Custom/Services/AI/ai_service.h`
- Modify: `Custom/Services/AI/ai_service.c`
- Modify/Test: existing AI/model host tests if present; otherwise add a focused host C test beside the existing NN/AI test harness.

**Interfaces:**
- Produces:
  ```c
  typedef struct {
      aicam_bool_t loaded;
      nn_state_t nn_state;
      char name[64];
      char version[32];
      char model_type[32];
      char postprocess_type[32];
      pp_type_t result_type;
      uint16_t num_classes;
      uint32_t generation;
  } ai_model_runtime_info_t;

  aicam_result_t ai_get_model_runtime_info(ai_model_runtime_info_t *info);
  aicam_result_t ai_get_model_class_count(uint16_t *count);
  aicam_result_t ai_get_model_class_name(uint16_t index, char *buf, size_t buf_size);
  ```
- Later tasks consume these APIs; callers receive copies, never postprocessor-owned pointers.

- [ ] **Step 1: Add failing metadata parsing tests**

Use a model JSON fixture containing:
```json
{
  "model_info": {"name":"test_od","version":"1.0","type":"OBJECT_DETECTION"},
  "postprocess_type":"pp_od_yolo_v11_ui",
  "postprocess_params":{"num_classes":2,"class_names":["person","car"]}
}
```
Assert `model_type == "OBJECT_DETECTION"`, `num_classes == 2`, class count is 2, and names are copied as `person`, `car`.

- [ ] **Step 2: Run the focused test and verify failure**

Run the existing host test command discovered for NN/AI parsing. Expected: FAIL because `model_type`, `num_classes`, and class-list APIs do not exist.

- [ ] **Step 3: Parse intrinsic metadata in `nn.c::load_info()`**

Add only:
```c
char model_type[32];
uint16_t num_classes;
```
to `nn_model_info_t`; parse `model_info.type` and `postprocess_params.num_classes` with bounded copies/range checks. Do not add `char **class_names` to `nn_model_info_t`.

- [ ] **Step 4: Give the active AI runtime stable class-list storage**

In the active video-AI/runtime state, copy selectable class names during successful model load. Clear them on unload/failure. Enforce a compile-time class-count/name-length bound consistent with supported model metadata; reject inconsistent metadata rather than exposing partial lists.

- [ ] **Step 5: Add generation semantics**

Increment runtime `generation` only after a successful load/reload has fully installed model info + class list. Failed reload leaves the prior generation unchanged.

- [ ] **Step 6: Implement read-only AI Service APIs**

Implement the three APIs above. `ai_get_model_class_name()` copies into caller buffer and returns invalid-param/out-of-range errors deterministically.

- [ ] **Step 7: Add invalid metadata tests**

Exercise zero classes, count/list mismatch, out-of-range class lookup, failed reload, and unload. Expected: no stale class pointers/data; failed reload does not advance generation.

- [ ] **Step 8: Run focused tests and firmware compile**

Run host tests plus:
```bash
export STEDGEAI_CORE_DIR=/Applications/ST/STEdgeAI/4.0
export PATH="/Applications/ST/STEdgeAI/4.0/Utilities/macarm:$PATH"
make STEDGEAI_VARIANT=4.0 pkg
```
Expected: PASS/build succeeds.

- [ ] **Step 9: Commit**

```bash
git add Custom/Hal/nn.* Custom/Core/Video/video_ai_node.* Custom/Services/AI/ai_service.*
git commit -m "feat: expose current AI model runtime metadata"
```

### Task 2: Rename and harden the generic Line Counting engine

**Files:**
- Create: `Custom/Tasks/Inc/lc_types.h`
- Create: `Custom/Tasks/Inc/lc_tracker.h`
- Create: `Custom/Tasks/Inc/lc_line_cross.h`
- Create: `Custom/Tasks/Src/lc_tracker.c`
- Create: `Custom/Tasks/Src/lc_line_cross.c`
- Test: migrate existing People Counting host tracker/line tests to `lc_*`.

**Interfaces:**
- Produces model-agnostic `lc_point_t`, `lc_tracker_t`, `lc_line_cross_t`, `lc_track_record_t`.
- `lc_tracker_update(..., n_detects=0, ...)` is a valid call.

- [ ] **Step 1: Migrate existing tracker/line tests to `lc_*` names and add regression cases**

Add tests for deterministic greedy association, alternating IN/OUT anti-bounce, zero-detection miss retirement, age saturation beyond 255 updates, and record-array growth preserving old pointers/content.

- [ ] **Step 2: Run tests and verify they fail before new files exist**

Expected: compile/link failure for missing `lc_*` symbols.

- [ ] **Step 3: Port pure algorithm types without `LC_REALLOC`**

Define:
```c
#define LC_MALLOC(sz) hal_mem_alloc_any(sz)
#define LC_FREE(p)    hal_mem_free(p)
```
with libc equivalents under the host-test define. There must be no realloc macro.

- [ ] **Step 4: Port tracker and line-crossing implementation**

Preserve current centroid/greedy/ring-history/anti-bounce behavior. Remove unused `n_active`; compute active count from track slots. Saturate age:
```c
if (trk->age != UINT8_MAX) {
    trk->age++;
}
```

- [ ] **Step 5: Keep explicit allocate-copy-free expansion**

Where output record arrays grow:
```c
new_records = LC_MALLOC(new_count * sizeof(*new_records));
memcpy(new_records, old_records, old_count * sizeof(*old_records));
LC_FREE(old_records);
```
Handle allocation failure without losing the original array.

- [ ] **Step 6: Run host tests**

Expected: all migrated engine tests PASS, including >255-frame age and zero-detection retirement.

- [ ] **Step 7: Commit**

```bash
git add Custom/Tasks/Inc/lc_* Custom/Tasks/Src/lc_* <migrated-test-paths>
git commit -m "refactor: generalize line counting engine"
```

### Task 3: Move inference fan-out to the successful-inference path

**Files:**
- Modify: `Custom/Core/Video/video_ai_node.c`
- Modify: `Custom/Services/AI/ai_service.c`
- Modify: `Custom/Services/AI/ai_service.h`
- Test: existing AI Service callback tests or a focused host test.

**Interfaces:**
- Produces:
  ```c
  typedef void (*ai_result_subscriber_t)(const nn_result_t *result,
                                         uint32_t timestamp_ms,
                                         void *user_data);
  aicam_result_t ai_result_subscribe(ai_result_subscriber_t cb, void *user_data);
  aicam_result_t ai_result_unsubscribe(ai_result_subscriber_t cb, void *user_data);
  ```
  Exact existing names may be retained if already public; semantics are what change.
- Consumes successful-inference callback from `video_ai_node`.

- [ ] **Step 1: Write failing zero-detection subscriber test**

Simulate successful `PP_TYPE_OD` inference with `nb_detect=0`. Assert subscriber called exactly once.

- [ ] **Step 2: Run and verify failure**

Expected: old draw-path subscriber does not fire for zero detections.

- [ ] **Step 3: Fan out immediately after successful inference**

Wire AI Service subscriber notification to the existing `video_ai_result_callback_t` path. Keep callback nonblocking and document that subscribers must copy required data before returning.

- [ ] **Step 4: Remove subscriber notification from draw-only condition**

Drawing may still skip empty results, but subscriber delivery must not depend on draw count.

- [ ] **Step 5: Add non-OD and ordinary OD tests**

Assert one callback per successful inference, no duplicate callback from drawing, and no callback for inference failure.

- [ ] **Step 6: Run tests/build and commit**

```bash
git add Custom/Core/Video/video_ai_node.c Custom/Services/AI/ai_service.*
git commit -m "fix: publish every successful AI inference"
```

### Task 4: Introduce canonical Line Counting configuration and one-way migration

**Files:**
- Modify: config declaration header containing `people_counting_config_t`
- Modify: `Custom/Core/System/json_config_nvs.c`
- Modify: corresponding JSON config manager/default/serialization source files found by `rg "people_counting" Custom/Core/System`
- Test: existing JSON config host tests or focused config serialization test.

**Interfaces:**
- Produces `line_counting_config_t` with internal permille representation.
- Produces canonical load/save accessors used by Line Counting app and REST.
- Old People Counting config is read only for migration when canonical config is absent.

- [ ] **Step 1: Write failing canonical-default and migration tests**

Verify default `counter_name == "客流统计"`; old person config maps line/tracker/report fields; old editable model fields are not copied into canonical config.

- [ ] **Step 2: Run and verify failure**

Expected: canonical type/key/accessors missing.

- [ ] **Step 3: Add `line_counting_config_t` and defaults**

Use the exact fields from the design spec; do not include model name/type/generation/classes/runtime state.

- [ ] **Step 4: Add canonical NVS/config serialization**

Persist only the new generic schema/key. Keep internal permille fields internal.

- [ ] **Step 5: Add one-way migration**

Pseudo-flow:
```c
if (load_line_counting_config(&cfg) == NOT_FOUND) {
    if (load_people_counting_config(&old) == OK) {
        map_people_to_line(&old, &cfg);
        save_line_counting_config(&cfg);
    } else {
        cfg = line_counting_defaults();
    }
}
```
Never dual-write the old key.

- [ ] **Step 6: Add migration idempotence test**

Once canonical config exists, changing the old key must not affect subsequent canonical loads.

- [ ] **Step 7: Run config tests/build and commit**

```bash
git add Custom/Core/System
git commit -m "feat: add canonical line counting configuration"
```

### Task 5: Build the Line Counting application state machine and statistics

**Files:**
- Create: `Custom/Tasks/Inc/line_counting.h`
- Create: `Custom/Tasks/Src/line_counting.c`
- Modify: task initialization/build lists that currently initialize `people_counting`
- Test: migrate/add host application tests.

**Interfaces:**
- Consumes Task 1 AI runtime APIs and Task 2 engine.
- Produces:
  ```c
  typedef enum {
      LC_STATE_DISABLED,
      LC_STATE_RUNNING,
      LC_STATE_UNSUPPORTED_MODEL,
      LC_STATE_TARGET_CLASS_INVALID
  } line_counting_state_t;

  aicam_result_t line_counting_init(void);
  void line_counting_on_ai_result(const nn_result_t *result, uint32_t timestamp_ms);
  aicam_bool_t line_counting_is_enabled(void);
  aicam_result_t line_counting_get_status(...);
  aicam_result_t line_counting_get_stats(...);
  aicam_result_t line_counting_get_events(...);
  aicam_result_t line_counting_apply_config(const line_counting_config_t *candidate);
  aicam_result_t line_counting_reset(void);
  ```
  Final output structs must be owned/copy-safe and not expose mutable internal pointers.

- [ ] **Step 1: Write failing state-transition tests**

Cover disabled, OD+valid target, non-OD, missing class metadata, target invalid, and return to valid model.

- [ ] **Step 2: Add the cached model binding**

Maintain:
```c
typedef struct {
    uint32_t model_generation;
    pp_type_t result_type;
    int16_t target_class_index;
    char target_class_name[32];
} lc_model_binding_t;
```
On unchanged generation, do not query/reparse the class list.

- [ ] **Step 3: Filter detections and always update tracker**

For OD results, select only matching target class + confidence and pass bbox centers. For zero matches:
```c
lc_tracker_update(g_lc.tracker, NULL, 0, timestamp_ms, ...);
```

- [ ] **Step 4: Implement model-change semantics**

When generation changes:
- re-read runtime info/classes;
- reset tracker/transient pending/events;
- preserve window counters, totals, window start, report schedule;
- enter RUNNING if target exists;
- otherwise enter unsupported/target-invalid without mutating persisted target.

- [ ] **Step 5: Add the model-reload preservation regression test**

Seed window/total counts, reload to a new generation that still contains the target, then assert tracker is fresh while window/total/start remain unchanged.

- [ ] **Step 6: Implement recent-event ring**

Capacity exactly 50; newest API ordering can be produced at read time. Clear on model change, target reset, manual reset, and reboot/init.

- [ ] **Step 7: Implement window clock and disable semantics**

Runtime pause does not pause the window timer. User disable closes the current partial window, preserves totals/backlog, then stops new windows. Re-enable starts a fresh window at current monotonic time.

- [ ] **Step 8: Persist totals separately**

Migrate old `/config/pc_totals.json` only when new totals do not exist. Thereafter write only canonical Line Counting totals. Reboot restores totals but starts a zero current window.

- [ ] **Step 9: Run application tests/build and commit**

```bash
git add Custom/Tasks Custom/Core/System <build-list-files>
git commit -m "feat: add generic line counting application"
```

### Task 6: Implement the crash-consistent Persistent Delivery Queue

**Files:**
- Create: `Custom/Tasks/Inc/lc_delivery_queue.h`
- Create: `Custom/Tasks/Src/lc_delivery_queue.c`
- Test: focused host tests with a fake storage backend or existing storage test harness.
- Later retire: `Custom/Tasks/Inc/pc_backlog.h`, `Custom/Tasks/Src/pc_backlog.c`.

**Interfaces:**
- Produces:
  ```c
  typedef enum {
      LC_DELIVERY_NOT_REQUIRED = 0,
      LC_DELIVERY_PENDING,
      LC_DELIVERY_DELIVERED
  } lc_delivery_state_t;

  typedef struct {
      uint32_t boot_id;
      uint32_t report_seq;
      lc_delivery_state_t mqtt;
      lc_delivery_state_t webhook;
  } lc_delivery_meta_t;

  aicam_result_t lc_delivery_queue_init(...);
  aicam_result_t lc_delivery_queue_enqueue(const lc_delivery_meta_t *meta,
                                           const char *payload,
                                           size_t payload_len);
  aicam_result_t lc_delivery_queue_peek_oldest(...);
  aicam_result_t lc_delivery_queue_mark_mqtt_delivered(...);
  aicam_result_t lc_delivery_queue_mark_webhook_delivered(...);
  aicam_result_t lc_delivery_queue_clear(void);
  ```
- Queue enforces both configured record count and fixed system byte cap.

- [ ] **Step 1: Write failing persistence tests**

Test enqueue/reopen, independent MQTT/Webhook state, FIFO, count overflow drop-oldest, byte overflow drop-oldest, and clear/reopen remains empty.

- [ ] **Step 2: Add torn-write/CRC tests**

Inject failure after header, mid-payload, before commit marker, and during delivery-state update. Recovery must expose only complete CRC-valid records.

- [ ] **Step 3: Define on-storage record format**

Use explicit magic/version/header length/payload length/CRC/delivery bits/commit marker. Never overwrite immutable payload. Delivery-state mutation must itself be recoverable (journaled metadata or append-only state record); do not rely on an in-place single-byte write being atomic.

- [ ] **Step 4: Implement recovery scan**

On init, scan bounded storage, discard incomplete tail/corrupt records, reconstruct live FIFO and byte/count usage, and preserve pending transport state.

- [ ] **Step 5: Implement enqueue and eviction**

Before enqueue, evict oldest live records until both:
```text
count < backlog_capacity
bytes + new_record_bytes <= persistent_backlog_max_bytes
```
Increment transport dropped counters for pending obligations that are evicted.

- [ ] **Step 6: Implement independent durable delivery state**

MQTT success must not erase a Webhook-pending record. Reclaim only when all required transports are delivered/not-required.

- [ ] **Step 7: Run persistence tests including reopen after every mutation**

Expected: PASS with no duplicate resurrection of delivered obligations.

- [ ] **Step 8: Commit**

```bash
git add Custom/Tasks/Inc/lc_delivery_queue.h Custom/Tasks/Src/lc_delivery_queue.c <queue-tests>
git commit -m "feat: persist line counting delivery queue"
```

### Task 7: Generate immutable ISO-8601 reports and drive existing transports

**Files:**
- Modify: `Custom/Tasks/Src/line_counting.c`
- Modify: `Custom/Tasks/Inc/line_counting.h` if report helpers need public test seams.
- Read/use only: `Custom/Services/MQTT/mqtt_service.h`, `Custom/Services/Webhook/webhook_service.h`.
- Test: report serialization + delivery worker tests.

**Interfaces:**
- Consumes Task 6 queue.
- Produces one immutable JSON payload per closed window.
- Uses existing MQTT topic configuration and existing Webhook endpoint; no Line Counting topic/URL fields.

- [ ] **Step 1: Write failing report-schema tests**

Assert `schema_version=1`, `type=line_counting`, `device_id`, `boot_id`, `report_seq`, counter name, window/total, target, current model name/version, normalized line/config, and optional tracks/heat blocks.

- [ ] **Step 2: Add clock-valid/invalid tests**

For valid UTC, require exact `YYYY-MM-DDTHH:mm:ss.sssZ`. For invalid clock:
```json
{"clock_valid":false,"reported_at":null}
```
and `window.start_time/end_time == null`. Assert serialized output contains neither `1970-01-01` nor uptime values in UTC fields.

- [ ] **Step 3: Implement report sequence and snapshot generation**

Allocate `report_seq` once when report is created. Capture current model at report generation time only. Do not record model switch history or generation.

- [ ] **Step 4: Persist before transport attempt**

Required ordering:
```text
serialize immutable payload
→ durable queue enqueue
→ publish/post attempts
```
If enqueue fails, increment dropped/storage-fault accounting and continue counting.

- [ ] **Step 5: Set delivery obligations from switches at generation time**

If MQTT enabled, state=PENDING; otherwise NOT_REQUIRED. Same for Webhook. Later enabling a previously disabled transport does not retroactively alter historical NOT_REQUIRED reports.

- [ ] **Step 6: Implement drain behavior**

When a transport is disabled, do not drain its pending records. When re-enabled, resume oldest pending record. Successful MQTT/Webhook delivery updates only that transport state.

- [ ] **Step 7: Verify no topic construction exists**

Search:
```bash
rg "line-counting|line_counting|people-count" Custom/Tasks/Src/line_counting.c
```
Expected: payload type/name references are present, but no hard-coded MQTT topic path. Publishing calls consume existing MQTT configuration.

- [ ] **Step 8: Run tests/build and commit**

```bash
git add Custom/Tasks/Src/line_counting.c Custom/Tasks/Inc/line_counting.h <report-tests>
git commit -m "feat: report line counting windows reliably"
```

### Task 8: Add canonical normalized REST API with atomic config apply

**Files:**
- Create: `Custom/Services/Web/api/api_line_counting_module.h`
- Create: `Custom/Services/Web/api/api_line_counting_module.c`
- Modify: Web API module registration/build lists.
- Test: existing API handler tests or focused host tests.

**Interfaces:**
- Produces:
  - `GET/POST /api/v1/apps/line-counting/config`
  - `GET /api/v1/apps/line-counting/status`
  - `GET /api/v1/apps/line-counting/stats`
  - `GET /api/v1/apps/line-counting/events`
  - `POST /api/v1/apps/line-counting/reset`

- [ ] **Step 1: Write failing normalized config GET/POST tests**

GET must return floats `0..1`, nested `line`, `tracking`, `reporting`, and no `*_permille`. POST must convert normalized values to internal permille deterministically.

- [ ] **Step 2: Add validation tests**

Exercise out-of-range normalized values, degenerate line, invalid tracker bounds, invalid backlog capacity, malformed UTF-8/overlong names, and a new target absent from current selectable classes. The latter returns HTTP 422 / `invalid_target_class`.

- [ ] **Step 3: Add atomic-apply failure test**

Arrange an existing active config, inject resource-preparation or persistence failure, POST a valid candidate, then assert both persisted config and active runtime remain the old config.

- [ ] **Step 4: Implement config serializer/parser**

Map public:
```text
target_class
confidence_threshold
tracking.association_distance
```
to internal:
```text
target_class_name
conf_threshold_permille
max_dist_permille
```

- [ ] **Step 5: Implement prepare/commit apply**

Prepare replacement line/tracker/timer resources before committing. Do not destroy old resources until persistence succeeds and swap is ready.

- [ ] **Step 6: Implement status/stats/events/reset**

Status gets model/classes from AI Runtime. Stats contains product statistics and delivery state, not legacy debug counters. Events returns RAM ring. Reset performs the full durable session reset including queue and dropped counters.

- [ ] **Step 7: Run API tests/build and commit**

```bash
git add Custom/Services/Web/api <api-registration-files> <api-tests>
git commit -m "feat: expose line counting REST API"
```

### Task 9: Convert old People Counting API/storage behavior into compatibility adapters

**Files:**
- Modify: `Custom/Services/Web/api/api_people_counting_module.c`
- Modify: `Custom/Services/Web/api/api_people_counting_module.h`
- Remove after no consumers: `Custom/Tasks/Inc/pc_model_registry.h`, `Custom/Tasks/Src/pc_model_registry.c`
- Remove after queue migration: `Custom/Tasks/Inc/pc_backlog.h`, `Custom/Tasks/Src/pc_backlog.c`
- Test: compatibility API tests.

**Interfaces:**
- Old routes continue to map onto the one canonical Line Counting service.
- Old model fields are informational/read-only; POST cannot switch models.

- [ ] **Step 1: Write failing old-config mapping tests**

Verify old permille fields round-trip through the adapter to normalized/canonical config and that old GET model fields reflect current AI Runtime.

- [ ] **Step 2: Add model-control regression test**

POST an old request with a different `model_name` / `model_pp_type`; assert current loaded model does not change.

- [ ] **Step 3: Replace old handlers with adapter calls**

Do not call old People Counting state, backlog, or model registry. Convert schema and delegate to canonical validation/apply/stats/reset.

- [ ] **Step 4: Remove obsolete model registry and old backlog implementation**

First:
```bash
rg "pc_model_registry|pc_backlog" Custom
```
Migrate every remaining canonical consumer; leave only compatibility references that are genuinely required, then remove the old files/build entries.

- [ ] **Step 5: Confirm no dual persistence**

Search old config/totals writes and ensure they occur only in migration read paths, never after canonical migration.

- [ ] **Step 6: Run compatibility tests/build and commit**

```bash
git add Custom/Services/Web/api Custom/Tasks Custom/Core/System <build-list-files>
git commit -m "refactor: adapt legacy people counting interfaces"
```

### Task 10: Build the Application Management Line Counting frontend

**Files:**
- Create: `Web/src/services/api/line-counting.ts`
- Create: `Web/src/pages/applicationManagement/line-counting-module.tsx`
- Create: `Web/src/pages/applicationManagement/lineCounting/ConfigPanel.tsx`
- Create: `Web/src/pages/applicationManagement/lineCounting/VideoPreview.tsx`
- Create: `Web/src/pages/applicationManagement/lineCounting/StatsAndEvents.tsx`
- Modify: `Web/src/pages/applicationManagement/index.tsx`
- Test: `Web/src/test/lineCountingApi.test.ts`, `Web/src/test/lineCountingPage.test.tsx`

**Interfaces:**
- Consumes canonical REST endpoints from Task 8.
- Produces sibling Application Management tabs: 过线统计 / MQTT/MQTTS / Webhook.

- [ ] **Step 1: Write API client tests**

Mock responses for config/status/stats/events and assert exact canonical paths. Verify save sends normalized values and reset uses POST with no body.

- [ ] **Step 2: Implement typed API client**

Define TypeScript types matching the canonical JSON schema. Keep runtime state union:
```ts
type LineCountingState =
  | 'disabled'
  | 'running'
  | 'unsupported_model'
  | 'target_class_invalid'
```

- [ ] **Step 3: Write Application Management tab test**

Render `applicationManagement/index.tsx`; assert 过线统计, MQTT/MQTTS, Webhook are sibling entries and selecting 过线统计 renders the Line Counting module.

- [ ] **Step 4: Implement the sibling tab integration**

Do not add a new top-level nav entry. Preserve existing MQTT/Webhook modules and add Line Counting alongside them.

- [ ] **Step 5: Write draft/save behavior test**

Edit confidence/line draft and assert no POST before Save. On Save assert one complete config POST. Target-class change must require confirmation; counter-name-only change must not show reset warning.

- [ ] **Step 6: Implement ConfigPanel**

Groups: 基础 / 计数线 / 跟踪参数 / 统计与上报. Current model read-only. Target dropdown uses `status.model.classes`. Tracker fields use professional labels from the spec. Save remains visible/sticky.

- [ ] **Step 7: Write video overlay test**

Given active tracks/line state, assert overlay renders line, direction arrow, center/trajectory primitives, runtime badge, and does not render bbox/Track ID by default.

- [ ] **Step 8: Implement VideoPreview**

Reuse the existing People Counting/video streaming mechanics, but move visual line editing into draft state. Active line solid; draft line dashed with handles. Do not display IN/OUT numbers over video.

- [ ] **Step 9: Implement stats/events 50/50 area**

Render four statistics and a scrollable recent-event list in equal-width bottom panels. Poll status/stats/events every 2 seconds; fetch config on entry and after successful save.

- [ ] **Step 10: Run frontend tests/build and commit**

```bash
cd Web
npm test -- --run
npm run build
git add src/services/api/line-counting.ts src/pages/applicationManagement src/test/lineCounting*
git commit -m "feat: add line counting application UI"
```

### Task 11: Redirect legacy UI and remove People Counting top-level product surface

**Files:**
- Modify: `Web/src/router/index.tsx`
- Modify: `Web/src/layout/index.tsx` and/or the exact navigation config referenced there.
- Remove: `Web/src/pages/peopleCounting/ConfigPanel.tsx`
- Remove: `Web/src/pages/peopleCounting/StatsPanel.tsx`
- Remove: `Web/src/pages/peopleCounting/VideoPreview.tsx`
- Replace/remove: `Web/src/pages/peopleCounting/index.tsx`
- Modify: locale files containing People Counting navigation labels.
- Test: `Web/src/test/lineCountingNavigation.test.tsx`

**Interfaces:**
- `/people-counting` redirects to `/application-management/line-counting` or the equivalent Application Management route state.
- No independent top-level People Counting navigation item remains.

- [ ] **Step 1: Write failing navigation/redirect tests**

Assert old URL redirects, top-level menu omits 客流统计, and 应用管理 contains the three sibling functions.

- [ ] **Step 2: Implement redirect and navigation cleanup**

Keep the old URL only as a redirect. Do not keep a second page implementation.

- [ ] **Step 3: Remove obsolete People Counting frontend components**

First:
```bash
rg "pages/peopleCounting|peopleCounting|people-counting" Web/src
```
Migrate references, then delete the old page components.

- [ ] **Step 4: Run frontend tests/build and commit**

```bash
cd Web
npm test -- --run
npm run build
git add -A src
git commit -m "refactor: move counting under application management"
```

### Task 12: Remove obsolete firmware People Counting implementation and verify naming boundaries

**Files:**
- Remove after migration: `Custom/Tasks/Inc/people_counting.h`, `Custom/Tasks/Src/people_counting.c`
- Remove after migration: `Custom/Tasks/Inc/pc_types.h`, `pc_tracker.h`, `pc_line_cross.h`
- Remove after migration: `Custom/Tasks/Src/pc_tracker.c`, `pc_line_cross.c`
- Modify: all build/source lists and initialization references.
- Keep: legacy API/config migration naming only where compatibility requires it.

**Interfaces:**
- Canonical runtime/build uses only `line_counting_*` / `lc_*`.

- [ ] **Step 1: Search all legacy symbols before deletion**

```bash
rg "people_counting|pc_tracker|pc_line_cross|pc_types|PC_REALLOC|pc_model_registry|pc_backlog" Custom Web/src
```
Classify every match as migration/compatibility or obsolete canonical code.

- [ ] **Step 2: Migrate remaining canonical references**

Replace runtime initialization, AI pipeline-hold checks, overlay hooks, build entries, and API includes with Line Counting equivalents.

- [ ] **Step 3: Delete obsolete implementation files**

Delete only after `rg` proves no canonical consumer remains.

- [ ] **Step 4: Add realloc regression guard**

Run:
```bash
rg "LC_REALLOC|PC_REALLOC|hal_mem_realloc" Custom/Tasks
```
Expected: no Line Counting use and no surviving old People Counting implementation use.

- [ ] **Step 5: Build firmware and frontend**

```bash
export STEDGEAI_CORE_DIR=/Applications/ST/STEdgeAI/4.0
export PATH="/Applications/ST/STEdgeAI/4.0/Utilities/macarm:$PATH"
make STEDGEAI_VARIANT=4.0 pkg
cd Web && npm test -- --run && npm run build
```

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "refactor: retire people counting implementation"
```

### Task 13: End-to-end behavior, reboot recovery, and stability verification

**Files:**
- Add/Modify: project test scripts or test documentation under the repository's established test location.
- Modify implementation only for defects found by the tests; use systematic debugging before fixes.

**Interfaces:**
- Verifies the complete design contract; introduces no new product API.

- [ ] **Step 1: Run complete host/unit test suites**

Run all migrated Line Counting C tests and all Web Vitest tests. Expected: PASS.

- [ ] **Step 2: Run full package build**

```bash
export STEDGEAI_CORE_DIR=/Applications/ST/STEdgeAI/4.0
export PATH="/Applications/ST/STEdgeAI/4.0/Utilities/macarm:$PATH"
make STEDGEAI_VARIANT=4.0 pkg
```
Expected: successful package build.

- [ ] **Step 3: Exercise model-switch acceptance cases on device**

Verify:
```text
OD A(person) → OD B(person): tracker reset, window/total preserved
OD B(person) → OD C(no person): target_class_invalid, window/total preserved
OD C → OD A: automatic resume, no reset/report boundary
```

- [ ] **Step 4: Exercise target/reset/disable cases**

Verify target change clears session + durable backlog; counter-name change preserves counts; disable emits one partial-window report and stops new windows; re-enable starts a fresh window.

- [ ] **Step 5: Exercise transport persistence**

Disconnect MQTT/Webhook, accumulate reports, reboot, confirm queue recovery, then reconnect one transport at a time. Confirm original payload/boot_id/report_seq/timestamps remain byte-for-byte stable across retry.

- [ ] **Step 6: Exercise clock-invalid reporting**

Boot without valid UTC synchronization and force a window report. Confirm `clock_valid=false`, UTC fields null, and statistics still present.

- [ ] **Step 7: Exercise overflow/storage-failure behavior**

Fill count and byte limits; verify oldest pending report eviction and dropped counters. Inject/force storage failure if harness supports it; counting must continue.

- [ ] **Step 8: Run ≥24-hour stability soak**

Mix detections, zero detections, model reloads, target valid/invalid transitions, transport outages/retries, reboot recovery, queue overflow, and reset. Monitor for HardFault, memory growth, tracker/queue bounds, duplicate report identities, and delivery-state inconsistencies.

- [ ] **Step 9: Final static searches**

```bash
rg "LC_REALLOC|PC_REALLOC" Custom
rg "people_counting" Custom Web/src
rg "people-counting" Web/src
```
Expected: no realloc abstraction; People Counting matches only intentional compatibility/migration/redirect code.

- [ ] **Step 10: Commit verification artifacts only if repository convention stores them**

```bash
git status --short
git diff --check
```
If test documentation/scripts were added:
```bash
git add <verification-files>
git commit -m "test: verify line counting end to end"
```
