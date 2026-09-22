# Task 13 Device Validation Runbook and Record

## Status

NOT STARTED (deployment blocked: no ST-Link probe attached to the workstation;
UART console /dev/tty.usbserial-14130 present but silent — device likely unpowered).

## Release candidate under validation

- Branch: counting
- Firmware commit: counting branch HEAD containing d34d3d440ba90a9ace8d5862272fbcffa4b6ca4a
  (parent 6e9c9005031530805a317e8c2225450d10e26440) plus the consolidated closure fix;
  exact SHA recorded in the round report section A before flashing
- Packages (make STEDGEAI_VARIANT=4.0 pkg):
  - build/ne301_App_signed_v4.3.1.326_pkg.bin (App1 0x70100000 / App2 OTA 0x70500000)
  - build/ne301_Web_v1.5.0.x_pkg.bin, build/ne301_Model_v4.0.0.x_pkg.bin, build/ne301_FSBL_signed_v1.0.3.1_pkg.bin

## Deployment

1. Switch 2 ON (flash boot mode), ST-Link on DEBUG port.
2. make flash  (FSBL + App + Web + Model + OTA-state clear)
3. Switch 2 OFF (normal boot), power-cycle.

## Record (fill at physical start)

- Start timestamp (UTC):
- RTC state at first boot:
- Device serial / MAC:
- Model configuration: default AI model partition contents + active generation
- MQTT broker / topic:
- Webhook endpoint:
- AI model generation changes during soak:

## Case matrix

| Case | Procedure | Pass criteria | Evidence | Result |
|------|-----------|---------------|----------|--------|
| C1 first boot | power-on after flash | boot log reaches LC init, subscriber registered | UART log | |
| C2 power-cut x20 | cut power at random 2-60s intervals | totals monotonic vs pre-cut durable record; no rollback of committed reset | UART log + REST stats before/after | |
| C3 manual reset | POST /api/v1/apps/line-counting/reset | totals zero durably; reboot keeps zero; epoch advanced | REST + reboot | |
| C4 target change | POST config with new target class | totals reset durably; candidate active after reboot; txn record resolved | REST + reboot | |
| C5 target change power-cut in window | cut during PREPARE..commit window | boot roll-forward completes candidate; deterministic | UART log | |
| C6 queue overflow | disable MQTT+Webhook, generate > capacity reports | dropped counters durable across reboot and compaction | REST delivery stats | |
| C7 queue recovery | re-enable transports | backlog delivered exactly once | MQTT/webhook capture | |
| C8 model reload | load different model via model management | generation advances; LC rebinding or disabled state correct | REST status | |
| C9 counting continuity | 24h soak, scripted crossings | window reports vs ground truth; no watchdog/assert/crash | UART log + reports | |
| C10 storage fault (practical) | optional: LittleFS fault via full partition | storage_faults counted; queue/totals remain consistent | CLI sys/ai logs | |

## Observations log

(append chronological entries: timestamp, case, observation)

## Execution record — 2026-09-22 (attempt 1)

- RC HEAD: 69c016332313e6fd9624c8d5a7b62af684cfb22e (App pkg v4.3.1.328, rebuilt at HEAD so embedded version metadata matches)
- ST-LINK: SN 56FF6F064984524928381287, FW V2J46S7 (connected, probe listed)
- Flash result: per-component, mode=UR (under-reset; HOTPLUG would wedge after FSBL per learnings):
  - FSBL  ne301_FSBL_signed.bin            -> 0x70000000  Download verified successfully
  - App   ne301_App_signed_v4.3.1.328_pkg  -> 0x70100000  Download verified successfully
  - Web   ne301_Web_v1.5.0.7_pkg           -> 0x71900000  Download verified successfully
  - Model ne301_Model_v4.0.0.0_pkg         -> 0x70900000  Download verified successfully
  - WiFi  ne301_Wifi_flash.bin             -> 0x71A00000  Download verified successfully
  - OTA state sector [9 9] erased (0x70090000..0x70091FFF)
- Boot verification (C1): BLOCKED — UART console silent on both exposed ports
  (/dev/cu.usbserial-14130 and /dev/cu.SLAB_USBtoUART, CP2102N, 115200 raw) across
  HOTPLUG software reset, UR hardware reset, and Enter-nudge probes. Zero bytes captured
  (.tests/task13-evidence/console-*.log). Flash path proven working (verified downloads),
  so the N6 is powered and SWD/NRST are wired; the failure is between reset-release and
  console output.
- Candidate causes (physical, need hands on board):
  1. SW2 boot switch not in boot-from-flash position after programming (most likely;
     learnings note the same flow previously required the switch handling).
  2. Debug console wired to a different USB-serial adapter/connector than the two exposed.
  3. Camera module unpowered (ST-Link powers the MCU rail but camera console/PSU path open).
- Unblock procedure: set SW2 to boot-from-flash, power-cycle the camera (full USB power,
  not just ST-Link), confirm which adapter carries the console (screen /dev/cu.SLAB_USBtoUART
  or /dev/cu.usbserial-14130 @115200), then re-run C1. Soak timer starts at first confirmed boot log.
- Soak: NOT STARTED (0h elapsed). All runbook cases pending C1.

## BLOCKER found during C1 (2026-09-22): config store establishment fails on blank media → auth never initializes

Symptom: after full-chip erase + full reflash (RC 69c01633 / App v4.3.1.328), every login fails
INVALID_PASSWORD (any password incl. default hicamthink); all web info missing (-36 cascade);
console shows zero auth init lines.

Failure path (evidence-backed):
1. Full erase → NVS/LittleFS blank.
2. main.c step 4 `core_system_init` → Stage 2 `core_init_config_stage` → `json_config_mgr_init`
   → blob establishment fails → "Failed to establish config store, will retry next boot"
   → "Configuration Manager initialization failed: -10" (~490 ms, twice, consecutive boots).
3. core_init.c:47-56 early-return on config failure → Stage 6 `core_init_security_stage`
   (auth_mgr_init) NEVER RUNS → g_auth_mgr zeroed BSS → admin_password_hash = all zeros.
4. auth_mgr_verify_password compares any hash against zeros → always false → all logins 401.
5. Config getters return -36 (NOT_INITIALIZED) → web info empty; device falls into AP+DHCPS
   192.168.10.10 mode; RTC unsynced (1970-01-01).

Branch attribution (vs origin/main):
- core_init.c early-return structure: IDENTICAL on main (0 diff) — but UNREACHABLE on main,
  because main's json_config_mgr_init self-bootstraps from blank media (load-fail → defaults →
  NVS save → OK).
- Reachability created on counting: rounds 5-13 added the fallible blob-store establishment
  gate to json_config_mgr_init; on blank media (and per two boots, persistently) it fails.
- auth_mgr.c/api_auth_module.c: zero diff on counting. Password 123123 found in old NVS dump
  was a red herring; with auth uninitialized ANY password fails.

Two consecutive boots show identical failure → "retry next boot" does not self-heal;
establishment failing step (LittleFS mount timing vs core stage 2 / blob IO) needs pinpointing
before fix. Device remains in AP fallback mode.

Reproduction: 100% — full erase (-e all) → reflash → boot ×2 → same log, login impossible.

Task 13 impact: C1 cannot PASS (no working config/auth); all cases + soak BLOCKED on this
software blocker. No soak started. Fix decision belongs to A (touches FROZEN config domain):
candidate minimal directions — (a) security stage independent of config stage success
(auth_mgr_init fallback was designed for this but is short-circuited by stage order);
(b) make establishment wait for/storage mount ordering fixed; (c) retry establishment after
storage becomes ready instead of failing init.
