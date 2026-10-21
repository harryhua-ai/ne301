# Host #124 Run Gate and Signed Pair Floor

Status: R3 candidate for PR #127, based on `bd7014e7`.

## Run gate matrix

`POST /api/v1/app/run` first enters `app_host_session_start`. Container v1 is
not a session profile, so it falls back to `app_host_run_installed` and keeps
the established one-shot v1 execution path. Supported v2, v3, and v4
containers stay on the session path.

| Installed state | Gate decision | Execution result |
| --- | --- | --- |
| No committed install | Refuse with `NOT_INSTALLED` | No entry call |
| Trusted legacy v1 install | Preserve legacy admission and load checks | Run entry and report completion |
| Authorized v4 native/UI Pair at the current release floor | Require an active lifecycle descriptor and Pair authorization | Start the session and invoke entry |
| Native-only v4 install, missing/corrupt UI, revoked Pair, or mismatched authorization | Refuse closed | No entry call |
| Pair storage or pending lifecycle state is UNKNOWN | Return `STORAGE_STATE_UNKNOWN` or a Pair-specific detail | Refuse closed; lifecycle recovery may make a later request servable |
| A session is already running or storage transaction is busy | Return `BUSY` | No second entry call |

The API's `reason` field is the readable `app_session_result_str` or
`app_host_run_result_str` value. A more specific gate cause is carried in
`detail` from the outcome reason. In particular, no result or reason is read
from an untyped test stub.

## Persistent Pair floor

`app_sidecar_pair_floor` stores the accepted Pair history: publisher/app
identity, publisher key digest, native/UI/Pair TBS digests, release sequence,
and native/UI versions, protected by a generation and CRC. The floor is
monotonic for the identity. An identical Pair at the same sequence is
idempotent; changed content at the same sequence and lower sequences are
refused. Uninstall does not erase the floor.

For a signed Pair install, the lifecycle verifies the bundle and persists its
assets before advancing the floor. The floor is written before the active
sidecar slot commit. If that later slot write fails, the install returns
`STORAGE_STATE_UNKNOWN`; the floor remains at the newly accepted sequence and
the service stays unavailable until recovery resolves the pending lifecycle
state. This ordering keeps the rollback barrier durable across an interrupted
upgrade. A missing, unreadable, or corrupt floor fails closed when prior Pair
records or an unresolved transaction make absence ambiguous.

The gate tests cover floor query/presence/sequence, interrupted floor and slot
writes, restart recovery, same-sequence conflicts, downgrade attempts, asset
corruption, native rebind, and uninstall retention.

## R3 test boundary and evidence

The earlier 39/290 fixture failures came from the gate target linking generated
`void` stubs for the value-returning session entry points. That sent v4 cases
through the legacy ABI v1 runner and made session result strings undefined.
The gate target now links the real session, control, live, and report cores;
its existing injected backend drives the same session path as the API. No
production admission policy or ABI check was relaxed.

With the four signed local Pair fixtures, `test_app_install_gate` reports 290
checks and 0 failures. `make test` in `tests/app_installer` reports 271, 280,
290, and 54 checks, each with 0 failures.

F4 remains pending A's decision. This candidate adds no Host C/Web Linux
workflow. The existing Docs Sync check is separate from Host test evidence.
