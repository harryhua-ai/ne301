# Host NFC4 Enable Linearization

## Wire contract

NFC4 enable commands use type 5 and an exact 48-byte envelope. The 32-byte Host control header is followed by a 16-byte `ENB1` payload containing the desired Enabled or Paused state, expected state revision, and zero reserved fields. The receipt is also exactly 48 bytes: the NFC4 receipt header plus a 16-byte `ENR1` body. The body repeats the effective state and state revision; both revision fields must agree. A 32-byte NFC3 receipt cannot complete an enable command.

The request ID is nonzero, strictly increasing within the control session, and consumed before the precommit result is returned. An enable request requires protocol v4, capability bit 8, capability bits 4, 5, and 6, a 48-byte control quota, and both Host proof callbacks. Capability bit 9 remains an independent live-publish capability. NFC3 CONFIG_APPLY, COUNTERS_RESET, and 32-byte receipts keep their prior behavior.

## Production authorization and ordering

The production session opens the control channel only for the committed, verified v4 application. The session binds it to the install record, publisher and app IDs, SPKI and TBS digests, session generation, boot epoch, config generation, and state-store identity. The existing administrator gate and expiry/revocation checks remain in the session lifecycle.

Before a Paused command enters the pending control slot, the production precommit callback checks the current state revision, raises the native-major floor to at least 4, and verifies the persisted floor by reading it back. The verified record includes the logical owner, package, SPKI, TBS, session, boot, request, config generation, and expected state revision. Only after that succeeds can `control_next` deliver the command.

The v4 state read and commit entries use the same Host-backed state store as the production control callbacks. A receipt can become APPLIED only after an exact state commit, a state readback with the returned revision and matching SHA-256, the same request/session/config context, and floor verification for Paused. NFC4's duplicated state revision and desired state must agree. A failed proof records UNKNOWN and cannot populate the APPLIED receipt cache. Session close records an in-flight enable request as UNKNOWN.

## Storage failure and recovery boundary

A floor write or readback failure returns a storage guard and does not deliver the command. The code does not report a floor marker when the first write failed. If the marker is absent after restart, the installer checks the committed package for a same-owner downgrade: it rereads the package, verifies its package digest and signature, then checks the committed identity, version, SPKI, and TBS. A verified v4 package declaring enable control blocks a v3 downgrade. If the record or package cannot be read or trusted, installation returns storage state unknown and remains closed until trusted recovery.

The low-level floor check alone returns OK for an absent marker; the conservative recovery decision is made by the production install path using the verified committed package. This fallback does not claim that a persistent marker exists when storage rejected the write. If both the marker and all trusted recovery evidence remain unavailable across restarts, durable fencing cannot be established from those unavailable sources; that condition remains fail-closed as storage unknown.

The installer's verified package classification cache is scoped to the current boot and is cleared during boot resolution and uninstall. A new boot therefore rechecks the committed package before allowing a same-owner downgrade when the floor marker is absent.

Floor retirement occurs only through the confirmed uninstall path after external report purge confirmation, local cleanup, exact uninstall-intent and install-record owner/package/TBS/SPKI checks, and floor retirement readback/deletion. A stale cleanup record cannot retire a replacement owner's active floor. Resume and ordinary command failures do not clear an active floor.

## Compatibility and verification

The Host enable suite uses the App #30 source at `85e3d4a46b8b4c53a63fac3405b91efb084a3f8a` (`d0e525e38f64735362cb71cc0ef014d421fddcc7` tree) from a temporary archive. It runs App's NFC4 and joint line-crossing suites against Host control, state-store, installer, and floor code, including CAS competition and failed-floor-write non-delivery.

The Host session suite exercises production v4 state and enable callbacks and retains the live-only bit-9 path. The native-floor suite covers CRC/epoch validation, exact request proof, and same-owner v3 rollback after a failed first floor write. The nine existing Host suites and live-ingest suite are run separately for regression coverage.

Device execution, production keys, external network delivery, and hardware validation are outside this host test fixture and remain NOT_RUN.
