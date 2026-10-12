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
- High-privilege operations refuse via `web_server_high_privilege_allowed(ctx)` unless the request is authenticated with a non-default credential and the session credential is trusted. Currently enforced on `POST /device/storage/format` (the destructive LittleFS format). Refusal is a 403 with an explicit message.
- Gate matrix (negative cases covered by tests): missing header / empty creds → 401; wrong user or password → 401; trusted default credential → ordinary flows OK, format refused; custom persisted credential → format allowed; untrusted media or corrupt record → everything refused; password change revokes sessions; expired sessions are refused (sliding expiry, `AUTH_SESSION_TIMEOUT_MS`).

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

- `tests/storage_gate/test_admin_auth` (new, AddressSanitizer-instrumented for auth objects): 285 checks / 0 failures. Covers record codec, weak-hash rejection (legacy mix output is not an identity), corrupt-record fail-closed and recovery via explicit factory reset, default-record persistence (non-plaintext at rest), export/dump redaction, import-identity-ignore with non-sensitive merge preserved, export/import round-trip, `set_config` failure propagation, persist-failure fail-closed with reboot consistency, verdict gate matrix, untrusted-media fail-closed, session expiry/revocation/limits, handler-level `/change-password` and `/login` negatives, sensitive-key predicate, legacy migration with unpersistable-write rejection.
- UAF regression is real: with the fix temporarily reverted, the suite aborts under AddressSanitizer (`heap-use-after-free ... in auth_mgr_change_password`); with the fix, 0 failures.
- `tests/storage_gate` main suite: 802 checks / 0 failures (unchanged #37 baseline).
- `tests/storage_gate` e2e suite: 267 checks / 0 failures (was 251; +16 checks added: format-gate matrix and record-at-rest assertions; all #37 media-gate/fail-closed/aggregation semantics kept green).
- `tests/app_host`: 21 checks / 0 failures.
- Target firmware: fresh `rm -rf Appli/build && make app -j16` passes (`ne301_App.elf` linked).

## 7. Residual limitations / non-goals honored

- No transport security is added or claimed: `Authorization: Basic` credentials remain readable on the wire. Per the A decision, high-risk administration is only meaningful in an isolated lab network; public/LAN/production safety is out of scope.
- The per-request verdict and the handler gate are exercised through the compiled verdict API, handler tests, and target build; `web_server.c` itself is not host-compiled (mongoose dependency), so `auth_verify_user`'s own lines are verified by review plus target build, not a host test.
- WiFi, MQTT, RTSP, webhook and cellular secrets remain in export/CLI output — this issue's contract covers admin identity material; those exits are recorded as an escalation finding.
- Salt entropy is time/address-based (no TRNG in this path). PBKDF2 iteration count and record format are fixed constants; a future iteration count bump would need a record version bump (the `A1:` prefix is the version hook).
- Session expiry uses the existing monotonic-fake timestamp source; sessions are an existing unused-by-Web API kept correct (deadlock fix, revocation), not a new session protocol.
