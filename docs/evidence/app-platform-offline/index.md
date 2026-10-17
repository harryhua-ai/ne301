# NE301 Offline App Platform Integration Evidence

**Evidence date:** 2026-10-17
**Disposition:** offline Host/App software evidence only. This is not real-device PASS, production trust acceptance, release approval, or final user acceptance.

## Reproduction

From the NE301 repository root:

```sh
python3 tests/app-platform-offline/run.py
```

The runner checks for pre-existing generated outputs before invoking legacy clean targets. It generates FSBL `1.0.3.1` and STEdgeAI `3.0.0` version headers in a temporary directory, makes the STEdgeAI shim visible to host compilation through `CPATH`, runs the C/API suites, and cleans only those test outputs. It disables Python bytecode output. It does not write under `Custom/**`, `Appli/**`, `Web/**`, `FSBL/**`, or `scripts/**`.

Historical browser evidence is re-audited from committed source and asset hashes. The command does not start Chrome; current browser rerun is `NOT_RUN`. Device tests are `NOT_RUN`.

## Frozen repositories and canonical inputs

GitHub was re-read on 2026-10-17. The NE301 `experiment/app-host-poc` branch tip and this worktree baseline both resolve to `884bb0ff0bea9b04534f91cd1c7c9af5a45c2798`. The `ne30x-app` `main` tip resolves to `27b768bdefb3283652e4ab3318cfe9f9dbf693e8`. App source and evidence were read from GitHub at that exact commit; the App repository was not modified.

| Repo / Issue | Canonical PR | Accepted candidate | Merge commit | Re-read state |
|---|---:|---|---|---|
| Host #39 | [#69](https://github.com/harryhua-ai/ne301/pull/69) | `350860b2b48e475d7e76e087474f7d7bf58a2760` | `1cf39ed87fb511fcd28c6443bf6a36f1e161fca3` | CLOSED + `ght:complete`, PR MERGED |
| Host #83 | [#97](https://github.com/harryhua-ai/ne301/pull/97) | `10fb2403c316a75f29397d58b5fd6f28fb69e590` | `fdefbc67abf13dbdbebbd71a89967d20e307486f` | CLOSED + `ght:complete`, PR MERGED |
| Host #99 | [#100](https://github.com/harryhua-ai/ne301/pull/100) | `91f88d03ffc7242d5c291247c36ee1cd414b104d` → `dd1acac075b681a85cb0a7b0b44aeb4f4b071cf4` | `884bb0ff0bea9b04534f91cd1c7c9af5a45c2798` | CLOSED + `ght:complete`, PR MERGED; final candidate is `dd1acac0…` |
| Host #86 | [#92](https://github.com/harryhua-ai/ne301/pull/92) | `a4cc62582e9db044898c1cce05e6fdf3f0146552` | `ab358eb150b4a77e16d64d81c04265c69b64be37` | CLOSED + `ght:complete`, PR MERGED |
| Host #44 | [#70](https://github.com/harryhua-ai/ne301/pull/70) | `d1bc3419aecd84c51b50a50114c258b0858b9756` | `b06ccae3a88f5292505999f139af610a27d5c214` | CLOSED + `ght:complete`, PR MERGED |
| Host #75 | [#77](https://github.com/harryhua-ai/ne301/pull/77) | `1a9df81d8c7c17c9f704c8ddd2a1f251b672680c` | `4026625e8f2bbd917f9db2b2979ef7c176752fb8` | CLOSED + `ght:complete`, PR MERGED |
| App #25 | [#31](https://github.com/harryhua-ai/ne30x-app/pull/31) | `ae5e9cbc0188d56e329b1198ce477019ab30a57c` | `27b768bdefb3283652e4ab3318cfe9f9dbf693e8` | CLOSED + `ght:complete`, PR MERGED; offline signed UI software only |
| App #26 | [#28](https://github.com/harryhua-ai/ne30x-app/pull/28) | `3086d6c57e65b83a743fff20a59e8f0986e5cb2f` | `84178d9d15d29b35712e6a90767de9d0bb30f89a` | CLOSED + `ght:complete`, PR MERGED; packer/verifier software only |

The host and app branch-tip checks are repeatable with:

```sh
git ls-remote origin refs/heads/experiment/app-host-poc
gh api repos/harryhua-ai/ne301/branches/experiment%2Fapp-host-poc --jq '.commit.sha'
gh api repos/harryhua-ai/ne30x-app/branches/main --jq '.commit.sha'
```

## Signed asset and trust-root index

| Item | Exact source | Result |
|---|---|---|
| App #25 signed counting NEUI | App #25 merge `27b768b…`, `docs/evidence/line-crossing-ui/line-crossing.neui`; byte-identical Host copy at `tests/sidecar-origin-study/input/line-crossing.neui` | 422,963 bytes; SHA-256 `3cd01519af881eb114c2831d67b4f0542815c98140e06deeba14e597d947771c` |
| Signed manifest native TBS binding | NEUI manifest bytes 80–111; verified by `app_sidecar_verify_bundle()` | 32-byte value `d793a455e3ccda715c8ee9ae95a718cf0fc2627c0ff20969f23e7ea9349b349e` |
| Signed UI TBS | App #25 provenance and independent Host C verifier | 422,892 bytes; SHA-256 `59f695608f3caa2a06ffae486e6288b3e3052c277a87c5a8a0a3cfb3a049e7a6`; signature DER length 71 bytes |
| Manifest and files | NEUI v1 parser/verifier | manifest 568 bytes; payload 422,308 bytes; 7 files; publisher `test-publisher`; App `line-crossing`; version `1.0.0`; Host UI ABI `1` |
| App test issuer SPKI | App #25 `tests/line-crossing-ui/evidence/test-publisher.spki.der`; Host copy `tests/sidecar-origin-study/input/test-publisher.spki.der` | 91 bytes; SHA-256 `5cd252fb0ce8932436faf8ccd1040981b89ee4ad6b9fe9e2a2b7e71aacb27cd3`; explicitly non-production dev/test identity |
| Current compiled Host native anchor | `Custom/Services/AppHost/app_trust_anchor.c` at Host `884bb0ff…` | publisher `ne301-exp-pub`; SPKI length 91; SHA-256 `06e732b1b94daeeef433297f7a8e797bf72b2fd3b882a4ec6a5490352931ebda`; `app_trust_anchor_status()` reports `PROVISIONED` in the host C test |
| Sidecar lifecycle test trust map | `tests/app_sidecar_lifecycle/test-publisher-trust.json` | Contains the App test SPKI `5cd252…`; this is test fixture trust, not the compiled native anchor |

The #99 Node source audit validates the exact App asset hash, manifest identity and native TBS binding, all seven embedded asset hashes and lengths, UI TBS digest, and P-256 signature using the copied public test key. The new Host C test calls the merged `app_sidecar_verify_bundle()` with that test key and checks the same metadata. Both return PASS for the **test issuer**.

The App test issuer SPKI (`5cd252…`) does not match the source-configured Host native anchor SPKI (`06e732…`), and the publisher identifiers also differ. Therefore the report is **`CROSS-ROOT UNKNOWN`**: no traceable same controlled keypair establishes that this signed App asset is authorized by the current Host native root. This test-issuer NEUI PASS is not Host native admission, device installation, or production signing PASS. The `native_binding` recorded by App #25 is App-side test-package evidence; it does not replace comparison with the Host anchor in commit `884bb0ff…`.

## Offline test matrix

The evidence command completed with exit `0`; all 11 `make test` directories passed. Individual printed counters are in [the test-run record](test-run-2026-10-17.md). The suites compile merged Host C and Web API modules; test storage, session and transport seams simulate hardware-facing behavior.

| Coverage | Reusable test source | Current result | Limit |
|---|---|---|---|
| v1/v2/v3 admission, signature and identity rejection | `tests/app_admission/**`; `app_admission.c`; `app_trust_anchor.c` | PASS | Test keys/vectors; no device key custody or production install |
| Installer, upgrade, content/version conflict, simulated recovery and Web API | `tests/app_installer/**`; `app_installer.c`; `api_app_install_module.c` | PASS | Memory I/O and fake device operations |
| Uninstall while report policy/purge integration is unfinished | Existing installer/lifecycle suites expose a fake purge seam only | **BLOCKED/NOT_AVAILABLE** | No real #88/#101 purge integration or uninstall DONE is claimed |
| Run eligibility and UNKNOWN-state refusal | `tests/app_host_run/**`; `app_host_run.c` | PASS | Host shim; no actual App process on hardware |
| Session/control/state behavior | `tests/app_host_session/**`; `tests/app_host_control/**` | PASS | Device seam and test request context; no deployed browser session |
| NEUI parsing, signature, asset hashes and callback boundary | `tests/app_sidecar/**`; `app_sidecar_verify.c` | PASS | Existing generated test vectors |
| Exact App #25 NEUI signature, TBS, SPKI, ABI and version | `tests/app-platform-offline/**` | PASS for test issuer; `CROSS-ROOT UNKNOWN` | No controlled production root match |
| Sidecar staged/ACTIVE/revoke, recovery and lifecycle faults | `tests/app_sidecar_lifecycle/**`; `app_sidecar_lifecycle.c` | PASS | Memory backend; external purge is fake |
| State persistence faults and recovery | `tests/app_state/**`; `app_state_store.c` | PASS | Simulated persistence backend |
| Existing report-service lifecycle and queue behavior | `tests/app_report/**`; `app_report_service.c` | PASS for merged #44/#75 code | Does not implement #88 policy or #101 report purge |
| Storage UNKNOWN/untrusted behavior and admin authentication | `tests/storage_gate/**` | PASS | Fake flash/NVS and local Web/API shims; not physical data protection |

### AC results

| AC | Result | Evidence boundary |
|---|---|---|
| AC1 — artifact/source provenance | **PASS for software provenance; `CROSS-ROOT UNKNOWN` for native trust mapping** | Exact Host/App branch tips, merged candidate SHAs, exact signed NEUI length/SHA/TBS/SPKI/ABI/version and current source-configured native anchor are listed above. No device-installed trust root was read. |
| AC2 — unified offline integration entry | **PASS for host C/API test execution** | One command runs real merged installer, admission, lifecycle, session/control/state/report and auth/storage source suites with simulated hardware seams. Real report purge stays **BLOCKED/NOT_AVAILABLE** because #88 is REQUEST_CHANGES and #101 is not done; the runner prints that status and makes no uninstall DONE claim. |
| AC3 — browser evidence consumption | **PASS for signed asset and independent review of historical #99 Chrome evidence; current browser rerun `NOT_RUN`** | The local Node audit verifies the signed asset. The merged #99 study supplies the recorded browser results below. No live Host credentials/API, device DNS, production runtime adapter, or camera was used. |
| AC4 — evidence index and downstream mapping | **PASS for this offline evidence package** | Reproduction, test matrix, exact source index, downstream reuse map, blocked prerequisites and future device evidence are recorded. This does not close or alter #40/#48/#58. |

## Historical #99 Chrome results

The exact Chrome run is recorded in [the merged Host #99 study](../../../Docs/design/sidecar-origin-signed-browser-study.md). It used Google Chrome `155.0.8059.80`, the signed App #25 asset SHA `3cd01519…`, synthetic local origins and generated H.264 data. It is historical lab evidence; the runner did not repeat it.

| Case | Recorded Chrome result | Meaning |
|---|---|---|
| Opaque sandbox `allow-scripts` | Both signed Workers blocked; parent DOM, storage and cookie access denied | **FAIL** for this signed bundle's complete Worker path |
| Same Host origin with scripts and `allow-same-origin` | Child read Host fixture token/DOM/API, removed sandbox and reloaded top page | **FAIL** isolation boundary |
| Same hostname, different port | DOM/localStorage separated; Host cookies crossed port | **FAIL** as cookie isolation boundary |
| Sibling hostname in same site | Parent DOM/storage separated; parent-domain cookie crossed | **FAIL** when parent-domain cookies are present |
| Independent App site origin | Two signed Workers, JMuxer Worker import, WebSocket fixture and MSE playback at 320×180; unauthenticated fixture admin API returned 401 | **PASS in lab fixture only**; does not establish device DNS/routing/TLS, Host resource service, runtime adapter, camera or live video |
| Signed FileSaver `saveAs`, with and without `allow-downloads` | **FAIL** both times with `ReferenceError: get_URL is not defined`; no download event | Permission decision was not reached by the failing signed API |
| App Blob URL anchor fallback without `allow-downloads` | **BLOCKED**; no Chrome download event within 2,500 ms | This exact sandbox permission denied the fallback download |
| App Blob URL anchor fallback with `allow-downloads` | **ALLOWED**; 40-byte fixture downloaded with SHA-256 `144fdfbeb102a56d59ea05f63e5bef58040c5a658a703d3dfaaa0b104795f54c` | Actual Chrome permission result for the fallback only |

The independent-site video used a local WebSocket fixture and generated H.264 access units, not a camera. The signed App produced no typed Host handshake ACK. A lab App-site PASS is not a device DNS, trusted Host runtime adapter, or live-video PASS. The source-reviewed harness is not permission to read admin credentials or call a deployed admin API.

## Reuse map for #40, #48 and #58

Each original equipment acceptance contract remains unchanged and open. `PASS` below means only the software behavior already verified in this offline record is reusable.

| Issue / AC | Reusable offline evidence | Required field evidence before that AC can pass |
|---|---|---|
| #40 AC1 | Installer/admission/Web API transition and failure-path tests; existing v1/v2 package fixtures | Actual signed `hello-app` v1/v2 bytes; authorized Host identity; Web install → execute → upgrade → execute → uninstall results and device logs |
| #40 AC2 | Admission and installer rejection tests cover auth/privilege, signature, identity/version/ABI/size/conflict and UNKNOWN storage paths | Same cases against the specific authorized device; Web state and device-reported committed state agree; no data erase or false executable status |
| #40 AC3 | Storage gate tests exercise refusal and recovery logic with fake flash/NVS | Device-specific written authorization, verified recoverable backup/rollback, before/after firmware/Web/partition identity and protected-data hashes, authorized power-fault recovery if separately approved |
| #40 AC4 | This exact-SHA/source/artifact index and reproducible command | Exact field package/configuration, permission record, target logs and remaining production/user-acceptance limitations |
| #48 AC1 | Installer, run, lifecycle and auth/API source tests; App #25 asset provenance | A signed package accepted by the same controlled Host root; real install/update/uninstall; Host/Web/model and non-App file identities before/after, with no implied reflash |
| #48 AC2 | Session and state software tests provide lifecycle/failure paths only | Real camera and managed AI; continuous tracks/events; IN/OUT direction, model/target changes, gaps/resource limits and restart behavior |
| #48 AC3 | Existing #44/#75 report-service tests and test transport paths | Actual device MQTT and HTTPS Webhook endpoints; accepted payloads, delivery acknowledgements, single-channel failures and queue-vs-delivery states |
| #48 AC4 | State-store/storage UNKNOWN and fault-injection suites | Device data ownership across stop/restart/replacement, real storage/network fault recovery, protected records and verified backups |
| #48 AC5 | Host control/session API suites cover software request and state-machine behavior | Continuous real AI session; Web line/direction/threshold/target/reset changes; durable revision and event boundary; conflict, disconnect, lost ack and real restart observations |
| #58 AC1 | This index, v1/v2/v3 admission tests, auth rejection and the Host/App source chain | One authorized hardware identity, fixed firmware/model and actual packages; same-root trust, v2→v3 persistence inheritance and #48 hot-change records |
| #58 AC2 | Offline fault/recovery cases can inform the approved load plan | A-approved duration/load plan, long-run camera/AI counts, gaps, resource maxima, management operations, restart and recovery on device |
| #58 AC3 | Host source and OTA contract references only | Authorized recoverable backup; actual Host update/downgrade path with installed App/state/Web compatibility; separate approval for power or partition tests |
| #58 AC4 | This evidence index and runner output are reusable documentation structure | Actual device/image/package hashes, network/test environment, protection and backup checks, demo log, user acceptance and separate release decision |

### Field checklist

- Identify the exact test device, firmware and embedded Web build; record their hashes and the native anchor actually present on that device.
- Obtain separate approval for each physical write, install, uninstall, reset, power-fault and external-network action. Verify a recoverable backup of relevant Flash/NVS/LittleFS and user data before any approved write.
- Obtain a controlled signing identity that the target Host actually trusts. Do not use the public test issuer or a reproducible test key as proof of device authorization.
- Record real Web authentication/privilege denials and install/update/recovery results on the target, including before/after protected-region and non-App file hashes.
- Capture continuous camera/AI event and counter data, gaps, resource limits, real MQTT/Webhook responses, restart state and hot-configuration acknowledgements for #48/#58.
- Resolve Host #84/App #32 resource serving and runtime adapter, then record target DNS/site reachability and live video authorization. The current lab origin is not a deployment address.
- Keep every unrun or unauthorized item `NOT_RUN`/`BLOCKED`; do not promote any software test to hardware acceptance, product acceptance or release.

## Blocked dependencies and successor conditions

| Work item | Current GitHub state | Condition before its evidence can be used as a pass |
|---|---|---|
| Host #57 old Web-in-firmware route | Canonical claim BLOCKED; Issue CLOSED as `not_planned`; old PR #78 closed unmerged | It is not a current dependency and cannot unlock #48. #48's current contract uses the Sidecar route. |
| Host #88 → Host #101 | #88 OPEN; PR #98 candidate `51aa3ac45a52154bcb9f7865e04dccfcae2138b5` is `REQUEST_CHANGES`; #101 OPEN / Prepared / NO READY | #88 needs exact-SHA correction, review, merge and canonical DONE. Then #101 must independently connect and verify real policy/report purge. #83's fake purge callback is state-machine evidence only. The #88 review found a remaining report-admission path after committed-identity failure/UNKNOWN; it is not a PASS. |
| Host #84 ↔ App #32 | Both OPEN with ACTIVE claims and no merged acceptance at this evidence cut | Their signed resource-serving and App-owned typed runtime adapter work is in progress. Until exact-SHA review, merge and canonical DONE, it is not implemented/accepted evidence for this package. Target-reachable origin, device DNS and live media remain later field checks; #99 lab evidence does not confer production PASS. |
| Host #91 ↔ App #27 | Host #91 OPEN / Prepared with no frozen executable contract; App #27 OPEN with no active claim | Freeze and independently implement the versioned live tracks/events producer and Host admission; do not substitute report-window snapshots for live tracks. |
| App #33 → App #30 | App #33 OPEN; PR #34 candidate `8df71defb1cb9e6221a2189d2fcf1ca3f33f3eb4` is open and not canonical DONE. App #30 OPEN / Prepared / no active claim | Complete exact-SHA review/merge/canonical DONE for the persistence and identity investigation, then A must freeze and authorize the App #30 control contract before implementation or device claims. |

Host #40, #48 and #58 remain open. This record does not edit their contracts, grant device access, sign packages, implement #88/#101/#84/App #32/live ABI/App Enable, or claim real-device PASS.
