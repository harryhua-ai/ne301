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

## 9. Correction round: decidable credential persistence under faults (review blocker 4, self-audit supplement)

Fault family proven real by injection: the old `json_config_nvs_persist_credential_record` wrote the new record, then read it back; on read-back failure it returned failure WITHOUT rollback — API reports failure, session keeps the old credential, but the next reboot loads the new record ("change failed" yet the change took effect).

Decidable-state semantics now implemented in `json_config_nvs_persist_credential_record` (`Custom/Core/System/json_config_nvs.c`):

1. The previous raw NVS value is captured before any write; if it cannot be read at all, the change is refused with NVS untouched (all-old).
2. New record is written; success is acknowledged only after read-back `auth_mgr_credential_record_verify`. On success any stale `auth_password_pending` marker is removed (a marker can only exist from an earlier interrupted attempt); failure to remove a real marker downgrades to the undecided path.
3. On any failure the current raw value is re-read (up to 3 attempts, absorbing transient read faults). If it provably equals the previous value, the failure is reported and the state stays all-old everywhere (API failure, session old, reboot old).
4. If the state is not provably old, an `auth_password_pending` marker is written, the previous raw value is restored (or the key deleted when there was none), the restore is proven by read-back, and the marker is removed: proven restore + removed marker = clean all-old failure.
5. Anything else (restore unproven, marker unwritable/unremovable) sets the undecided state: `json_config_nvs_credential_undecided()` returns true, `json_config_set_device_password` drops `credential_trusted` and clears RAM credential fields, `auth_mgr_change_password` clears the RAM credential, revokes all sessions and fails closed. Boot-side, `json_config_load_from_nvs` treats a present `auth_password_pending` key as an unresolved credential (fail-closed, no default fallback). All three views agree: nobody authenticates until `json_config_reset_to_default` re-persists a verified default record (the existing trusted recovery, which also removes the marker).

Guarantees: on any persistence failure the API view, this-session view and next-reboot view are identical (all-old, or fail-closed in the undecidable case) — never mixed; raw NVS stays trustworthy (rollback is verified by read-back, media bytes preserved); a subsequent successful retry after a clean failure completes the change. Retry-after-undecided is intentionally refused; the only recovery is the explicit reset-to-default, matching the existing untrusted-credential design.

Bounded check of `json_config_reset_to_default` ("persist default credential first, then save other config"): credential-persist failure refuses the whole reset with the previous credential proven intact (tested, including raw NVS byte comparison). A config-save failure after a successful credential persist returns failure (no false success) and leaves the credential state decidable (default at reboot, previous custom credential still valid for the current session) — the same partial-commit direction the function already documented; no false-success recognition exists. Covered by `test_reset_to_default_credential_failure_ordering`.

Interruption semantics: a crash between record write and rollback/marker-clear can leave the marker set; the next boot then fails closed (nobody authenticates) until reset-to-default — safe and decidable, covered by `test_change_interruption_marker_failclosed` and `test_persist_rollback_unproven_failclosed_and_recovery`.

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
