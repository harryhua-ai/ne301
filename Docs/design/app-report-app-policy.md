# App-scoped Host report policy — Issue #88

Status: software implementation candidate; device validation remains NOT_RUN.
Scope: Host-owned MQTT/Webhook send switches, bounded volatile backlog admission, and the purge seam consumed by the future trusted App cleanup path.

## Ownership and trust boundary

`app_report_service` remains the only report queue owner. The policy module stores only Host policy in a separate NVS key, `app_report_policy`; it does not read or write the App-owned 144-byte CONFIG_APPLY record or its legacy JSON. Existing MQTT topic, Webhook URL, credentials, TLS, certificates, and global network settings remain controlled by their existing services.

`app_report_service_policy_query` and `app_report_service_policy_commit` require the currently bound, active Host session and an identity matching its publisher ID, App ID, and publisher-key hash. The identity also includes the committed native TBS SHA-256. That full identity must be resolved by trusted Host code; after #84 exists, only its typed broker should call these internal APIs. No Web route, iframe capability, admin token, or queue pointer is added here. The App session struct available in this branch does not carry the TBS hash, so this branch cannot independently derive that final identity component.

The session is one App at a time. A persisted policy is never applied to a session until trusted Host code resolves and binds its full identity. If persisted policy exists but the service only has the legacy publisher/App/key tuple, reporting fails closed until that typed identity is supplied. A different full TBS identity starts from defaults; committing its first policy replaces stale policy records for the same publisher/App/key tuple.

## Policy record and persistence

Each record is keyed by `(publisher_id, app_id, publisher_key_sha256, native_tbs_sha256)` and contains MQTT enabled, Webhook enabled, backlog capacity, a u32 revision, and the last completed cleanup intent. The store has a fixed 16-record limit, including purge tombstones. The serialized format uses an 8-byte header, 132 bytes per record, and a 4-byte CRC32; the maximum blob is 2,124 bytes. It is version 2 and little-endian. Payload bytes and queue entries are never persisted.

Policy writes use compare-and-swap on the expected revision. The service reports APPLIED only after a write and exact readback match. A durable old value after a failed write returns STORAGE_ERROR; an unreadable, malformed, CRC-invalid, or partially written value returns STORAGE_UNKNOWN and blocks policy use. Revision overflow and stale revision fail closed. Missing policy has verified defaults: both channels enabled, capacity 8, revision 0.

The device adapter uses `storage_nvs_read/write(NVS_USER, "app_report_policy", ...)`. Each adapter call validates the storage return value and byte count. Reboot clears the RAM queue and seen state as before, then reloads persistent policy. Device NVS behavior and flash wear are NOT_RUN.

## Capacity and RAM budget

The current implementation keeps the physical queue at 8 shared slots, with the existing 6,240-byte payload array in each slot (49,920 payload bytes). Per-App policy capacity is writable from 1 through `APP_REPORT_POLICY_MAX_CAPACITY`, and defaults to that maximum. The compile-time maximum defaults to 8 and cannot exceed `APP_REPORT_QUEUE_SLOTS`; future device budgets can lower it at build time. A requested value outside the supported range is rejected, never clamped.

Admission counts one backlog item while either channel remains PENDING. Reaching the App's effective capacity returns QUOTA_EXCEEDED. Reaching the shared physical slot limit because other Apps hold entries returns BUSY. Only a terminal entry belonging to the same full App identity can be evicted to preserve its status slot; entries belonging to another App are never evicted. Shrinking below current pending use returns CAPACITY_CONFLICT and leaves the prior policy and revision unchanged. Query returns configured capacity, allowed range, current use, revision/generation, and storage result.

The software-owned memory ceiling is bounded by compile-time constants. A reproducible Cortex-M55 object compile with `arm-none-eabi-gcc 15.2.1` reports service `.bss` of 58,680 bytes at baseline and 60,124 bytes after this change, a 1,444-byte increase. The new policy-store object uses 8,700 bytes of `.bss`; combined service and policy storage is 68,824 bytes, 10,144 bytes above the baseline service object. The compile includes the fixed 16-record DB, three 2,124-byte serialization buffers, App identity/revision/generation/call state on each of 8 queue entries, publisher attribution for the existing 16-slot seen ring, and active/purge bookkeeping. The existing 49,920-byte queue payload pool remains part of the service `.bss` and is not counted again in the 10,144-byte increase. The independent C capacity study reports an 8-entry queue using 51,968 bytes on the host ABI; 256 theoretical entries require 1,662,976 bytes at that entry size (1,597,440 payload bytes plus per-entry metadata). That calculation is a software model, not a device measurement or approved device limit. The object `.bss` values include compiler layout and alignment and can be reproduced with `arm-none-eabi-gcc -std=c11 -mcpu=cortex-m55 -mthumb -ffreestanding -I Custom/Common/Inc -I Custom/Services/AppHost -c <source> -o <object>` followed by `arm-none-eabi-size <object>`. This is a compile-time storage budget, not a measurement of free board RAM, stack headroom, NVS availability, or safe device capacity. Those remain NOT_RUN for #48/#58.

## Channel close and send-start boundary

Each accepted report keeps independent MQTT and Webhook states. A successful close commit immediately cancels only matching PENDING work that has not entered that channel's transport call. First-attempt work becomes CANCELLED; work with an earlier failed attempt becomes RETRY_SUPPRESSED. A disabled channel on a newly accepted report is DISABLED. Query and report status expose these results.

Pump selection, `in_flight`, and a READY result are not send start. After each readiness check, the service reacquires its lock, verifies the entry is still pending, reads the committed policy and revision, and checks the enabled bit immediately before invoking that transport callback. The callback must call `app_report_service_transport_entered(channel)` as its first operation; that marks the channel in-call and releases the service lock. The adapter then rechecks policy with `app_report_service_transport_start(channel)` and reserves the attempt with `app_report_service_transport_begin(channel)`. The production MQTT/Webhook primitive is called only after `transport_begin` returns. A close before `transport_entered` prevents the callback, and a close before `transport_begin` prevents the primitive. If a close observes a pending, in-call channel after `transport_begin`, it returns BUSY without committing; the caller retries after that channel callback returns and its outcome is recorded. Thus disable cannot return APPLIED in the begin-to-primitive window. The network primitive remains outside the report lock. Each channel outcome is applied as its callback returns, so a completed MQTT callback does not keep Webhook busy, and vice versa. A started operation resolves as success, failure, or UNKNOWN and is never relabeled CANCELLED. Reopening never changes terminal CANCELLED or RETRY_SUPPRESSED entries back to pending.

An unreadable policy at the final gate becomes POLICY_UNKNOWN and prevents a new transport call. The reported policy generation is the committed per-identity revision checked at that gate.

## #83 purge seam

The internal signature is:

```c
int32_t app_report_service_policy_purge(
    const app_report_policy_identity_t *identity,
    uint64_t cleanup_intent,
    app_report_policy_status_t *out);

int32_t app_report_service_policy_purge_status(
    const app_report_policy_identity_t *identity,
    uint64_t cleanup_intent,
    app_report_policy_status_t *out);
```

The trusted cleanup caller supplies the saved full identity and a nonzero stable intent, so cleanup can locate the App after its low-level native install record has been removed. The top bit `APP_REPORT_POLICY_PURGE_INTENT_ADMIN` marks an authorized Host uninstall intent; only the trusted internal cleanup caller may set it, and the remaining bits carry its stable intent value. This permits that caller to attribute and clear unbound reports accepted in the current session generation without a UI policy query or commit. Unbound reports from earlier session generations remain unattributed: they block purge with BUSY and are never deleted by binding a later TBS identity. A successful purge with a stored policy persists a tombstone containing that identity and intent, then clears only that identity's volatile report entries and seen attribution. When no policy record exists, the same trusted purge can complete by clearing eligible current-generation RAM entries; those entries are volatile and are lost on service restart. A retry with the same identity and intent is idempotent across service restart and identical reinstall; it will not remove new policy or reports created after that intent completed. A stale intent for an older TBS identity cannot delete a newer TBS policy or report queue, even when publisher ID, App ID, and publisher key are unchanged. A later uninstall uses a new intent. While a purge is unresolved, a different identity or intent returns BUSY; the same intent can retry. A transport already in-call makes purge return BUSY/PENDING. Purge blocks new admission and prevents not-yet-started channel calls for the target identity. The tombstone is bounded by the same 16-record store; a full store rejects a new tombstone and never reports completion. The operation does not set the installer state or declare UNINSTALLED. #83 remains the sole owner of the overall uninstall result.

Read-only purge status takes the expected cleanup intent and verifies it against the CRC-checked record and matching queue state. Storage failure or an intent mismatch never reports completion. Same-identity reinstall after successful purge receives defaults. Other publisher identities and other policy records remain unchanged.

## Independent host verification

`tests/app_report/test_app_report_policy.c` drives the production service and policy-store C sources through fake storage and transport callbacks. It covers current session and full-identity checks, TBS replacement, CAS conflict, capacity range and shrink conflict, per-App quota versus shared queue exhaustion, restart behavior, preselected and in-call close races, per-channel cancellation, retry suppression and reopen, storage write/read/CRC/partial-write recovery, cross-publisher seen-state preservation, and purge retry/idempotence/reinstall behavior including stale intent replay after restart.

The core existing `app_report` suite remains at 552 checks. Fake NVS and transport passes do not establish real board RAM headroom, NVS durability/wear, actual MQTT/Webhook delivery, or end-to-end behavior. Those are NOT_RUN and remain for #48/#58.
