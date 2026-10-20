# Default Build Includes App Host

## Change

`Appli/Makefile` now sets `APP_HOST_POC ?= 1`. The existing guarded source list, compile define, include paths, and Web API registration are therefore included by the ordinary zero-argument App build; the linker layout remains unchanged. The root Makefile, packers, command targets, package naming, and runtime security code were not changed.

The baseline was collected before this change at `279fa0202b7c321ce83e917406f55c9ade0b84bc` and recorded in the prior `/tmp/codex110_report.md`; it was not rebuilt for this task.

## PSRAM Rule

The root Makefile defaults to `-DBOARD_PSRAM_SIZE=64`. The existing App Makefile check still rejects an App Host build without that exact define:

```text
make app COMMON_DEFS=-DBOARD_PSRAM_SIZE=32
Makefile:709: *** APP_HOST_POC=1 requires BOARD_PSRAM_SIZE=64.  Stop.
exit 2
```

The check fails during Makefile parsing before App compilation. A non-64 MB PSRAM build must explicitly disable App Host to use a configuration without it.

## Build and Artifact Evidence

The zero-argument command `make clean && make app && make sign-app && make pkg-app` passed. App version and package filename remain `4.3.1.465` and `ne301_App_signed_v4.3.1.465_pkg.bin`.

| Artifact | Baseline size | Baseline SHA-256 | Updated size | Updated SHA-256 |
|---|---:|---|---:|---|
| `build/ne301_App.bin` | 3,873,604 | `a89193f8a5036750e9d0ba5973209de825a672f18a7782088926fe360cd30112` | 4,086,588 | `bf2a2fad4e034cd3c0b905d4979011ea39e5083628ab59bec0b3656268204220` |
| `build/ne301_App_signed.bin` | 3,874,656 | `5cfbde4da63a386f9d4572987155272b349c35fd3f77eda4d9ad7d7b135af2e7` | 4,087,616 | `cec24082f810791e3d2dfe0a50e717e4d503c7efbad7bb18e201a3e377d5f5f5` |
| `build/ne301_App_signed_v4.3.1.465_pkg.bin` | 3,875,680 | `fb7525ed59d40cb649a0fe5af1fc5231ab9052e955e05159d575ad0cb5557db6` | 4,088,640 | `955b8a9473209facc9616c01ee50fe23a431d8aeb129d020ea81559eabfeae3c` |

Baseline ELF filtering found only `__app_host_ram_base__` and `__app_host_ram_end__`; the baseline firmware had no `/api/v1/app/` string. After the change, `arm-none-eabi-nm -C Appli/build/ne301_App.elf | rg 'app_admission|app_installer|app_host_|api_app_'` returned 68 matches, including 66 functional symbols and the two linker markers. Examples include `app_admission_verify_package`, `app_installer_storage_init`, `app_host_run_installed`, `app_host_session_start`, `app_host_control_status`, and `api_app_install_verify`. `rg -a -o -F '/api/v1/app/' build/ne301_App.bin` returned 18 matches.

`python3 Script/verify_ota_package.py build/ne301_App_signed_v4.3.1.465_pkg.bin` passed. The updated package retains OTAU magic `0x4F544155`, the 1,024-byte header, Application type, device model `0x3010`, and valid header and firmware CRCs. The baseline verifier also passed those checks. CRC values, payload hash, and sizes changed with the firmware contents; baseline CRCs were header `0x763FE62D` and firmware `0xFE8BDD3A`, while updated CRCs are header `0x912DEF69` and firmware `0x5A701993`.

### Version display

The package description and filename report `4.3.1.465`. `Script/ota_packer.py` writes both `fw_ver` and `min_ver` from the same `FirmwareInfo.version`; it has no separate minimum-version argument. Build 465 is `0x01D1`, so the raw fields are `[4, 3, 1, 209, 1, 0, 0, 0]` for both values. `Script/verify_ota_package.py` prints only the first four bytes, which explains its `4.3.1.209` display. This is existing verifier formatting behavior; no Script files were changed.

## Validation Matrix

Each C suite below was run once with `make -C tests/<suite> test`.

| Command | Result |
|---|---|
| `make clean && make app && make sign-app && make pkg-app` | PASS; zero App Host flags |
| `python3 Script/verify_ota_package.py build/ne301_App_signed_v4.3.1.465_pkg.bin` | PASS; OTA structure and CRCs |
| PSRAM 32 MB build command above | PASS as an explicit rejection; exit 2 |
| `tests/app-live-vnext-study` | PASS; 39 + 865 + 8 checks, 0 failures |
| `tests/app_admission` | PASS; 179 + 84 + 61 + 97 checks, 0 failures |
| `tests/app_host` | PASS; 21 checks, 0 failures |
| `tests/app_host_control` | PASS; 506 + 107 checks, 0 failures |
| `tests/app_host_live` | PASS; 300 + 296 checks, 0 failures |
| `tests/app_host_run` | PASS; 214 checks, 0 failures |
| `tests/app_host_session` | PASS; 828 checks, 0 failures |
| `tests/app_installer` | PASS; 271 + 280 + 173 + 54 checks, 0 failures |
| `tests/app_native_vnext` | PASS; 96 checks, 0 failures |
| `tests/app_report` | PASS; 552 + 575 checks, 0 failures |
| `tests/app_report_uninstall` | PASS; 130 checks, 0 failures |
| `tests/app_sidecar` | PASS; 155 checks, 0 failures |
| `tests/app_sidecar_lifecycle` | PASS; 351 checks, 0 failures |
| `tests/app_state` | PASS; 188 + 260 checks, 0 failures |
| `tests/report-policy-capacity-study` | FAIL at link: undefined `_app_state_store_purge_locked`; its final link command does not include `app_state_store.c`, which defines it. No test or product source was changed. |
| Full `make` and `make pkg` | NOT RUN: Web `node_modules` is absent; the installed ST Edge AI Core is v3.0.0 while repository default `STEDGEAI_VARIANT` is 4.0. |
| Device flashing, NVS, and hardware checks | NOT RUN; no device operation was performed. |

The implementation changes only build-time selection of existing App Host code. The admission, signature trust, installer, manual-run, session, and fail-closed state implementations were untouched; their existing C suites are included above. The App link map reports `APP_HOST_RAM: 0 B`, while the App Host functional symbols are present in the ELF; this reserved execution region has no static allocation in this image.
