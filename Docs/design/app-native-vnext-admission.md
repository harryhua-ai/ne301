# NE301 signed native vNext admission and floor

## Status and authority

This document records the Host implementation for Issue #104 against base `6357a04d509fb8e1851603d1efe0de51def9edb1`. The implementation commit is recorded below. The binary authority is the ordered A-freeze comments: [F1–F4](https://github.com/harryhua-ai/ne301/issues/104#issuecomment-6258534821), [F5–F7](https://github.com/harryhua-ai/ne301/issues/104#issuecomment-6258601629), [F8–F10](https://github.com/harryhua-ai/ne301/issues/104#issuecomment-6258637748), plus the [logical floor owner and retirement decision](https://github.com/harryhua-ai/ne301/issues/104#issuecomment-6257771431). Those comments are the only source for v4 bytes. Host code and tests implement the specified bytes; they do not create additional wire meanings.

| Item | Value |
| --- | --- |
| Host implementation commit | `163f5d0d415e9837f4d494314bd72baf58575ae4` |
| Candidate base | `6357a04d509fb8e1851603d1efe0de51def9edb1` |
| Container / manifest | `NEAPP04` / `NMF4`, 208-byte manifest |
| Native ABI / 32-bit API table | `0x00040000` / 64 bytes |
| Floor logical owner | `(publisher_id, app_id)` |
| App #36 v4 fixture in the read-only `ne30x-app` checkout | not found; no cross-repository v4 artifact is claimed |

The existing App repository contains v1–v3 package and control goldens. App #36 must independently generate matching v4 artifacts from the same A-freeze comments before cross-repository golden agreement can be claimed. The Host tests below use test-only signers and are not production signatures or App #36 goldens.

## Container and signed manifest

The v4 package layout is `8-byte container magic || u32-le manifest_len || u32-le native_file_len || 208-byte manifest || native file || strict DER ECDSA P-256 signature`. The TBS is exactly `file[0 : 224 + native_file_len]`; the signature has no length prefix and no trailing bytes are accepted. `native_file_len` is at most 2,097,152 bytes and package length at most 2,097,448 bytes. Admission checks the sum and offsets using 64-bit arithmetic.

The magic bytes are `4e 45 41 50 50 04 00 00` (`NEAPP` plus major 04). The native file starts at byte 224 and retains the 32-byte `NEA1` header. Its ABI and manifest `required_host_abi` must both equal `0x00040000`. The host verifies SHA-256 of the complete native file, its CRC32 payload, target, entry offset, Thumb bit, and execution bounds. Trust comes from the Host publisher-to-SPKI mapping; package-supplied key data never establishes trust.

All manifest integers are little-endian. Identifier strings retain the v3 identifier validation and right-zero-padded slots.

| Manifest offset | Field | Host rule |
| --- | --- | --- |
| `0..3` | magic | exact ASCII `NMF4` (`4e 4d 46 34`) |
| `4..19` | publisher ID | 16-byte v3 identifier encoding |
| `20..51` | app ID | 32-byte v3 identifier encoding |
| `52..57` | version major, minor, patch | three `u16-le` values |
| `58..59` | reserved | zero |
| `60..67` | target board, required PSRAM MiB | two `u32-le` values, checked against Host policy |
| `68..75` | required Host ABI, required caps | two `u32-le` values; ABI is `0x00040000` |
| `76..95` | exec bytes, native file length, image size, entry offset, target address | five `u32-le` values cross-checked with the package and native header |
| `96..127` | native file SHA-256 | hash of the complete native file including its header |
| `128..159` | publisher SPKI SHA-256 | must match the Host trusted key for the publisher |
| `160..163` | run profile | `u32-le = 3` |
| `164..183` | event, state, report, control, snapshot quotas | five `u32-le` values with v3 meanings |
| `184..187` | `live_publish_max` | `u32-le`, required live packet quota |
| `188..191` | `live_track_max` | `u32-le`, 1 through 64 when live is opted in |
| `192..207` | reserved | all 16 bytes zero |

This is a separate parser and signature path. V3's 192-byte manifest, reserved bytes, caps mask `0xFF`, 60-byte table, and `NFC3` control remain unchanged.

## Capabilities and ABI table

V4 `known_caps_mask` is `0x000003FF`. Bits 0–7 keep their v3 meanings: log, tick, AI events, report submit, app state, session lifecycle, control receive, and business snapshot. Bit 8 (`ENABLE_CONTROL`, `0x100`) additionally requires bits 4, 5, and 6. Bit 9 (`LIVE_PUBLISH`, `0x200`) requires bit 5, `live_track_max` in 1–64, `live_publish_max >= 312`, and the complete track-snapshot closure `48 + tracks * (12 + 16 * 8)`. Without bit 9, both live quotas must be zero. Unknown bits 10–31, missing dependencies, and over-capacity requests fail admission.

The 64-byte v4 table has a 32-bit target layout: `[0..3] table_size=64`, `[4..7] abi_version=0x00040000`, then the 13 v3-signature slots at offsets 8 through 56 under an independent v4 type and dispatch, and `live_publish` at `[60..63]`. The 32-bit target build has a static size assertion. Host 64-bit tests keep native pointer storage for the test process while the API header reports the frozen target ABI size of 64.

Current runtime policy deliberately supports only v4 bits 0–7. It does not grant bit 8 or bit 9: v4 Enable control packages fail the session policy check, v4 live producer packages fail the session policy check, and `live_publish` returns `APP_SESSION_ERR_UNAVAILABLE`. `app_host_control_session_open` also rejects v4 sessions that claim either new bit. This keeps manifest opt-in from becoming Host permission by itself.

## NFC4 control boundary

V4 control magic is exact bytes `4e 46 43 34` (`NFC4`), with a 32-byte header:

| Offset | Field |
| --- | --- |
| `0..3` | magic `NFC4` |
| `4..5` | header version `u16-le = 1` |
| `6..7` | message type `u16-le` |
| `8..11` | total length `u32-le` |
| `12..15` | session generation `u32-le` |
| `16..19` | request ID `u32-le` |
| `20..23` | config generation `u32-le` |
| `24..27` | state revision `u32-le` |
| `28..31` | flags or status `u32-le` |

Types 1–4 retain the existing operation meanings only under the selected major's exact magic. Type 5 `ENABLE_SET` would be exactly 48 bytes with `ENB1`, desired state, expected state revision, and zero reserved fields. A 48-byte Enable receipt would use `ENR1` and must echo the readback revision. The Host currently has no trusted Enable admin grant path, so type 5 is always `UNAUTHORIZED`; no `APPLIED` receipt is synthesized. Tests verify v4 emits `NFC4`, rejects an `NFC3` receipt in the v4 session, accepts the matching v4 receipt for the test control frame, and refuses type 5.

| ENABLE_SET offset | Field | Rule |
| --- | --- | --- |
| `0..31` | common header | type 5, nonzero session/request/config generations, state revision 0, flags 0 |
| `32..35` | payload tag | ASCII `ENB1` |
| `36..39` | desired state | `u32-le`: 1 Enabled, 2 Paused |
| `40..43` | expected App state revision | `u32-le`; zero only for trusted verified initial-state absence |
| `44..47` | reserved | `u32-le = 0` |

An Enable receipt is 48 bytes: the common type-3 header followed by `[32..35] ENR1`, `[36..39] effective state (1 Enabled, 2 Paused, 3 UNKNOWN)`, `[40..43] readback revision equal to header state revision`, and `[44..47] zero. Existing status values remain 0–7; status 0 is valid only after state CAS/readback and Host floor confirmation. This candidate accepts none of these Enable requests, so this byte description is documentary and the Enable path is not represented as implemented.

The floor API stores request/session evidence and makes an exact replay idempotent. Reusing the same `(session_generation, boot_id, request_id)` with different config/revision or package evidence returns `IDENTITY_CONFLICT`. The Host does not currently hand an Enable_SET to the app, because doing so requires the missing trusted admin gate and App #30 state-CAS/readback integration. No App-owned bytes are interpreted as Host policy.

## LVN4 live producer boundary

The A-frozen live packet has a 48-byte header. The Host table exposes the typed callback, but it always returns unavailable until the Host #91 ingest path and the signed capability policy are enabled. No live packet parser, queue, browser read path, or App tracker is claimed by this card.

| Offset | Field | Rule |
| --- | --- | --- |
| `0..3` | magic | ASCII `LVN4` |
| `4..5` | header version | `u16-le = 1` |
| `6..7` | kind | 1 event batch, 2 active-track snapshot |
| `8..11` | total length | exactly header plus records, within signed and Host quota |
| `12..15` | packet sequence | nonzero, monotonic within current session |
| `16..19` | event-through sequence | latest observed source crossing sequence |
| `20..23` | cumulative event total | includes unpublishable source facts |
| `24..27` | cumulative source gap | producer-side known loss only |
| `28..31` | source timestamp | informational; not an ordering or identity source |
| `32..35` | config generation | current nonzero generation |
| `36..39` | record count | kind-specific count |
| `40..43` | flags | bit 0 source-gap delta, bit 1 loss unknown, bit 2 resync; other bits zero |
| `44..47` | reserved | zero |

Kind 1 records are exactly 16 bytes: event sequence, track ID, event timestamp (`u32-le` each), direction, source-uncertain flags, and normalized `x_q8`/`y_q8`. Count is 0–16 and total length must be exact. Kind 2 carries 0–`live_track_max` variable records. Each track is a 12-byte fixed header plus 0–16 points of 8 bytes each; the snapshot is a complete replacement view and must consume the packet exactly. The maximum closure is `48 + live_track_max * (12 + 16 * 8)` bytes, up to 9,008 bytes for 64 tracks.

The event record offsets are `[0..3] event_seq:u32-le`, `[4..7] track_id:u32-le`, `[8..11] event_ts_ms:u32-le`, `[12] direction (1 IN, 2 OUT)`, `[13] flags (bit 0 source uncertain; all other bits zero)`, `[14] x_q8`, `[15] y_q8`. Event sequences strictly increase and cannot exceed the header's event-through sequence. A track record is `[0..3] nonzero unique track_id:u32-le`, `[4..5] point_count:u16-le (0–16)`, `[6..7] class_index:u16-le (valid current model class or 0xFFFF unknown)`, `[8] state=1 ACTIVE`, `[9] flags=0`, `[10..11] confidence_q16:u16-le`, followed by `point_count` records of `[0..3] ts_ms:u32-le`, `[4..5] x_q16:u16-le`, `[6..7] y_q16:u16-le`. All record boundaries and count products are checked before any future Host copy.

The Host never takes publisher, app, SPKI, TBS, boot, or session identity from these packet bytes. Those values must come from the signed committed caller context when #91 implements ingestion. The callback currently cannot make such a claim and returns unavailable.

## Floor encoding and lifecycle

The Host-owned KV key is `app_native_vnext_floor`. Its fixed 192-byte little-endian value is CRC32-protected. One active native install record exists in the current installer, so the floor row is scoped to that logical owner; key hash and package identities are evidence, not floor partitions.

| Offset | Field | Encoding |
| --- | --- | --- |
| `0..3` | magic | `NVF4` (`4e 56 46 34`) |
| `4..7` | record version | `u32-le = 1` |
| `8..11` | state | `1 ACTIVE`, `2 RETIRE_READY` |
| `12..15` | minimum native major | `u32-le`, at least 4 |
| `16..23` | floor generation | nonzero `u64-le`, increments for a new request |
| `24..27` | session generation | nonzero `u32-le` |
| `28..31` | boot ID | nonzero `u32-le` |
| `32..35` | request ID | nonzero `u32-le` |
| `36..39` | config generation | nonzero `u32-le` |
| `40..43` | expected state revision | `u32-le` |
| `44..59` | publisher ID | 16-byte zero-padded identifier |
| `60..91` | app ID | 32-byte zero-padded identifier |
| `92..123` | signer SPKI SHA-256 | 32 bytes |
| `124..155` | signed native TBS SHA-256 | 32 bytes |
| `156..187` | complete package SHA-256 | 32 bytes |
| `188..191` | CRC32 | CRC over bytes 0–187 |

`app_install` verifies the signed package, checks the candidate owner and major against the floor before install side effects, and rejects a lower major. `app_install_query_committed` treats corrupt or unreadable floor state as storage UNKNOWN. `app_host_session_start` re-reads and re-verifies the committed package and checks the floor before staging/entry. Boot recovery preserves ACTIVE; it only completes a RETIRE_READY removal when no uninstall intent and no committed record remain.

Floor writes use the existing installer transaction lock and exact readback proof. A failed write returns storage UNKNOWN. If the bytes reached storage but the readback receipt is lost, retrying the identical session/boot/request evidence returns success without advancing the generation again. Reusing the same request identity with changed config/revision is rejected. Install/TBS/key changes do not clear the logical owner's floor.

The #101 handoff is inside the existing `app_install_uninstall_confirmed` lifecycle. Only after exact intent matching, local cleanup, and externally confirmed purge does it persist RETIRE_READY, prove uninstall-intent deletion, confirm the committed record is absent, then delete the floor. Pending/unknown cleanup leaves the active floor. A late completion for a retired record cannot clear a new ACTIVE floor. The owner/fencing path continues to use the existing installer and sidecar APIs; it does not introduce a second installed-record truth.

The floor writer is an internal Host lifecycle API. There is no production Enable_SET caller in this candidate: the real Host admin grant and App #30 CAS/readback path are absent, so this implementation does not claim a production Paused transition. The Host-side storage, rollback refusal, request replay, recovery, and clean-retirement behavior are exercised through signed install/session/lifecycle tests.

## Golden vectors and failure matrix

`tests/app_admission/gen_v4_vectors.py` creates 22 signed v4 test fixtures using only the two test keys copied from `tests/sign_spike/vectors/` with mode 0600. The generator is consumed by independent C admission/session parsers. It emits valid generic, Enable-opt-in, live opt-in, 64-track closure, and next-generation packages; and negative vectors for unknown caps, missing dependencies, quota/track violations, wrong ABI, wrong publisher/target, untrusted rotated key namespace, foreign signer, TBS/signature tamper, bad magic/length/reserved bytes, and trailing data. V3 fixtures also use these test-only keys; no production signing key is read.

| Scenario | Exercised through | Expected result |
| --- | --- | --- |
| Signed v4 generic package and v3 compatibility | C admission and session | v4 major 4/ABI4; v3 golden still accepted |
| Bad signature, unknown cap, wrong publisher/target, TBS mismatch | C admission | signature, package, trust, or target error; no fallback parser |
| Enable/live manifest opt-in with Host policy absent | C admission and v4 session | standalone shape is recognized; session denies the unsupported new capability |
| V4 live callback and Enable_SET request | v4 table and Host control | live returns unavailable; type 5 is unauthorized |
| Cross-major control receipt | v4 session/table | `NFC3` rejected; matching `NFC4` accepted |
| Paused floor followed by signed old v3 package | real install, committed record replacement, session start | install/start refused with downgrade; no entry attach |
| Same owner, new signed v4 TBS/package | real installer | accepted while floor remains 4; floor evidence remains owner-scoped |
| Different trusted logical publisher owner | real installer with alternate test trust mapping | floor owner conflict before install side effects |
| Floor write failure, committed write with lost readback receipt, exact replay, changed replay, transaction busy | active v4 session and durable KV API | UNKNOWN on uncertain write/busy; exact replay idempotent; changed replay conflicts |
| Restart and damaged floor | boot recovery and real install/session entry | ACTIVE survives recovery; bad CRC/IO becomes storage UNKNOWN and refuses admission |
| Pending purge and confirmed clean uninstall | real `app_install_uninstall_confirmed` lifecycle | pending retains floor; confirmed cleanup retires it |
| Late cleanup after new v4 installation | real install plus retirement API | stale retirement cannot clear new ACTIVE floor; v3 remains refused |

The Host vector hashes are generated in the test build and are not presented as checked-in production artifacts. The read-only App checkout has no matching v4 fixture, so the independent App #36 parser/serializer and byte-for-byte shared fixture comparison remain NOT_RUN.

## Build and Host test commands

Run the focused v4 suite with:

```sh
make app-native-vnext-test
```

Run each acceptance suite from the repository root with:

```sh
make -C tests/app_admission clean test
make -C tests/app_host_session clean test
make -C tests/app_host_control clean test
make -C tests/app_host_run clean test
make -C tests/app_sidecar_lifecycle clean test
make -C tests/app_report_uninstall clean test
make -C tests/app_installer clean test
make -C tests/app_state clean test
make -C tests/app_native_vnext clean test
```

The build commands regenerate test vectors and compile with `-Wall -Wextra -Werror`. Build products stay under the test directories and must be removed with the corresponding `clean` targets before candidate preservation.

Final Host results for this candidate: admission `421/0`, session `583/0`, control `548/0`, run `214/0`, sidecar lifecycle `351/0`, report uninstall `130/0`, installer `778/0`, state `448/0`, and vNext `58/0`. Total: `3,531 checks, 0 failures`. The root `make app-native-vnext-test` target also passed at `58/0`.

## Acceptance boundary and NOT_RUN

Host software tests prove signed v4 parser/session dispatch, Host capability denial, durable floor storage and refusal paths, and the confirmed uninstall handoff. They do not prove App #30 Enable/Paused CAS semantics, App #27 crossing/tracker behavior, Host #91 live ingest/queue/read behavior, or browser presentation. Those product functions remain unavailable in this candidate. The issue's broader joint Host/App state-codec vectors and independent App #36 v4 goldens are not claimed as PASS.

No production signer, device, Flash/NVS, camera, MQTT, webhook, firmware build, or release operation was run. Target 32-bit binary layout is guarded by source static assertions, while the hardware build and physical persistence behavior remain NOT_RUN. The Candidate is software evidence only and does not set App #30/#27 or Host #91 readiness.
# Retired by Issue #136

This document records the former signed vNext admission design. Host no longer accepts its signed v1–v4 App package formats. The active unsigned package format and its preserved-state policy are defined in [app-unsigned-single-file-format.md](app-unsigned-single-file-format.md).
