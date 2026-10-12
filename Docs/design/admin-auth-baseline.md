# Admin Authentication Baseline (Issue #36)

Status: implemented on `agent/36/1c70839b` (execution base `e1ec3e1c` = merged #37 storage-safety baseline).
Scope of evidence: host-isolated automated tests plus an `arm-none-eabi-gcc` target cross-build.
No physical device, no network reachability, and no production-safety claim is made or implied.

## 1. Threat-adjacent defects found on the base (all fixed here)

| Site | Defect on `e1ec3e1c` | Fix |
| --- | --- | --- |
| `Custom/Core/Security/auth_mgr.c` boot path | Logged the admin plaintext password at init; verified with a 32-bit mix+LCG expansion (non-cryptographic); `auth_mgr_get_password()` handed plaintext to any caller | Plaintext log removed; weak hash deleted entirely; plaintext getter removed; credentials now held as salted PBKDF2 records |
| `Custom/Services/Web/web_server.c` `auth_verify_user()` | Logged the full `Authorization` header and the parsed password on every protected request | Header/password logging removed; verification routed through the reusable verdict API; password buffer wiped after use |
| `Custom/Core/System/json_config_json.c` `serialize_auth_mgr` / `parse_auth_mgr` | Export put the admin password into config JSON served by `/device/config/export` (and `config_show` CLI); import accepted `admin_password`, letting a plain config import replace the admin identity | Export emits only non-sensitive fields plus `admin_password_configured` boolean; import ignores all admin identity fields (old backups neither reset nor replace the credential) |
| `Custom/Core/Log/cli_cmd.c` `fget_cmd`, `Custom/Hal/storage.c` `storage_nvs_dump` | Echoed raw NVS values, including `auth_password` / `dev_info_password` | Shared denylist predicate `json_config_nvs_key_is_sensitive()` (in the #37 boot-gate unit); both exits print `(redacted)` for those keys |
| `Custom/Services/Web/api/api_auth_module.c` `change_password_handler` | Used the `password` pointer from `web_api_get_string()` (a cJSON-internal `valuestring`) after `cJSON_Delete(request)` — a confirmed use-after-free reachable from `/change-password` | Password copied into a stack buffer before the JSON tree is deleted; handler additionally refuses unauthenticated contexts (401) before touching credentials; regression-covered under AddressSanitizer |
| `Custom/Core/System/json_config_mgr.c` `json_config_set_config` | Ignored the `json_config_save_to_nvs()` result and always returned OK | Persists the candidate first; on failure returns the real error with in-RAM config untouched (no RAM/NVS split, no fake success) |
| `auth_mgr_login()` lock recursion | Called `auth_mgr_verify_password()` while holding the non-recursive CMSIS mutex (static deadlock risk on target) | Internal unlocked `auth_mgr_verify_locked()` used under the held mutex |

## 2. Credential storage design

Record format (stored at NVS key `auth_password`, ASCII, 101 bytes):

```
"A1:" hex(salt[16]) ":" hex(pbkdf2_dk[32])
```

- KDF: PBKDF2-HMAC-SHA256, 4096 iterations, 16-byte salt, 32-byte derived key (`auth_mgr_credential_record_encode/verify` in `Custom/Core/Security/auth_mgr.c`).
- Hash choice reasoning: MbedTLS is already vendored, configured (SHA-256 + MD enabled) and linked into the app (`Appli/Makefile`), so the marginal cost is zero; PBKDF2-HMAC-SHA256 at 4096 iterations is a standard, memory-cheap KDF (constant ~100 B of SHA state, no large RAM block like Argon2 requires), which fits the STM32N6 web-task budget where verification runs per request. Two PBKDF2 verifications per authorized request cost a few milliseconds on the Cortex-M55 and are accepted for this baseline.
- Salt entropy is derived from kernel tick, a counter, and address entropy hashed through SHA-256. The chip has no entropy source wired into this path, so the salt defends against precomputation/rainbow tables but is not claimed to be cryptographic-RNG quality. This is a documented residual limitation, consistent with the isolated-lab scope.
- Verification never stores or compares the weak legacy mix: that function was removed. Any value that starts with `A1:` but does not parse as a valid record is treated as corrupted and fails closed (no default fallback, no literal-string authentication).
- Values that are not records are legacy plaintext credentials (both legacy NVS keys are honored: `auth_password` primary, `dev_info_password` fallback), compared in constant time. This preserves authentication for healthy pre-existing devices.

## 3. Lifecycle and persistence rules

- Blank first boot: persists a record of the default password (`AUTH_DEFAULT_PASSWORD`), gated by the #37 `credential_trusted_persisted` gate with write + read-back verification. Default config, Web login and `/change-password` semantics are unchanged.
- Legacy migration (two layers):
  1. Load-time: if only the legacy old key exists, its plaintext is copied into RAM and a record is persisted with read-back verification (failure keeps the #37 aggregated-failure semantics and fails closed). On success the old key is deleted (best effort).
  2. Authentication-gated upgrade: the first successful verification of a still-plaintext credential (init, login, or any per-request verification) re-persists it as a record via the same verified path, once per boot. Failure keeps the legacy credential working; nothing is faked.
- Password change (`auth_mgr_change_password` → `json_config_set_device_password`): encode → NVS write → read back → record verify → only then commit RAM and revoke all sessions. Any failure returns an error, leaves the previous credential authoritative in both RAM and NVS, and never reports success (no reboot credential split).
- Full-config save/import paths no longer write the credential key at all; the credential changes only through the dedicated verified path. Factory reset (`json_config_reset_to_default`) re-persists the default credential explicitly and truthfully reports failure.

## 4. Reusable admin verdict and high-privilege gate (AC3)

- `auth_verdict_t auth_mgr_authorize(username, password)` returns `{authenticated, default_password_active, high_privilege_allowed}` and is the single reusable decision for future App management operations. It refuses missing/empty/oversized inputs, wrong username, wrong password, and any untrusted-credential state (fail closed).
- `auth_mgr_default_password_active()` is true exactly when the known public default authenticates; a custom password that happens to equal the default is indistinguishable and intentionally treated as default.
- Per-request Web enforcement: `auth_verify_user()` now refuses missing header, missing creds, invalid/expired credentials, and untrusted state, and records the verdict in the request context (`ctx->authenticated`, `ctx->high_privilege`).
- High-privilege operations refuse via `web_server_high_privilege_allowed(ctx)` unless the request is authenticated with a non-default credential and the session credential is trusted. Currently enforced on `POST /device/storage/format` (destructive LittleFS format) and `POST /system/factory-reset` (destructive full reset including the admin identity). Refusal is a 403 with an explicit message.
- Gate matrix (negative cases covered by tests): missing header / empty creds → 401; wrong user or password → 401; trusted default credential → ordinary flows OK, format and factory-reset refused; custom persisted credential → format and factory-reset allowed; untrusted media or corrupt record → everything refused; password change revokes sessions; expired sessions are refused (sliding expiry, `AUTH_SESSION_TIMEOUT_MS`).

## 5. Secret-exit redaction map (AC1)

| Exit | Behavior now |
| --- | --- |
| Boot/auth logs | No password, no Authorization header, no record contents logged |
| `/device/config/export` + `config_show` CLI | `auth_mgr` section carries only `session_timeout_ms`, `enable_session_timeout`, `admin_password_configured` |
| Config import | `auth_mgr.admin_password` ignored (identity cannot be imported or reset) |
| `fget <key>` / full NVS dump | `auth_password`, `dev_info_password` printed as `(redacted)`; other keys unchanged |
| API responses | No handler returns credential material; error envelopes unchanged |

Non-sensitive diagnostics (log level, thresholds, network SSIDs, timeouts) are unchanged.

## 6. Test evidence (host, offline)

- `tests/storage_gate/test_admin_auth` (new, AddressSanitizer-instrumented for auth objects): 469 checks / 0 failures (was 285; +184 checks added by the correction round: factory-reset gate matrix, write-success/read-back-failure rollback, rollback-unproven fail-closed + reboot + recovery, change-interruption marker fail-closed, reset-to-default failure ordering). Covers record codec, weak-hash rejection (legacy mix output is not an identity), corrupt-record fail-closed and recovery via explicit factory reset, default-record persistence (non-plaintext at rest), export/dump redaction, import-identity-ignore with non-sensitive merge preserved, export/import round-trip, `set_config` failure propagation, persist-failure fail-closed with reboot consistency, verdict gate matrix, untrusted-media fail-closed, session expiry/revocation/limits, handler-level `/change-password` and `/login` negatives, sensitive-key predicate, legacy migration with unpersistable-write rejection.
- `tests/storage_gate/test_auth_boundary` (new, AddressSanitizer-instrumented): 83 checks / 0 failures. Runs the REAL mongoose `mg_http_parse` + `mg_http_creds` (host-compiled from `Custom/Common/Lib/mongoose/mongoose.c`, TLS disabled) end-to-end into `auth_mgr_authorize()`: Basic-header decode at password lengths 8/31/32 after real `change_password` calls, old-credential invalidation after each change, decode boundary capacity (`AUTH_BASIC_DECODE_BUF_LEN == 65` holds the accepted 32-char maximum + NUL), the legacy 32-byte buffer truncation bug reproduced and refused, oversized/illegal/truncated credentials refused without false accept, wrong username, empty password, non-Base64 garbage, and reboot persistence of a 32-char credential.
- UAF regression is real: with the fix temporarily reverted, the suite aborts under AddressSanitizer (`heap-use-after-free ... in auth_mgr_change_password`); with the fix, 0 failures.
- `tests/storage_gate` main suite: 802 checks / 0 failures (unchanged #37 baseline).
- `tests/storage_gate` e2e suite: 267 checks / 0 failures (was 251; +16 checks added: format-gate matrix and record-at-rest assertions; all #37 media-gate/fail-closed/aggregation semantics kept green).
- `tests/app_host`: 21 checks / 0 failures.
- Target firmware: fresh `rm -rf Appli/build && make app -j16` passes (`ne301_App.elf` linked, 3458440 text).

## 7. Correction round: high-privilege factory-reset gate (review blocker 1)

`POST /api/v1/system/factory-reset` executes `device_service_reset_to_factory_defaults()` (resets configuration, models and the admin identity). On the reviewed candidate it was reachable with the public default credential. It now refuses via the same verdict path as `/device/storage/format`:

- Handler gate: `web_server_high_privilege_allowed(ctx)` before any body parsing; refusal is 403 `High-privilege administrator authentication required`.
- Route unchanged (`require_auth = AICAM_TRUE`); no new claim system, no device-privilege expansion. Healthy devices keep the authorized explicit factory reset working.
- API-level negatives (in `test_factory_reset_high_privilege_gate` / `test_factory_reset_refused_when_media_untrusted`): unauthenticated → 403; default-credential session (`high_privilege == AICAM_FALSE`, derived from `auth_mgr_authorize("admin", AUTH_DEFAULT_PASSWORD)`) → 403; authenticated-without-privilege → 403; wrong method → 405; `delay_seconds` out of range with privilege → 400; valid authorized request → 200 and reset initiated; media-untrusted boot → `auth_mgr_authorize` returns unauthenticated even for the real stored password, so no privileged session can exist and the handler refuses.

## 8. Correction round: per-request credential decode boundary (review blocker 2)

Defect: `api_auth_module.c` accepts new passwords of 8..32 characters, but `auth_verify_user()` decoded the Basic header into `char password[32]`; `mg_http_creds` NUL-terminates, so a legal 32-character password was truncated to 31 on every later request — protected Web APIs never authenticated again after a legal change (and a 31-character prefix falsely authenticated the 32-character credential, a truncation false-accept).

Fix (product limit stays 8..32, unchanged):

- `auth_mgr.h` defines `AUTH_BASIC_DECODE_MAX_LEN = AUTH_MAX_PASSWORD_LEN (64)` and `AUTH_BASIC_DECODE_BUF_LEN = 65`; any storable credential (≤ 32) plus the verify-side cap (≤ 64) decodes without truncation.
- `Custom/Services/Web/web_server.c` `auth_verify_user()` and `Custom/Services/Web/api/api_file_module.c` upload auth decode use the new size (both were 32-byte buffers; those were the only two `mg_http_creds` decode sites in the codebase). Longer inputs are refused by length/compare, never truncated into a matching value.
- Regressions: see `tests/storage_gate/test_auth_boundary` in section 6 (real HTTP Basic headers through the real mongoose parser; 8/31/32 end-to-end; truncated/illegal refused; old credential invalid after change; 32-char credential survives reboot).

## 9. Correction round 2: write-ahead intent transaction for credential persistence (review blocker 4)

Round-1 residual rejected by review: the round-1 design wrote the pending marker AFTER the new record, so a power loss (or verify-fail plus marker-write failure) could leave new-record-on-flash with no marker — the next boot would trust a credential whose commit was never proven, and "API failed + old password valid this session + new password valid after reboot" remained an undecidable state. The governing invariant: only a trusted complete commit may change the admin identity accepted at the next boot; any failure must not produce an old-RAM/new-NVS privilege split.

### 9.1 Transaction discipline (write-ahead intent)

Implemented in `json_config_nvs_persist_credential_record` and `json_config_load_from_nvs` (`Custom/Core/System/json_config_nvs.c`):

1. Capture the previous raw `auth_password` value before any write; if it cannot be read at all, refuse with NVS untouched (all-old).
2. Encode the new record locally.
3. WRITE-AHEAD INTENT first: write `auth_password_pending` = `P1:<crc32-hex>:<previous raw value or empty>` — a checksum-wrapped copy of the previous credential — and prove it by read-back before any new-record byte reaches NVS. The CRC makes the intent self-certifying: torn or corrupt intent data fails validation at boot and fails closed instead of restoring garbage as a credential. Intent-write failure paths: marker provably absent (read-back NOT_FOUND, possibly after a proven delete) → report failure, all-old, record untouched; marker presence unprovable → undecided fail-closed (record still untouched, so boot can only see all-old or fail-closed, never new).
4. Write the new record; prove by read-back bytes plus `auth_mgr_credential_record_verify` against the new password. Proof failure → rollback: restore the previous raw value (or delete the key when there was none) with read-back proof, then clear the intent marker with absence proof; both proven → report failure, all-old everywhere. Otherwise leave the intent standing (session undecided fail-closed; the next boot resolves it).
5. COMMIT POINT: clear the intent marker and prove absence. Only now is AICAM_OK returned — the record is verified on flash and no marker stands, so every later boot trusts the new identity. Marker-clear unprovable → commit unproven: session undecided fail-closed, next boot resolves the standing intent back to the previous credential.
6. Boot-side resolution (`json_config_nvs_credential_intent_resolve`, called at the top of the credential section of `json_config_load_from_nvs`): marker absent → normal load. Marker present with a valid CRC-wrapped intent → the intent is a rollback order: restore the previous value (or ensure the key absent when the intent payload is empty) with read-back proof, then clear the marker with absence proof, then load the restored credential normally. Resolution is idempotent under repeated power loss (restore-then-clear ordering means an interrupted resolution simply redoes the no-op restore next boot). Marker unreadable or CRC-invalid → fail-closed (no authentication, no default) until reset-to-default. Any resolution proof failure leaves the marker standing and fails closed, retrying next boot.

In-session coherence: a reported failure leaves either proven all-old (session keeps the old credential, boot restores the same old credential) or undecided fail-closed (session refuses everything; boot resolves to all-old or stays fail-closed). A reported success exists only after the proven commit point. The undecidable round-1 split ("API failed, old valid now, new valid after reboot") is structurally impossible: while the marker stands, boot never trusts the record it finds, marker-absent trust requires the proven marker clear, and the new record only ever survives reboot with a proven marker clear.

### 9.2 Failure-point matrix and proof map

Every step of the sequence is fault-injected or power-loss-sliced in `tests/storage_gate/test_admin_auth.c`; after each injection the flash image is rebooted (fresh NVS session + manager/auth init) and the accepted admin credential state is asserted decidable and unmixed (all-old, committed-new, committed-default, or fail-closed):

| Injection point | Post-reboot accepted credential | Evidence |
|---|---|---|
| Before marker write (write fails / power loss) | all-old (record untouched, marker proven absent) | `test_persist_failure_no_fake_success`, slice k=1 drop |
| Marker written, record not yet | fail-closed (next boot resolves intent to previous) | slice k after marker ops |
| Record written, verify fails (read-back fault) | all-old (rollback proven in-session) or intent-resolved all-old | `test_persist_readback_failure_rolls_back_all_old`, `test_persist_rollback_unproven_failclosed_and_recovery` |
| Rollback unproven (media fault) | session fail-closed; next boot resolves intent to previous | `test_persist_rollback_unproven_failclosed_and_recovery` |
| Marker-clear pre-delete probe fails | intent standing; session undecided; boot resolves all-old | `test_commit_marker_clear_proof_failures` (mode 2) |
| Marker deleted, clear-proof read fails | commit residue: session undecided fail-closed; boot sees proven marker absence → committed-new (never mixed, nobody holds old-RAM acceptance) | `test_commit_marker_clear_proof_failures` (mode 3) |
| Power loss at ANY flash op boundary of the change (drop-before and apply-after, k = 1..14) | classified from raw flash: marker present → resolved all-old; marker absent + record unchanged → all-old; marker absent + new record → committed-new; assertion that old+new acceptance never coexist | `test_powerloss_slices_change_reboot_consistency` |
| Same slicing applied to reset-to-default commit (k = 1..20) | all-old (previous custom credential) or committed-default; never mixed | `test_powerloss_slices_reset_reboot_consistency` |
| Same slicing applied to legacy migration persist during boot (k = 1..10) | migration always converges: legacy credential active as verified record, marker absent | `test_powerloss_slices_legacy_migration` |
| Change interrupted by reboot mid-commit (marker left by older/hand-written state) | fail-closed until reset-to-default when intent is absent/invalid | `test_change_interruption_marker_failclosed` |
| Corrupt intent payload (fails `P1:` CRC validation) | fail-closed until reset-to-default, never restored as a credential | `test_change_interruption_marker_failclosed`, CRC validation in `json_config_nvs_credential_intent_payload_valid` |

Bounded check of `json_config_reset_to_default` unchanged from round 1 and re-proven under the new discipline: credential-persist failure refuses the whole reset with the previous credential intact (`test_reset_to_default_credential_failure_ordering`, including raw NVS byte comparison and reboot resolution to the previous credential); a config-save failure after a proven credential commit leaves the default committed (decidable at reboot, no false success).

## 10. CI `Check docs sync` failure: root-cause evidence (review blocker 3, reproduction only — no docs edits)

Reproduced in an isolated detached worktree at the reviewed candidate `fe5d422c` (main worktree docs untouched, temp worktree removed afterwards):

- `python3 Script/gen_web_api_docs.py --check` exits 1 with `::error::Web API 已变更，但文档端点参考未同步` listing exactly two files: `docs/public/web-api/endpoints-manifest.json` and `docs/web-api/endpoints/manifest.json`. All generated `*.md` endpoint pages match (generated diff shows zero `.md` changes — route tables are identical).
- The entire generated diff is two `source_sha256` lines (16-hex prefixes), duplicated in both manifests:
  - `auth`: `d866579fde8b0901` → `b226d056142f00db` (`Custom/Services/Web/api/api_auth_module.c`)
  - `device`: `f8148e8894b66400` → `601663e0c60f4a67` (`Custom/Services/Web/api/api_device_module.c`)
- Root cause confirmed as analyzed by A: `Script/gen_web_api_docs.py:297–305` stamps each API module's raw `*.c` byte hash into both manifests. Any source-byte change to an API module (even route-unchanged, as in this auth-hardening work) mismatches the committed manifests. The genuinely needed fix is regenerating those two lowercase `docs/**` manifests at the final delivered SHA (`python3 Script/gen_web_api_docs.py` or `cd docs && npm run sync`) — that path is outside this task contract's scope (`Docs/design/**` only), so the one-line-each regeneration is left as A's explicit scope decision. CI on the PR stays red until A acts; no `.github/**` change and no check skip was made.

## 11. Residual limitations / non-goals honored

- No transport security is added or claimed: `Authorization: Basic` credentials remain readable on the wire. Per the A decision, high-risk administration is only meaningful in an isolated lab network; public/LAN/production safety is out of scope.
- The per-request verdict and the handler gate are exercised through the compiled verdict API, handler tests, and target build; `web_server.c` itself is not host-compiled (server wiring), so `auth_verify_user`'s glue lines are verified by review plus target build. The decode boundary inside it IS host-tested: the correction round links the real mongoose `mg_http_parse`/`mg_http_creds` into `tests/storage_gate/test_auth_boundary` and drives the full decode-to-`auth_mgr_authorize` chain with real HTTP Basic headers.
- WiFi, MQTT, RTSP, webhook and cellular secrets remain in export/CLI output — this issue's contract covers admin identity material; those exits are recorded as an escalation finding.
- Salt entropy is time/address-based (no TRNG in this path). PBKDF2 iteration count and record format are fixed constants; a future iteration count bump would need a record version bump (the `A1:` prefix is the version hook).
- Session expiry uses the existing monotonic-fake timestamp source; sessions are an existing unused-by-Web API kept correct (deadlock fix, revocation), not a new session protocol.
