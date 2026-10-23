# NEUI v1 Sidecar lifecycle

Issue #83 adds Host-managed Sidecar admission, durable activation, query, recovery, and uninstall coordination. It uses the already merged Host #86 verifier and does not change native NEAPP v1/v2/v3 admission. The endpoint returns a read-only descriptor; it does not create a browser loader or claim that a browser has loaded the assets.

## State and persistence

The lifecycle accepts a candidate only after the committed native install record, publisher key hash, publisher/app IDs, and native TBS match the supplied trust context. It stores the bundle by whole-bundle SHA-256, verifies the stored bytes with `app_sidecar_verify_bundle`, writes each asset under a publisher/app/key/native/UI-TBS-derived path, then reads the committed bundle and every asset back and compares authenticated content before returning ACTIVE.

The state sequence is `CANDIDATE_STAGED → VERIFIED → durable ACTIVE`. A CRC-protected staging record and two CRC-protected generation slots retain the candidate and displaced metadata through interrupted writes. Recovery validates the highest committed slot and all referenced bytes against the current committed native identity. Invalid or unreadable metadata/content produces UNKNOWN or UNAVAILABLE. A changed native identity revokes the prior service generation before old Sidecar resources can be served. Generation changes at boot recovery and identity change, so consumers can reject stale capabilities. A same-version, different-UI-TBS candidate is rejected as a conflict; a downgrade is rejected; a failed upgrade preserves the old matching ACTIVE version.

Storage paths are `/apps/ui-b_<bundle-sha256>.neui` for the signed package and `/apps/ui-a_<identity-and-ui-tbs-hash>_<index>.bin` for expanded assets. The descriptor exposes the signed UI TBS, semantic version, ABI, generation, identity/key/native TBS, and verified asset paths and hashes. Consumers must not accept a browser-supplied identity as authority.

## Uninstall barrier

DELETE requires `confirm_delete_data: true`. Missing or false confirmation returns `cancelled` with `side_effects: false` before storage access. After confirmation, a CRC-protected uninstall intent is durably written first. Boot resolution and Sidecar install/query are blocked while intent remains. Local cleanup removes the committed native record, install pending marker, native package, publisher/app state blobs and metadata, candidate/active Sidecar bundles and assets, and recovery metadata. The intent is removed last, only after the external purge seam confirms success.

The `app_installer_device_cleanup_ops` seam currently reports UNKNOWN. Therefore production device API reports `cleanup_pending` until #88 supplies and proves its purge callback. Tests use a fault-injected fake callback to exercise confirmed completion, retry, and idempotency. Uninstall is scoped to the matching publisher/app/key identity and does not clear other apps or global MQTT/Webhook configuration. A pending in-flight external send cannot be retracted by this API.

## Limits and host memory bound

The accepted NEUI v1 limits are 64 files, 1 MiB per file, 2 MiB total payload, and a 16 KiB manifest. The lifecycle checks free space before staging using `2 * (bundle_len + payload_len) + 2 * 224 + 65,536` bytes. The two 224-byte records cover active/rollback metadata; the additional 65,536 bytes are reserved for conservative cleanup/GC work. The test rejects one byte below this budget and accepts the exact budget for a 64-file, 2 MiB fixture containing one 1 MiB file.

The verifier state is fixed-size and bounded by the existing Host #86 assertion `sizeof(app_sidecar_verifier_t) <= 20,480`. The largest lifecycle heap copies are the caller's candidate and one read-back bundle, each bounded by 2,113,624 bytes; record verification additionally allocates at most one 1 MiB asset at a time. The endpoint caps and checks Base64 input before decoded allocation. These are offline software bounds, not measurements of target RAM, allocator fragmentation, filesystem overhead, or device installability.

## Acceptance evidence

### AC1 — App #26 / Host #86 mutual verification

| Direction | Artifact and identity | Result |
| --- | --- | --- |
| App #26 golden → Host #86 C verifier and lifecycle | Bundle SHA-256 `9ec5e44616e197dfa0b3d16a5f9b0f280b523883be2c1cddf4f363126e8f79d4`; UI TBS SHA-256 `aa8130ea20737d457bb6ace49871daa82e85338c7b2e2da656edcc9b7f3fe02f`; 968 bytes; 4 files | PASS; committed-record fixture carries `test-publisher/line-crossing`, publisher SPKI SHA-256 `5cd252fb0ce8932436faf8ccd1040981b89ee4ad6b9fe9e2a2b7e71aacb27cd3`, native TBS SHA-256 `d793a455e3ccda715c8ee9ae95a718cf0fc2627c0ff20969f23e7ea9349b349e` |
| Host-controlled fixture → App #26 offline verifier | Bundle SHA-256 `e5d43d9be2bfb32c0978b4b586da7d75aab4214c2203f0d12f14ee78cd1885a1`; UI TBS SHA-256 `91c392f993521c11ebfd8ce566237337d8e46bebcffe9f0ede95201517f263ee`; version 1.0.1; same non-production d=1 test key and native TBS | PASS; App verifier reports native binding trusted when native package and native trust are supplied |

The App #26 source is read-only commit `84178d9d15d29b35712e6a90767de9d0bb30f89a`. Host vectors include its committed `golden.neui`, `golden.neapp.v3`, and `test-publisher.spki.der`. The C test verifies the native package with Host admission, then stores the derived native identity in the in-memory committed install-record slot; this is an offline integration fixture, not a device install. The test key is public/non-production and is not a device production root.

App #26 verifier command, run from the NE301 repository root after extracting that commit to `/tmp/ne30x-app-issue83-84178d9d`:

```sh
python3 /tmp/ne30x-app-issue83-84178d9d/package/sidecar/neui_verify_v1.py \
  --bundle tests/app_sidecar_lifecycle/build/golden.neui \
  --trust tests/app_sidecar_lifecycle/test-publisher-trust.json \
  --native-package tests/app_sidecar_lifecycle/build/golden.neapp.v3 \
  --native-trust tests/app_sidecar_lifecycle/test-publisher-trust.json \
  --expect PASS --compact
```

Replace `golden.neui` with `host_controlled.neui` for the reverse direction. Both runs return PASS with `native_binding_trusted: true`. Host C evidence is `test_app_sidecar_lifecycle:test_app26_golden_host86_interop`; Host #86's existing negative suite covers bad signatures/foreign keys, changed signed manifest, payload tamper/reorder, ABI and version rules, path traversal/absolute/backslash/duplicate/case collisions, missing root index, publisher/app/key mismatches, and native TBS mismatch. Both Host and App accept `docs/index.html`; the lifecycle fixture rejects a duplicate root `index.html`.

### AC2 — bounded activation and recovery

`test_app_sidecar_lifecycle` covers stage/query/ACTIVE, persistent query, boot generation rollover, changed native identity refusal, same-version TBS conflict, downgrade refusal, retry/idempotency, and preservation of the previous ACTIVE version on a failed upgrade. ACTIVE is returned only after the committed record, signed bundle, version/ABI, and every expanded asset have been read back and verified.

### AC3 — offline storage fault injection

`mem_sidecar_backend.h` implements persistent files/NVS with injected write/read/stat/remove, NVS write/read/delete, short-write, and free-space failures. `test_storage_faults_fail_closed_and_recover`, `test_upgrade_conflict_retry_and_nested_index`, and `test_uninstall_nvs_delete_fault` cover interrupted staging, verification, metadata commit/readback, replacement, rollback/retry, CRC corruption, asset tamper, pending-marker recovery, and reboot resolver behavior. Injected uncertainty never returns ACTIVE. These are software restart/fault simulations; no physical power-cut test was run.

### AC4 — uninstall coordination

`test_confirmed_uninstall_purge_gate_and_reinstall` verifies cancellation before side effects, intent-first blocking, failed file removal, UNKNOWN external purge, retry to confirmed cleanup, idempotency, no state resurrection after same-identity reinstall, and fresh Sidecar install after removal. `test_uninstall_nvs_delete_fault` verifies persistent retry after an NVS delete error. The real #88 transport/report purge callback is not present, so device status remains CLEANUP_PENDING/UNKNOWN.

### AC5 — bounded admission

`test_limit_admission_and_budget` verifies protocol maxima, exact calculated free-space threshold, one-byte-below refusal, and no writes on refusal. `test_app_sidecar` retains Host #86's bounded verifier-state assertion and protocol-negative vectors. Corrupt signature/content/path/ABI/version and identity cases fail closed. True device free space and peak RAM remain for the #48/#58 integration gate.

## Run commands

```sh
make -C tests/app_sidecar test
make -C tests/app_sidecar_lifecycle test
```

The lifecycle test executable currently reports 658 checks and zero failures. Run the existing installer, admission, state, Host run/report/session suites before delivery; target-device FS/RAM/power, production signing, browser loading, and a real #88 purge remain NOT_RUN.
# Retired by Issue #136

This document records the former signed Sidecar lifecycle design. Host no longer accepts its signed UI bundle format. The active unsigned single-file package format and its preserved-state policy are defined in [app-unsigned-single-file-format.md](app-unsigned-single-file-format.md).
