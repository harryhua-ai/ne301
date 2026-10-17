# Sidecar signed UI browser origin study

**Issue:** [NE301 #99](https://github.com/harryhua-ai/ne301/issues/99)

**Disposition:** browser feasibility is demonstrated only for a lab independent origin; target deployment is **UNKNOWN** and #84/#32 remain **BLOCKED / NO READY**.

**Study baseline:** NE301 `fdefbc67abf13dbdbebbd71a89967d20e307486f`; signed App input from ne30x-app merge `27b768bdefb3283652e4ab3318cfe9f9dbf693e8` (PR #31).
**Boundary:** offline/read-only feasibility study. No production loader, broker, App runtime adapter, device access, DNS/TLS changes, or production URL/wire ABI decision.

## Decision in one page

The signed counting page and its H.264 assets run in Chrome when served from an origin separate from the Host page. The opaque-origin script sandbox blocks both native Worker scripts, while Host-origin `allow-scripts allow-same-origin` allows the child to read Host DOM and localStorage, remove the sandbox attribute, and reload the Host. A different TCP port isolates DOM and localStorage but still sends Host cookies to that port. A sibling hostname isolates DOM/localStorage but receives any cookie scoped to the parent domain.

**Recommendation for A:** carry forward a dedicated App origin that is a different site from the Host, with the App HTML, relative chunks, Worker scripts, and JMuxer served from that App origin. The video endpoint must also have independently verified session authorization and reachability; this study does not require it to share the static resource URL. The Host may use `allow-scripts allow-same-origin` only while the App origin is cross-origin to the Host parent; Host remains the owner of the iframe and its revocation. Use a narrow, Host-issued MessagePort capability grant. This is a candidate security boundary, not a production hostname, URL, DNS/TLS, or wire ABI decision.

The actual device origin is not established. Current Host source contains an HTTP Host server and a separate WebSocket stream service, but no browser Sidecar resource service is present in the inspected tree. #84 and App #32 remain Prepared / NO READY because the device network path and production typed adapter are unproven. Keep App unavailable until those gates are resolved; do not fall back to loading the signed UI on the Host origin.

## AC1 — source and asset inventory

All Host source statements below refer to the NE301 study baseline above. A source default is not evidence of the currently deployed firmware image.

| Area | Repository evidence | Finding and boundary |
|---|---|---|
| Host HTTP/TLS | `Custom/Services/Web/web_config.h:8`; `Custom/Services/Web/web_server.h:489-502`; `Custom/Services/Web/web_server.c:79-92` | Source defaults to `IS_HTTPS=0`, HTTP port 80, and binds `0.0.0.0`. Build overrides and the target image were not inspected: deployment protocol/port are **UNKNOWN**. |
| WebSocket stream | `Custom/Services/Web/websocket_stream_server.c:165-177, 504-511`; `Custom/Hal/Network/netif_manager/sl_net_netif.c:2316-2323` | Source config uses port 8081 and `/stream`; AP mDNS registers one `ne301` name with `_http` 80 and `_ws` 8081 when that AP interface is brought up. No second App-specific hostname is evidenced. This does not prove target DNS, routing, interface, or TLS. |
| Static Host assets | `Custom/Services/Web/web_service.c:49, 111-123`; `Custom/Services/Web/web_server.c:921-1029`; `Custom/Services/Web/web_assets.c:181-200` | Existing Web assets are loaded from the embedded `WEB_BASE` image. Static responses use CRC32 ETag and `Cache-Control: max-age=86400, public`, with `Access-Control-Allow-Origin: *`. `web_asset_find()` uses prefix comparison and unknown paths fall back to `index.html`; it is not an exact-match Sidecar resource router or a signed per-response manifest verifier. |
| Sidecar lifecycle/API | `Custom/Services/Web/api/api_app_install_module.c:32-60, 491-555, 690-706`; `Custom/Services/AppHost/app_sidecar_lifecycle.c:403-433, 730-789` | The authenticated admin descriptor reports identity, hashes, service generation, logical file paths, lengths, SHA-256 values, and private `storage_path`. Lifecycle storage uses `/apps/ui-b_<bundle_sha>.neui` and `/apps/ui-a_<identity>_<index>.bin`. These are storage/API data, not a browser URL or an implemented static resource service. No browser route from that storage path was found in the inspected Web tree. |
| Admin auth/session | `Custom/Services/Web/web_server.c:595-601, 1048-1081`; `Custom/Services/Web/api/api_auth_module.c:23-59`; `Web/src/services/request.ts:103-117` | Request context starts with `session_id=NULL`; server auth consumes `Authorization`; Web client reads `localStorage['token']` and attaches `Authorization`. No Cookie/Set-Cookie handling was found in the inspected Web API/server paths. This is a source search, not a statement about every deployed build. |
| CORS/API | `Custom/Services/Web/web_server.c:611-620, 886-896`; `Custom/Services/Web/api/api_app_install_module.c:32-60` | CORS permits `*` and the `Authorization` header, with no `Access-Control-Allow-Credentials` in these responses. Sidecar install/state routes require authentication; the installed descriptor additionally checks high privilege. CORS is not authorization: an independent App origin without the Host token receives an auth denial. |
| Target network | repository defaults above; no device/browser session in this task | Whether a second hostname/site can resolve and reach a resource server from the real device network, and whether the deployed Host image matches this source, are **UNKNOWN**. Lab resolver rules and the old #82 `127.0.0.1`/`localhost` fixture are not device DNS/TLS evidence. |

### Fixed signed App #25 input

The exact seven-file NEUI from App PR #31 was copied byte-for-byte into `tests/sidecar-origin-study/input/line-crossing.neui`. The harness validates the NEUI v1 header and lengths, publisher/app/version/ABI, bundle SHA, UI TBS SHA, copied public SPKI SHA, ECDSA-P256/SHA-256 signature, path allowlist, and each payload SHA/size before extraction. The copied public key is the PR #31 **test/development** publisher key; this is not a production trust-root or device verification claim.

- Bundle: 422,963 bytes; SHA-256 `3cd01519af881eb114c2831d67b4f0542815c98140e06deeba14e597d947771c`
- Identity: `test-publisher / line-crossing / 1.0.0`, `host_ui_abi=1`
- Publisher SPKI SHA-256: `5cd252fb0ce8932436faf8ccd1040981b89ee4ad6b9fe9e2a2b7e71aacb27cd3`
- Native TBS SHA-256: `d793a455e3ccda715c8ee9ae95a718cf0fc2627c0ff20969f23e7ea9349b349e`
- UI TBS SHA-256: `59f695608f3caa2a06ffae486e6288b3e3052c277a87c5a8a0a3cfb3a049e7a6`

| Signed file | Bytes | SHA-256 |
|---|---:|---|
| `index.html` | 399 | `344dc49609dbd519411e098bf9d53ab9f9a08232ba6169e3f069f48d5f091532` |
| `assets/index-CfTnTFvt.js` | 269,841 | `13b3071920115f597295e6f1002dae43b9d43797b3bafb4aedc7c619ead3737e` |
| `assets/index-D-isHcyK.css` | 41,608 | `caa915f0cbaf7e9b499bebc8b7232cc85b96c359c8a3230040dee379794021f1` |
| `assets/FileSaver.min-D428eXCv.js` | 6,939 | `37ca34e7ab99ebf9a005aaaee56a1004b196411f9410a0cd25c0ebf9024b7aec` |
| `assets/jmuxer.min.js` | 100,839 | `87b8dac054a9d2e06dacb8eccb1b3580c39af8634180bc36012c1b7d958c7442` |
| `assets/videoWorker-CwSEKqUk.js` | 682 | `0f07f17eac982a2ff1eeff58b50c41a69d5237363397cd2ce5415a7c9ea0f439` |
| `assets/websocket-init-yiJ-u6et.js` | 2,000 | `9987a6188be31e521fa577338e11d85b2bfaa0ce4b67b263bdbcde3931df6800` |

The immutable App source is at ne30x-app merge `27b768bdefb3283652e4ab3318cfe9f9dbf693e8`: `apps/line-crossing/ui/src/main.tsx:8-14` mounts `LineCountingModule` without a production capability adapter; `LineCountingModule.tsx:81` defaults to `unavailableCapabilities`. `VideoPreview.tsx` only creates the live player when a video grant is available. `h264Player.ts:359-367` creates the module WebSocket Worker and classic decoder Worker; `videoWorker.ts:40-42` uses relative `importScripts()` for JMuxer; `media.ts` appends MP4 through MSE. Therefore the signed UI mounts and shows unavailable state in this study, but the UI itself does not start a live camera session. The direct Worker/MSE probe below exercises the same signed worker/muxer assets separately; it is not an App UI-to-Host end-to-end pass.

## AC2 — reproducible Chrome harness

The harness is `tests/sidecar-origin-study/run.mjs`; package dependency is pinned to `playwright-core@1.64.0`. It starts two local HTTP servers, a local WebSocket stream, synthetic H.264 from FFmpeg `testsrc2`, and launches the installed Google Chrome with `--host-resolver-rules` mapping test-only hostnames to `127.0.0.1`. It does not edit system DNS/hosts or contact a device. The secondary server uses another ephemeral TCP port solely to observe port/cookie behavior.

Observed tool versions: Google Chrome `155.0.8059.80`; Node `v22.22.3`; npm `10.9.8`; FFmpeg `8.0`. Reproduce from this directory:

```sh
npm ci
npm run study
```

The measured run used `npm run study > /tmp/ne301-issue99-study-run.log 2>&1`. It exited 0. Chrome log summary:

```text
Chrome 155.0.8059.80; NEUI SHA256 3cd01519af881eb114c2831d67b4f0542815c98140e06deeba14e597d947771c
opaque sandbox: moduleMounted=true, parent/storage/cookie=SecurityError, both Worker constructors=SecurityError (origin null)
same Host origin + allow-scripts allow-same-origin: host token/DOM/API readable, sandboxRemoved=true, topReloadObserved=true
same host, different TCP port: parent DOM/localStorage separated; host_only_session cookie sent to the second port
independent site origin: parent DOM/storage/cookie unavailable; unauthenticated Host admin API=401
signed App main JS/CSS and FileSaver asset responses=200; independent-origin media probe: websocketOpened=true, 2 Workers, videoWorker importScripts(JMuxer), MSE 320x180, playback currentTime > 0.1s
MessagePort fixture: ACTIVE READY/ACK; inactive, expired/ungranted, oversized, stale generation, duplicate sequence, unauthorized capability and forged source/origin denied; close/navigation drops old channel
cache probe: force-cache replayed build-old after active build changed to build-current; generation comparison rejected the old build
signed App adapter handshake: no ACK; target deployment: UNKNOWN; decision: BLOCKED
```

| Browser case | Direct observation | Meaning for the existing contracts |
|---|---|---|
| Opaque origin, `sandbox="allow-scripts"` | Signed page mounted and reported App/video `unavailable`. Parent DOM, localStorage, and `document.cookie` access raised `SecurityError`. Both exact Worker URLs failed to construct with `SecurityError`; browser message says the scripts cannot be accessed from origin `null`. | Fails complete Worker/H.264 resource path for this signed package. Do not remove isolation to rescue it. The API request without Authorization returned 401. |
| Same Host origin, `allow-scripts allow-same-origin` | Child read Host DOM/localStorage and the fixture admin API returned 200. Child removed the iframe `sandbox` attribute, then top page reload ran without it. | Confirmed escape case. Reject as Host/App isolation. |
| Same hostname, second TCP port | Parent DOM/localStorage were separated and Host API request without Authorization returned 401, but the HTTP request to the App port included host-only and domain cookies set on the other port. | Reject port-only as a cookie boundary. |
| Sibling hostname in same test site | Parent DOM/localStorage were separated. The cookie with `Domain=example.test` was sent/read by the sibling hostname; Host API without Authorization returned 401. | A sibling name alone is not enough if Host or another sibling uses a parent-domain cookie. Current inspected Host API code uses Authorization, not a cookie session. |
| Independent hostname and site | Parent DOM/storage/cookie were unavailable; Host API without Authorization returned 401. Relative App resources loaded from the App origin. Both signed Workers ran, classic Worker loaded the signed JMuxer through `importScripts`, WebSocket frames reached the decoder, and MSE displayed 320×180 playback. | Browser/resource/media path is feasible for a cross-site App origin under the fixture's explicitly provided serving policy. This says nothing about target DNS, TLS, routing, firmware, camera, or production authorization. |

The H.264 source was generated by FFmpeg, not a camera: 320×180 at 25 fps, 50 access units, 157,673 bytes, SHA-256 `314c40d497a36a8f072c8fbdb55c20fe68a4096be937a3629d46a70e9fa8abb0`. The local fixture supplied a `/stream` WebSocket and 64-byte frame envelope for the signed decoder Worker. No live stream or target WebSocket authorization was exercised.

FileSaver's exact signed JS was requested with HTTP 200. `window.saveAs` was not present in the mounted UI; the App source's `h264Player.ts` has an anchor-download fallback. The actual download click and sandbox download permission were **NOT_RUN**. This does not affect the measured playback path, but prevents a claim that user file export was browser-verified.

The harness MessagePort schema test is a local fixture, not the production App adapter. It verified a bounded (`4096` byte fixture limit), one-port handshake carrying ACTIVE state, identity hashes, build/boot/session IDs, a UI generation, a permission, and a matching unexpired grant; it then exercised READY/ACK, wrong generation, repeated sequence, unknown capability, oversized message, inactive/expired handshake, sibling/opaque/evil-origin message, close, and navigation. The fixture limit and schema are evidence only; neither is a frozen wire ABI. The real signed App gave no handshake ACK because its production main entry has no runtime adapter.

The old `tests/counting_sidecar`/#82 `127.0.0.1` versus `localhost` synthetic fixture was not used as target-network evidence.

## AC3 — decision matrix for Host #84 and App #32

| Candidate boundary | Worker/H.264 result | Host privilege/cookie result | Required network/service ability | Decision and recovery on falsification |
|---|---|---|---|---|
| Opaque sandbox on Host asset URL | Both signed Workers blocked in Chrome. | Host DOM/storage/cookie denied. | Current Host can serve the main static asset, but opaque-origin Worker scripts fail. | **Reject for this package.** Keep the App unavailable; do not relax the sandbox. A different worker-safe signed architecture would require a new App artifact and separate review. |
| Host page and App share origin with scripts + same-origin | Worker URLs can be same-origin, but isolation was disproved. | Child read Host token/DOM/API and removed sandbox, then reload escaped. | No new hostname is needed, which is precisely why it inherits the Host origin. | **Reject.** If a deployment only exposes this boundary, do not create the business iframe; show the existing Host Manage/Status state. |
| Same hostname, different port | Workers may be served same-origin to the App document on its port; a full video run on this port was not required for the cookie test. | Host-only cookies cross the port boundary in Chrome. DOM/storage remain origin-separated. | Separate listener/port would be needed; no such App listener is evidenced. | **Reject as the security boundary.** Port may route a service, but cannot be the cookie isolation claim. If this is the only available service, keep the App unavailable. |
| Sibling hostname under a shared site | Signed page loaded; this study did not repeat the full MSE probe on this row. | Parent DOM/storage isolated; `Domain=example.test` cookie crossed to sibling. | Requires a second hostname and some target DNS/service mapping; neither is evidenced as deployed. | **Conditional only** if A establishes host-only credentials and prevents parent-domain cookies. Otherwise reject and return to Host Manage/Status on missing proof. |
| Different App hostname and site from Host | **Chrome pass in the direct asset-level probe:** two signed Workers, relative Worker/importScripts/JMuxer, WebSocket, MSE and playback. | Parent DOM/storage/cookie unavailable; no Authorization token; fixture admin API returned 401. | Need the real device network to resolve/reach the separate App origin and serve the entire signed resource closure plus an independently authorized stream endpoint. None is currently evidenced. | **Recommended candidate boundary for A.** If target resolution, signed resource serving, or stream authorization cannot be shown, remain BLOCKED and expose no App session. Do not invent a production URL or TLS choice here. |

### Typed handshake invariants for joint #84 / #32 review

These are security invariants to agree across the existing contracts, not a proposed production message schema:

1. Host #84 may issue a grant only from #83's current committed **ACTIVE** descriptor and the live Host session. The App must not establish trust by echoing a URL, `app_id`, `origin=null`, or nonce by itself.
2. Bind the one-time handshake to the exact parent `WindowProxy`, the expected parent origin where the origin is non-opaque, a dedicated transferred `MessagePort`, and the committed identity tuple: publisher, App, publisher key hash, native TBS, UI TBS, build, boot, Host session, and UI generation.
3. Host-authored permissions are an explicit allowlist and grant expiry. Every request and response carries the grant/session/generation binding, a request ID, a monotonic sequence, bounded bytes/rate, and a deadline. Reject unknown capabilities, stale generations, duplicate/replayed sequences, malformed identity/grant, oversized input, and late replies.
4. Grant real business operations individually. `config.apply` must reconcile final `APPLIED`/`CONFLICT`/`UNKNOWN`/`UNAVAILABLE`; video open/close binds the stream to the same App/session/generation; reset requires the Host-owned administrator confirmation. App code never receives the Host admin token, arbitrary admin API, device-global API, or private storage path.
5. On navigation, iframe removal, App switch/update/uninstall, session/boot change, grant expiry, permission loss, or identity mismatch: close the channel and both Workers/WebSocket/MSE resources, reject all old-generation responses, and return to the Host's existing Manage/Status/unavailable view. Do not replay cached UI or bridge authority.

Fixture failures have explicit recovery: Worker or resource failure leaves the App unavailable; invalid/replayed/late messages receive no authority and require a fresh Host grant; stale cache is rejected against the current build/generation; navigation or revocation closes the old channel before another session can start. No failure path falls back to Host-origin loading or fabricates an APPLIED/live state.

### Ownership and remaining gates

- **Host #84:** own the Host parent, selection of current committed ACTIVE descriptor, app-origin resource service and exact path/hash checks, sandbox, per-session grant issuance/revocation, and a typed broker that never exposes admin credentials. Do not use the current `web_asset_find()` prefix/fallback or admin `storage_path` as the production loader contract.
- **App #32:** own the adapter inside the signed App source, consuming only the joint typed grant and mapping existing `LineCountingCapabilities` to those permissions. Preserve the signed App UI and its two Workers. An absent/invalid grant stays unavailable.
- **A / joint review:** select and prove a target-reachable origin/sandbox boundary, agree the typed handshake and recovery behavior in the existing #84/#32 contracts, and provide evidence from the actual target network before either card can become READY. Current #84 records a prior User choice not to require LAN MITM resistance in this phase; this study does not reopen or implement that choice, and does not establish deployed DNS/TLS.

| Gate | Current result |
|---|---|
| Browser behavior of tested boundaries | Opaque Worker path **FAIL**; same-origin sandbox **FAIL**; port-only cookie isolation **FAIL**; independent site origin + signed Worker/MSE assets **PASS in lab fixture**. |
| Target hostname/DNS/TLS/routing and deployed firmware identity | **UNKNOWN / NOT_RUN**. |
| Production App #32 adapter handshake | **NOT_RUN**; signed main gave no ACK. |
| Production Host #84 browser resource service and per-operation authorization | **NOT_RUN / absent in inspected source**. |
| Real camera stream, device, flash, session, or live user permissions | **NOT_RUN**. |
| #84 / #32 READY | **BLOCKED**; this study does not lower their acceptance gates. |

## Standards consulted

- [WHATWG HTML Standard — iframe sandbox](https://html.spec.whatwg.org/multipage/iframe-embed-object.html#attr-iframe-sandbox): same-origin content with both scripts and same-origin permission can remove the sandbox attribute and reload to escape; a dedicated domain is recommended for hostile content.
- [RFC 6265 §8.5 — Weak Confidentiality](https://www.rfc-editor.org/rfc/rfc6265#section-8.5): cookies do not isolate by TCP port; §8.6 also covers weak integrity for sibling domains.
- [MDN Worker() constructor](https://developer.mozilla.org/en-US/docs/Web/API/Worker/Worker): worker script URL must be same-origin with the caller document (or `blob:`/`data:`); the browser opaque-origin failure above is direct Chrome evidence for this bundle.
