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
