# Default Build Includes App Host

## r3 Root Default Goal Correction and Fresh Evidence (2026-10-21)

This addendum records the new #110 Claim `harryhua-ai-20261021T033011-865bd2c4`. The r2 evidence from candidate `5c5701db8d04c04b9b36946345c9c6d3aacfae24` is carried forward only as evidence; its Claim and REQUEST_CHANGES review are not reused. The 14 App Host C-suite results, the known capacity-study linker failure, and the 45 Web bridge tests are recorded in the [r2 PR #125](https://github.com/harryhua-ai/ne301/pull/125) evidence.

### Root Makefile change

The root `Makefile` now sets `.DEFAULT_GOAL := all` immediately after including `version.mk` and `stedgeai.mk`. Before this change, the included `version.mk` declared `version` first, so a bare `make` selected that target and printed version information without building a component. The new default selects the existing `all` target (`fsbl app web model wakecore`). No target recipe, dependency, flag, signing/package rule, or flash rule changed. `version.mk` is unchanged.

### Fresh build and package matrix

The fresh clean build and package used ST Edge AI Core `v4.0.1-20581` through the environment only; no Make variables were passed to the full build:

```sh
export STEDGEAI_CORE_DIR=/Applications/ST/STEdgeAI/4.0
export PATH=/Applications/ST/STEdgeAI/4.0/Utilities/macarm:$PATH
make clean && make
make pkg
```

| Command | Result |
|---|---|
| `make clean && make` | PASS, exit 0. The log shows `Building fsbl/app/web/model/wakecore`, each component's completion line, and `Firmware Build Complete!`. |
| `make pkg` | PASS, exit 0. App, FSBL, Model, Web, and WiFi OTA packages were generated. |
| App Host ELF symbol check (command below) | 68 matches: 66 functional App Host symbols and two linker-region markers. Includes `app_admission_verify_package`, `app_installer_storage_init`, `app_host_run_installed`, `app_host_session_start`, `app_host_control_status`, and `api_app_install_verify`. |
| App route string check (command below) | 19 route-string matches. |
| OTA verifier loop (command below) | PASS, 5/5 packages. All have OTAU magic `0x4F544155`, a 1,024-byte header, device model `0x3010`, and no encryption or compression. |
| App `fw_ver` / `min_ver` fields at `0xA0` / `0xA8` | Both are `[4, 3, 1, 211, 1, 0, 0, 0]`; the 16-bit little-endian build is 467. The verifier's four-byte display shows 211 because it omits the high build byte. |
| `make app COMMON_DEFS=-DBOARD_PSRAM_SIZE=32` | Expected rejection, exit 2: `APP_HOST_POC=1 requires BOARD_PSRAM_SIZE=64`, before App compilation. |

Exact read-only checks:

```sh
arm-none-eabi-nm -C Appli/build/ne301_App.elf | rg -e app_admission -e app_installer -e app_host_ -e api_app_
rg -a -o -F '/api/v1/app/' build/ne301_App.bin | wc -l
for f in build/*_pkg.bin; do python3 Script/verify_ota_package.py "$f"; done
```

Fresh component artifacts:

| Artifact | Bytes |
|---|---:|
| `build/ne301_FSBL.bin` | 83,820 |
| `build/ne301_App.bin` | 4,086,588 |
| `build/ne301_Web.bin` | 302,417 |
| `build/ne301_Model.bin` | 3,250,280 |
| `build/ne301_WakeCore.bin` | 98,628 |

SHA-256 for every package produced by `make pkg`:

| Package | Bytes | SHA-256 |
|---|---:|---|
| `build/ne301_App_signed_v4.3.1.467_pkg.bin` | 4,088,640 | `0a88a53e287caff9efc729720f3bf5fbfe52663a9c3007dcbe6869c0c2983dfe` |
| `build/ne301_FSBL_signed_v1.0.3.1_pkg.bin` | 85,888 | `22354ef7fa7836ba9e079110f086fbe0a9f67244c46cd1f105774fb162fd5064` |
| `build/ne301_Model_v4.0.0.0_pkg.bin` | 3,251,304 | `5bf9b41dbcc0b5d0804dd95bf129e460e5585ff359a5c675e6ddafd3ad04f335` |
| `build/ne301_Web_v1.5.0.8_pkg.bin` | 303,441 | `ea2883695f3ddf5e92efd44207b86c5c4d0ba7dae831bcb6de9c842ec33c8c34` |
| `build/ne301_Wifi_flash_v2.16.5.108_pkg.bin` | 1,771,968 | `36da2e800aeb81753a437e2249db43ba4e6356aeab1c866b865ad51f6e2d6503` |

No device operation was performed. The build emitted a non-fatal Web circular-dependency warning and existing frontend data/chunk-size notices; Model generation used the v4 `stedgeai` executable from `PATH` and verified it as `v4.0.1-20581`.

## r2 Web Type Corrections and Fresh Build Evidence (2026-10-21)

This historical r2 addendum follows candidate `91cafbdc` and records the fresh checks performed for PR #125 candidate `5c5701db`. Its baseline and 14-suite App Host results are carried forward as evidence from the [r2 PR #125](https://github.com/harryhua-ai/ne301/pull/125); its Claim and review verdict are not current authority. The r3 root default-goal correction and fresh checks are recorded above.

The Web build needed two type-only corrections in the exact files authorized by A:

- `Web/src/services/sidecarBridge.ts`: assert the known live event data as `Record<string, unknown>` at the outbound message boundary. The assertion is erased by TypeScript and does not copy or change the data.
- `Web/src/test/appManagement/sidecarBridgeLive.test.ts`: type the harness function as `BridgeServerCall`, matching the function passed to `SidecarBroker` and the existing broker harness.

No other Web path, runtime branch, dependency, package file, or lockfile changed. `cd Web && pnpm exec tsc -b` passed. `pnpm vitest run src/test/appManagement/sidecarBridgeLive.test.ts src/test/appManagement/sidecarBridge.test.ts` passed: 2 files, 45 tests.

### Root build and package matrix

| Command | Result |
|---|---|
| `make clean && make` | Exit 0, but built no component. GNU Make selected the `version` goal from included `version.mk` before reaching the root `all` target; the command printed version information only. This is not full-build evidence. |
| `make clean && make all` with the shell's original ST Edge AI 3.0 environment | Exit 2 at Model: repository default is `STEDGEAI_VARIANT=4.0`, but `STEDGEAI_CORE_DIR` selected v3.0.0. |
| `make clean && make all` with the v4.0 environment below | PASS, exit 0; FSBL, App, Web, Model, and WakeCore all built. No `STEDGEAI_VARIANT` or other Make variable was supplied. |
| `make pkg` with the same v4.0 environment | PASS, exit 0; all five OTA packages, including WiFi, were generated. |

The successful full build and package used the installed ST Edge AI Core `v4.0.1-20581` by setting the toolchain environment, without passing build variables to Make:

```sh
export STEDGEAI_CORE_DIR=/Applications/ST/STEdgeAI/4.0
export PATH=/Applications/ST/STEdgeAI/4.0/Utilities/macarm:$PATH
make clean && make all
make pkg
```

Fresh component outputs from `make all`:

| Artifact | Bytes |
|---|---:|
| `build/ne301_FSBL.bin` | 83,820 |
| `build/ne301_App.bin` | 4,086,588 |
| `build/ne301_Web.bin` | 302,417 |
| `build/ne301_Model.bin` | 3,250,280 |
| `build/ne301_WakeCore.bin` | 98,628 |

SHA-256 for every OTA package produced by `make pkg`:

| Package | Bytes | SHA-256 |
|---|---:|---|
| `build/ne301_App_signed_v4.3.1.466_pkg.bin` | 4,088,640 | `fefc7b42199a858ce38141c4642395837974976af2e1c99e47089978daa45aec` |
| `build/ne301_Web_v1.5.0.8_pkg.bin` | 303,441 | `31a2b11236403a7d63b36a3433fc90b749becc709ee7ac14688257b1118dbdd8` |
| `build/ne301_Model_v4.0.0.0_pkg.bin` | 3,251,304 | `4558c53710a2e3e38841af14a077dab933fe4c3ca199814d3b864f49fee32c8c` |
| `build/ne301_FSBL_signed_v1.0.3.1_pkg.bin` | 85,888 | `da0ed8d164d88555d057029db3320fba8baf48f58cd590625001d5056319d5c2` |
| `build/ne301_Wifi_flash_v2.16.5.108_pkg.bin` | 1,771,968 | `68bf991edef5d6b16c64e6d2191fdbf14dba164d2411307c5ded34d99e4d36e2` |

### App Host and OTA checks

Compared with the r1 baseline at `279fa020` in the prior report, whose filtered ELF output contained only the two App Host linker-region markers and whose App binary had no `/api/v1/app/` string, the fresh App ELF has 68 filtered symbols: 66 functional symbols plus the two markers. The output includes `app_admission_verify_package`, `app_installer_storage_init`, `app_host_run_installed`, `app_host_session_start`, `app_host_control_status`, and `api_app_install_verify`. The fresh `build/ne301_App.bin` contains 19 `/api/v1/app/` matches.

`python3 Script/verify_ota_package.py build/ne301_App_signed_v4.3.1.466_pkg.bin` passed. The package retains magic `0x4F544155`, a 1,024-byte header, Application type, device model `0x3010`, and no encryption or compression. Header CRC is `0x48EA6717`; firmware CRC is `0x31F82547`. The filename carries build `4.3.1.466`; the existing verifier displays `4.3.1.210` from the low byte of the version field, as described in the version note below. No Script file changed.

`make app COMMON_DEFS=-DBOARD_PSRAM_SIZE=32` exited 2 with `APP_HOST_POC=1 requires BOARD_PSRAM_SIZE=64`, before App compilation.

In r2, bare `make` selected only `version` and did not invoke `all`; that historical result did not establish a five-component zero-argument build. The r3 correction and fresh bare `make` evidence are recorded above.

## Change

The earlier App Host change set `APP_HOST_POC ?= 1` in `Appli/Makefile`. The existing guarded source list, compile define, include paths, and Web API registration are therefore included by the ordinary zero-argument App build; the linker layout remains unchanged. That App Host change did not modify packers, package naming, or runtime security code. The separate r3 root default-goal selection change is documented above.

The baseline was collected before the App Host change at `279fa0202b7c321ce83e917406f55c9ade0b84bc` and is recorded in the r2 evidence on PR #125; it was not rebuilt for this task.

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

The r2 C-suite results below were each run once with `make -C tests/<suite> test` and are carried forward from PR #125; they were not rerun in r3.

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
| Full `make` and `make pkg` in the earlier `91cafbdc` run | NOT RUN at that time: Web `node_modules` was absent and the selected ST Edge AI Core was v3.0.0. The fresh r2 results are recorded above. |
| Device flashing, NVS, and hardware checks | NOT RUN; no device operation was performed. |

The earlier App Host implementation changes only build-time selection of existing App Host code. The admission, signature trust, installer, manual-run, session, and fail-closed state implementations were untouched; their existing C suites are included above. The App link map reports `APP_HOST_RAM: 0 B`, while the App Host functional symbols are present in the ELF; this reserved execution region has no static allocation in this image.
