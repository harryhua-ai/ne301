# App Host Session — #43 Host 持续 AI 检测结果供给与独立 App 运行会话

Status: implemented on `agent/43/1b581cdb` (base `4a9de54e`).
Authority: NE301 issue #43 ght-contract (AC1–AC4) + A READY decision 2026-10-13
+ A v3 consumer handover notes 2026-10-12. Wire/ABI authority:
ne30x-app `docs/app-package-protocol-v2-draft.md` (formally integrated v2 spec)
and the accepted consumer `apps/line-crossing/app/lc_bus.c`.

This file carries all explanatory prose; per #43 governance the new/changed
`.c/.h`/Makefile/test sources carry no new comments.

## 1. What is implemented

A single, admin-started, cooperative-stop **v2 run session** on the
experiment App Host:

- `Custom/Services/AppHost/app_host_session.h/.c` — portable session core:
  admission against the v2 policy, v2 48B API table (`log`, `tick_ms`,
  `event_next`, `model_meta`, `class_name`, `report_submit`, `report_status`,
  `state_read`, `state_commit`, `should_stop`), bounded event ring, class
  table mapping, generation tracking, cooperative stop, teardown. All RTOS /
  AI dependencies are injected through `app_host_session_backend_t`.
- `Custom/Services/AppHost/app_host_session_device.h/.c` — device backend
  (CMSIS-RTOS2 threads/mutexes/events + video hub hold + AI listener adapter
  + model/class-table snapshot from `nn_model_info_t.config_ptr`).
- `Custom/Services/Web/api/api_app_session_module.h/.c` — authenticated
  high-privilege admin routes `POST /api/v1/app/session/start`,
  `POST /api/v1/app/session/stop`, `GET /api/v1/app/session/status`
  (registered under the existing `NE301_APP_HOST_POC` guard in
  `web_service.c`, sources and the `Custom/Services/Video` include path
  added to `Appli/Makefile` in the same guard).
- `Custom/Services/Web/api/api_app_install_module.c` — the install gate now
  dispatches on container version: v1 containers verify exactly as before
  (zero behaviour change); v2 containers verify against a v2 policy that
  grants only `log|tick_ms|ai_events|session_lifecycle` (bit0,1,2,5).
- `Custom/Core/Video/video_ai_node.h/.c` — additive bounded result-listener
  list (2 slots) invoked at the same safe point as the existing single-slot
  telemetry callback, under the node cache mutex so removal is strictly
  ordered against in-flight callbacks. Existing single-slot behaviour and the
  telemetry path are untouched.
- `Custom/Services/AppHost/app_admission.h/.c` — additive
  `app_admission_container_version()` (magic sniff, 0/1/2).

Not implemented (owned elsewhere, returned `UNAUTHORIZED`): `report_submit`,
`report_status` (#44), `state_read`, `state_commit` (#46). The session Host
policy mask excludes those capability bits, so no admissible package can
request them; the table slots exist only because the v2 table layout is
fixed at 48B/10 pointers.

## 2. Session lifecycle (AC2)

State machine: `IDLE → RUNNING → STOPPING → IDLE` with a sticky
`last_result`/`last_reason` pair. One session device-wide; enforced by the
existing `app_install_app_running()` gate which the session holds TRUE from
successful admission until entry return — this single flag already blocks
`app_install`, `app_install_uninstall` and the v1 `app_host_run_installed`
path (verified in `app_installer.c`), giving the AC2 mutual exclusion
between stop/return/upgrade/uninstall and AI callbacks without new locks on
the installer.

Start (admin thread, synchronous admission like v1 run):

1. refuse unless `IDLE` and no app running (`BUSY`);
2. query committed install (`STORAGE_STATE_UNKNOWN` fail-closed);
3. verify package again (SHA-256 over storage content, v2 admission,
   identity match against the install record) — a session is only ever
   started from a currently-trusted v2 `run_profile=1` package;
4. snapshot the current model (name/version + full class table). A snapshot
   without a loaded model, a non-OD result type, or a class table that fails
   canonical encoding (empty, duplicate, >32 labels, label >31 chars,
   invalid UTF-8) refuses the start (`UNAVAILABLE` / reason `model_incompatible`);
5. stage + validate the native image (v2 image offset 200, abi 0x00020000),
   commit caches, attach the AI source, spawn the entry thread with the v2
   table.

If the AI source cannot be attached (no backend hook or attach failure) the
start rolls back (detach, scrub, `app_running=FALSE`) and reports
`RUNTIME_UNAVAILABLE` / reason `ai_attach_failed` — a session that could
never receive events is never reported as started. If entry spawn fails the
start rolls back the same way. Teardown publishes `app_running=FALSE`
before the `IDLE` transition becomes observable, so a successful stop and a
fresh start can never observe a half-torn-down session.

Stop (admin thread): sets the cooperative stop request, wakes event waiters,
then waits a bounded time for entry return. Confirmed return → teardown
(detach AI source first, then scrub, app_running=FALSE) and report
`stopped`. Unconfirmed after the bound → report `stopping` with `busy:true`
(BUSY semantics); the stop request stays latched, teardown still happens on
entry return, and no image overwrite/uninstall can proceed because
`app_running` is still held. There is no forced termination of native code.

Restart: nothing persists; state, sequence, generations and the stop flag
are RAM-only; boot never auto-starts a session (no call site outside the
admin route).

## 3. AI event supply (AC1, AC3)

Producer = the AI node thread, at the only safe stage: immediately after a
successful inference while the postprocess output buffers still hold that
inference's data (same invariant the telemetry snapshot relies on). The
device adapter copies at most 64 `od_detect_t` values plus the raw
`class_name` pointers into a stack frame view and hands it to
`app_host_session_on_ai_result()`. The session core then, under its own
mutex:

- resolves each detection's `class_name` byte string against the session's
  private copy of the current class table (exact unique match). No
  `nn_result_t*` or `class_name` pointer ever crosses to the delivered
  event: frames are independent bounded copies (40B header + N×24B records,
  N≤64, max 1576B) written into ring slots owned by the session;
- drops any detection with a non-finite float (NaN/Inf) — the whole frame is
  then not completely encodable and is reported as a known-loss GAP, never
  silently truncated into a "complete" frame;
- `FRAME(0)` (real empty OD frame) is delivered as a 40B FRAME event,
  distinct from the `NO_EVENT` return code;
- results whose model identity no longer matches the session snapshot
  trigger a rebuild: the ring is flushed (flushed frames counted as known
  loss), `model_generation`/`class_generation` advance, and a
  `MODEL_CHANGED` event carrying the gap flags is enqueued before any
  new-generation frame;
- a model snapshot that cannot be canonically encoded marks the model
  incompatible: `event_next`/`model_meta`/`class_name` return
  `INCOMPATIBLE` until a valid model appears (which surfaces as
  `MODEL_CHANGED`). Nothing is fabricated while unmapped.

Ring depth is 8 frames. If the consumer is slower than the producer the
oldest queued frame is dropped (newest preserved, order stable) and the drop
is accounted as known loss; loss is surfaced on the next delivered event as
`flags bit0` + `lost_frame_count` (or `bit1` + `0xFFFFFFFF` when the loss
counter cannot be represented honestly), or as a standalone `GAP` event when
no frame follows. Counters (`frames_dropped`, `gap_events`,
`model_changes`, `events_delivered`) are exposed via status and are
cumulative for the session lifetime.

`event_next` waits on an event flag with the caller's `max_wait_ms` clamped
to an internal finite bound (5000 ms), so a stopping session can never be
blocked indefinitely; `BUFFER_TOO_SMALL` does not consume the head event;
`sequence` and `monotonic_ms` are raw u32 mod-2^32 counters (session-scoped,
never persisted). AI events do not depend on MQTT telemetry or web preview:
the session holds its own video-hub subscription (`VIDEO_HUB_SUBSCRIBER_CUSTOM`,
discarding sink) so the pipeline keeps inferring headless, exactly like the
telemetry path but on a separate listener and separate buffers.

On the device the model view is cached in the backend keyed by the model
identity (`config_ptr`, `model_size`): the per-frame path only copies a
cached snapshot, and the model JSON (class table) is re-parsed only when the
loaded model identity actually changes. Both `ai_model_view` call sites hold
the session mutex, so the cache needs no lock of its own.

## 4. Hot-switch discipline support

The Host keeps `model_meta`/`class_name` answers generation-stable: both
bind to the session's recorded `(model_generation, class_generation)`;
stale-generation `class_name` queries return `INCOMPATIBLE`. Because all
allocation happens at start (or inside the AI thread during a model
rebuild), and `model_meta`/`class_name`/`event_next` never allocate, an App
can resolve a complete target binding before a config CAS and is guaranteed
that no Host call after the CAS can fail on Host-side resource allocation —
the only post-CAS failure modes are generation mismatch (observable, not a
resource failure).

## 5. v1 compatibility

`app_host_run.c`, the v1 admission policy values and the v1 16B table are
byte-identical to #31/#47 state. v1 containers select the v1 policy at the
install gate and are refused at session start (reason `profile_unsupported`).
The full existing regression matrix is re-run green.

## 6. Testing

`tests/app_host_session/` builds the portable session core against signed v2
vectors generated by `gen_session_vectors.py` (same pure-Python P-256
signing as `tests/app_admission/anchor_keygen.py`, key d=1 matching
`ANCHOR_VECTOR_SPKI_DER`; session vectors request exactly
`log|tick_ms|ai_events|session_lifecycle` = caps 0x27, the only mask the
Host grants), with a pthread seam backend and a fake AI source.
Covered: lifecycle (start/double-start/refusals/cooperative stop/BUSY
latch/restart/attach-failure refusal), bounded-copy independence, 40B/24B
wire layout, 64-detect frames, over-64 GAP, NaN rejection, class mapping
(unique match, duplicate labels, stale generation), NO_EVENT vs FRAME(0) vs
GAP vs MODEL_CHANGED vs STOPPING, slow-consumer loss accounting, generation
wrap, u32 clock bits, unauthorized before/after session, stub slots,
install-gate v1/v2 dispatch, and uninstall-blocked-while-running.
Device-only concerns (real Thumb entry execution, real NPU inference
cadence, board model switch) are NOT_RUN — no host simulation is presented
as device PASS.

## 7. Known boundaries

- One session; no concurrent sessions, no auto-start, no persistence.
- `event_capacity` in the session/install v2 policy is the accepted
  `event_max` ceiling (16384); every delivered event is ≤1576B by
  construction, so any admissible `event_max ∈ [1576, 16384]` is honestly
  servable.
- The 32-label class-table bound and 5000 ms wait clamp are Host resource
  decisions, reported fail-closed (INCOMPATIBLE / clamped wait), never
  papered over.
- Device E2E (real inference feed, Thumb entry, model hot swap on board) is
  explicitly NOT_RUN per contract.
