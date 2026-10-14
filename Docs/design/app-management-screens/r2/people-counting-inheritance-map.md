# people-counting source code inheritance map

## Source and target

Source tree: `Web/src/pages/peopleCounting/` at commit `6fb5dd798ef623316971816475be2208d9fbe018`.

The four source files below are the implementation basis for the business page. The source component behavior and JSX are carried into the `applicationManagement/linecrossing/` page, with the v3 API boundary adapted in the page and adapter. The accepted application-management shell, four states, management dialog, permissions, MQTT and Webhook pages remain outside this correction.

| Source component and code blocks | Inherited target | Treatment and review pointer |
|---|---|---|
| `peopleCounting/index.tsx`: `clampPm`, `computeOutside`, `handleLineChange`, `handlePickPoint`, `handleFlipDirection`, `handleToggleEdit`, `handleResetLine`; root `Card` + `VideoPreview` / `StatsPanel` / scrollable `ConfigPanel` JSX | `linecrossing/LineCrossingPage.tsx`; shape conversion in `linecrossing/lineCrossingAdapter.ts` | The same line geometry and edit phases, direction mirror, reset-to-center action, 3fr video / 1fr right column, statistics above scrollable config, and five-second refresh cadence form the page. Fetch/save/reset ownership moves from `peopleCounting` calls to the accepted session store and v3 APIs. `LineCrossingPage` is the composition host for the inherited components and v3 status blocks. |
| `peopleCounting/VideoPreview.tsx`: player setup, `mseError` bounded restart handler, one-second frozen-video watchdog, `wv_close` / `wv_open` backoff recovery, ResizeObserver size tracking, manual reload, stage click-to-permille handler, geometry, SVG overlay, reconnect banner and controls | `linecrossing/VideoPreview.tsx` | The source component is carried as the video component. Its props use the adapter's v3 editor/stat view types; `config.enable` becomes snapshot-config presence because v3 has no `enable` field; the stage receives a test id. Video stream request ownership, natural `videoWidth/videoHeight` aspect, shared coordinate stage, 1:1 SVG viewBox, line / direction / IN-OUT overlay, edit phases, reload, watchdog, bounded restart and reconnect backoff remain. |
| `peopleCounting/StatsPanel.tsx`: `StatBig`, compact four-value statistics JSX, reset button and reset-confirmation Dialog | `linecrossing/StatsPanel.tsx` | The compact green IN / red OUT statistic components and confirmation flow are retained. Values come from `statsFromSnapshot`; session freshness/state, report sequence/gaps, two-channel report queue and v3 reset receipt are added. Old timestamp, debug counters and old backlog API reads are omitted because v3 does not provide those fields. |
| `peopleCounting/ConfigPanel.tsx`: `permilleToPercent`, `numField`, `permilleField`, `switchField`; General / Counting Line / Detection / Tracking / Reporting group JSX; edit, flip, reset-line and save controls | `linecrossing/ConfigPanel.tsx` | The source group hierarchy, percentage display, editable numeric fields, switches, line controls and save action are carried into the v3 candidate form. The accepted v3 schema determines available fields; generation drift, candidate validation, destructive acknowledgement and receipt feedback are added around save. |
| Source `PeopleCountingConfig` flat fields and `PeopleCountingStats` values | `linecrossing/lineCrossingAdapter.ts`: `configFromSnapshot`, `candidateFromEditorConfig`, `statsFromSnapshot` | Flat legacy line/outside values map to nested trusted snapshot config and the validated v3 candidate; current/cumulative counters and report/gap metadata map to the inherited display model. No adapter imports the legacy API service. |

## Preserved source behavior

- People-counting live preview uses the source `deviceTool.startVideoStreamReq` / `stopVideoStreamReq` lifecycle and `H264Player` stream path.
- The video stays at natural aspect ratio, updated from loaded video metadata. Overlay geometry and pointer coordinates share the rendered video stage; SVG coordinates are measured by `ResizeObserver`.
- Two clicks place the line endpoints. The outside reference is computed perpendicular to the line. A third click starts a new line; flip mirrors the outside point around the line midpoint; reset-line returns the line editor to center.
- Video IN/OUT counts, line and direction overlay, reconnect state, manual reload, one-second freeze detection, up to three MSE error restarts and close/open backoff recovery remain in the inherited video component.
- The compact cumulative/window count bar and grouped config layout remain in the right column. Config edits stay local until save; successful v3 receipt plus matching snapshot generation rebases the draft. Reset requires the existing confirmation dialog and exposes v3 receipt state.
- The existing management dialog continues to own persistent-session start/stop. The business page displays the trusted session state with hot-config receipt and report status.

## Exact v3 adaptation delta

- Reads use `useAppSessionStore` snapshot, session status and report status. The snapshot remains the accepted single-lock view with generation/identity consistency checks. The page polls snapshot and reports on the existing five-second refresh cadence.
- Config save validates and encodes `LineConfigCandidateInput`, then submits `control/config` with a request id, expected config generation and destructive-change acknowledgement. UI state follows the trusted control receipt and matching applied snapshot generation.
- Counter reset submits `control/reset` with the snapshot generation and confirmation; the old `peopleCounting.resetTotals()` direct call is not used.
- Report transport status comes from the dual-channel v3 report API, including session binding, queue rows, per-channel state and attempt count.
- Video preview start/stop remains the shared device video-preview API used by the source component; business config, counter, session, and report state use v3 interfaces.
- MQTT/MQTTS and Webhook enable/configuration remain in the preserved application-management pages; the business config does not invent duplicate transport controls.

## Schema incompatibility evidence

`Web/src/services/api/appSession.ts` defines `LineConfigCandidateInput` with only `counter_name`, `target_class_name`, nested line/outside coordinates, `conf_threshold_permille`, `max_dist_permille`, tracking values, `window_minutes`, `tracks_report_enable`, and `heat_grid_enable`. `validateLineConfigCandidate` validates these fields, and `encodeLineConfigCandidate` serializes the bounded candidate buffer. There is no v3 candidate field for legacy `enable`, editable `model_name` / `model_pp_type`, `mqtt_report_enable`, `webhook_report_enable`, or `backlog_capacity`.

Accordingly, the page shows model metadata read-only from trusted session/snapshot data; it does not expose the removed enable/model selectors, MQTT/Webhook switches or backlog capacity as if v3 could save them. Transport settings remain owned by the preserved MQTT/Webhook pages. This is the only source-field omission: it follows the v3 request contract, rather than a visual redraw or substitute implementation.

## Source and regression review

The actual source files can be read at the pinned commit with:

```sh
git show 6fb5dd798ef623316971816475be2208d9fbe018:Web/src/pages/peopleCounting/index.tsx
git show 6fb5dd798ef623316971816475be2208d9fbe018:Web/src/pages/peopleCounting/VideoPreview.tsx
git show 6fb5dd798ef623316971816475be2208d9fbe018:Web/src/pages/peopleCounting/StatsPanel.tsx
git show 6fb5dd798ef623316971816475be2208d9fbe018:Web/src/pages/peopleCounting/ConfigPanel.tsx
```

Interaction regressions are covered in `Web/src/test/appManagement/linecrossing.test.tsx`: source config groups, two-point line stage, flipped direction, source statistics shape, v3 config submit/receipt, destructive acknowledgement, reset confirmation, stale snapshot and report status. `Web/src/test/appManagement/videoPreviewInheritance.test.tsx` checks natural-aspect video rendering and stage coordinate mapping. Build and suite results are recorded with the candidate delivery evidence.
