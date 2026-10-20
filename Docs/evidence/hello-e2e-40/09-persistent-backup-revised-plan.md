# 09 — Persistent backup migration and revised S2/S3 plan (post A ruling 2026-10-20)

## Backup persistence (A directive: move off volatile storage, independent re-verification)

- Migrated the full 2026-10-19 partition backup set, stage-1 NVS, forensic dump, ATE parser and
  companion scripts/logs (34 artifacts) from the volatile /tmp location to an operator-local
  persistent directory (access-restricted, mode 0700). Off-machine replication destination is not
  yet designated; recorded as pending.
- Verification: original manifest re-checked on the copy — 11/11 OK, zero drift; independent
  re-hash of all binary artifacts in the persistent copy — 14/14 OK.
- Historical recovery point (10-19 set + stage-1 NVS) is now durable. This is distinct from and
  must not be conflated with the CURRENT DEVICE STATE, which is NOT CAPTURED: at probe time the
  device was unreachable (read-only ping/HTTP only), so the active APP slot, OTA state, current
  NVS content and signing identity remain unknown until read-only access is restored. No write
  may proceed on the assumption that the backup set reflects the device's current state.
- Forensic gap recorded: the erase-time intermediate NVS snapshot was lost to /tmp cleanup
  (historical SHA retained in the ledger). Pre-/post-incident states are therefore not fully
  reconstructible; treated as a gap, not minimized.
- Key-count note: historical forensic passes reported 283 vs 284 distinct NVS keys. The difference
  does not involve credential keys and does not affect recovery interpretation; no further
  forensic expansion performed.

## Revised S2 (flash) — minimal write set, no pre-approved retries

- Pre-write (read-only, no flash): determine the active APP slot, OTA state and startup policy on
  the device before any write, to establish whether a single-slot write suffices while preserving
  a usable fallback slot. Two-slot overwrite is not the default; a single-slot write is not forced
  either if it fails the safety conditions established by that read-only determination.
- verify/double-read failure at any point: STOP immediately, seal the error and current state.
  No pre-approved transient retry count and no automatic rollback rewrites: retries and recovery
  writes are themselves new risk writes requiring their own explicit authorization.

## Revised S3 (NVS) — no-NVS-action preferred

- Neither prior option is treated as low-risk default. Stage-1 write-back is itself a write that
  may roll back configuration and reintroduce a legacy authentication state; erase-to-BLANK
  forfeits the current (uncaptured) state. The preferred path is: restore readable on-device
  state and a fresh verified preservation first, then propose a conservative, separately
  authorizable NVS action based on observed device/boot behavior and actual data loss.
- If a #121 BLANK first-boot experiment is still warranted, it must run on isolated blank/dedicated
  test media or a separately controlled unit, with its own loss/backup/recovery list — not on this
  board's original state.

## Default-build gate for any future target firmware

- The flagged build (APP_HOST_POC=1 BOARD_PSRAM_SIZE=64) is recorded as B-reported flagged BUILD
  PASS / artifact survival only. It is NOT a default-build PASS and is not the official #40 target
  firmware. User requires default make/make app/make pkg/make pkg-app to include App Host without
  special variables; that rectification is owned by issue #110 (Prepared, authorization channel
  reserved to A) and must land before any #40 device acceptance build.
- The #121 fix content is already merged on the app-host-poc line; packaging compliance awaits
  #110, not a re-build with flags.

## Device reachability

- Read-only probe at evidence time: device unreachable. Recorded BLOCKED for all on-device
  confirmation steps (identity/slot/auth/NVS state). No write mode is entered to compensate for
  unreadable state.

## Relation to claim state

- r2 Candidate (73f2dbca) remains REQUEST_CHANGES; this delta adds no device actions and no code
  changes. It delivers the A-directed minimal no-device evidence: persistent verified backups,
  current-state-uncaptured distinction, revised S2/S3, and the default-build gate acknowledgment.
