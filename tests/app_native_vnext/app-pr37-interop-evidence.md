# App PR #37 to Host #104 v4 interoperability evidence

## Exact source and Host revisions

| Repository | Exact revision | Evidence source |
| --- | --- | --- |
| `harryhua-ai/ne30x-app` | `71f1850063f022b86f3b027e27eaabb992de60b0` | PR #37 checkout; `tests/package-vnext/work/pkg` provenance records this `source_commit` for the generated live-only and Enable packages |
| `harryhua-ai/ne301` | `2a7cad0393b4642252f76d5e0fa5652e6b637ca7` | Host parser, C tests, and exact App package fixtures tested below |

The App worktree was read-only. The tracked App golden is `docs/evidence/package-vnext/spec/golden.neapp.v4`. The live-only and Enable packages are the App-generated files `tests/package-vnext/work/pkg/lc-test-app-live-4.0.0.neapp.v4` and `tests/package-vnext/work/pkg/lc-test-app-enable-4.0.0.neapp.v4`; their provenance files pin the App source revision and package, TBS, image, and SPKI hashes. Host fixtures under `tests/app_native_vnext/fixtures/app-pr37/` are byte-for-byte copies of those App outputs and public test SPKIs.

## App package bytes and Host C results

| App package | Bytes | Package SHA-256 | TBS SHA-256 | Image SHA-256 | Publisher / SPKI SHA-256 | Required caps and quota fields | Host C result |
| --- | ---: | --- | --- | --- | --- | --- | --- |
| Tracked v4 golden | 394 | `e6a7fedc8cda9f3900311bc077de2a3ea79002d10b23241d1690c9e28ee46e0e` | `581e3ae98afef313de0bdf7d8bc85c2a36585a7c28e609dea183a558e599e095` | `845e09288e0f1d876832dcda553580c6aa64ef17f08b9e529550aefa4db12cd1` | `test-publisher` / `5cd252fb0ce8932436faf8ccd1040981b89ee4ad6b9fe9e2a2b7e71aacb27cd3` | `0x3ff` | `trusted_compatible` |
| Live-only | 394 | `ba64faefea77a1569a2b96f86a4ac356d34f27cbd7e90808af21b7a9b38ae2b2` | `5c61a2522b566af23f46d56cdb6ba82db11a663a2ff6bde7c86fc5981e78111c` | `845e09288e0f1d876832dcda553580c6aa64ef17f08b9e529550aefa4db12cd1` | `dev-publisher` / `dc0ce633dbcc913dafafa4b89ac44d8ce683fdfc3f60c8bdf21213b9f2b534ba` | `0x223`, control `0`, live publish `9008`, tracks `64` | Verify `trusted_compatible`; install `APP_INSTALL_OK`; session denied `ABI_INCOMPATIBLE` |
| Enable | 395 | `1d6356272535efc854eb968ea23f694144b111ab1830137a81d7dad7461c75a7` | `26df9606642ae23fe14ca570e8ad5c3b88097a1e8d88a18008dae7e0b40127f4` | `845e09288e0f1d876832dcda553580c6aa64ef17f08b9e529550aefa4db12cd1` | `dev-publisher` / `dc0ce633dbcc913dafafa4b89ac44d8ce683fdfc3f60c8bdf21213b9f2b534ba` | `0x173`, state quota `4096`, control `256` | Verify `trusted_compatible`; install `APP_INSTALL_OK`; session denied `ABI_INCOMPATIBLE` |

The v4 test policy uses board `0x3010`, ABI `0x00040000`, the exact matching public test issuer/SPKI, all known v4 capabilities, live publish capacity `9008`, and live track capacity `64`. The session test installs each App package under that matching admission policy, then independently exercises the Host session authorization path. The current Host session capability mask does not grant bit 8 or bit 9, so both sessions fail closed; no live runtime permission is inferred.

## Reproduction commands and output

The package byte hashes were checked in the App worktree and then in the Host fixture directory with:

```text
shasum -a 256 docs/evidence/package-vnext/spec/golden.neapp.v4 tests/package-vnext/work/pkg/lc-test-app-live-4.0.0.neapp.v4 tests/package-vnext/work/pkg/lc-test-app-enable-4.0.0.neapp.v4
e6a7fedc8cda9f3900311bc077de2a3ea79002d10b23241d1690c9e28ee46e0e  docs/evidence/package-vnext/spec/golden.neapp.v4
ba64faefea77a1569a2b96f86a4ac356d34f27cbd7e90808af21b7a9b38ae2b2  tests/package-vnext/work/pkg/lc-test-app-live-4.0.0.neapp.v4
1d6356272535efc854eb968ea23f694144b111ab1830137a81d7dad7461c75a7  tests/package-vnext/work/pkg/lc-test-app-enable-4.0.0.neapp.v4

shasum -a 256 tests/app_native_vnext/fixtures/app-pr37/enable.neapp.v4 tests/app_native_vnext/fixtures/app-pr37/golden.neapp.v4 tests/app_native_vnext/fixtures/app-pr37/live-only.neapp.v4 tests/app_native_vnext/fixtures/app-pr37/dev-publisher.spki.der tests/app_native_vnext/fixtures/app-pr37/test-publisher.spki.der
1d6356272535efc854eb968ea23f694144b111ab1830137a81d7dad7461c75a7  tests/app_native_vnext/fixtures/app-pr37/enable.neapp.v4
e6a7fedc8cda9f3900311bc077de2a3ea79002d10b23241d1690c9e28ee46e0e  tests/app_native_vnext/fixtures/app-pr37/golden.neapp.v4
ba64faefea77a1569a2b96f86a4ac356d34f27cbd7e90808af21b7a9b38ae2b2  tests/app_native_vnext/fixtures/app-pr37/live-only.neapp.v4
dc0ce633dbcc913dafafa4b89ac44d8ce683fdfc3f60c8bdf21213b9f2b534ba  tests/app_native_vnext/fixtures/app-pr37/dev-publisher.spki.der
5cd252fb0ce8932436faf8ccd1040981b89ee4ad6b9fe9e2a2b7e71aacb27cd3  tests/app_native_vnext/fixtures/app-pr37/test-publisher.spki.der
```

Host direct admission/install verification ran against the committed App bytes using the real C `app_admission_verify_package()` and `app_install()` paths:

```text
CPATH=/private/tmp/ne301-104-r2-version-headers PYTHONDONTWRITEBYTECODE=1 make -s -C tests/app_native_vnext test
APP37_C case=tracked-golden package_len=394 result=trusted_compatible publisher=test-publisher caps=0x000003ff tbs_sha256=581e3ae98afef313de0bdf7d8bc85c2a36585a7c28e609dea183a558e599e095 image_sha256=845e09288e0f1d876832dcda553580c6aa64ef17f08b9e529550aefa4db12cd1 spki_sha256=5cd252fb0ce8932436faf8ccd1040981b89ee4ad6b9fe9e2a2b7e71aacb27cd3
APP37_C case=live-only package_len=394 result=trusted_compatible publisher=dev-publisher caps=0x00000223 tbs_sha256=5c61a2522b566af23f46d56cdb6ba82db11a663a2ff6bde7c86fc5981e78111c image_sha256=845e09288e0f1d876832dcda553580c6aa64ef17f08b9e529550aefa4db12cd1 spki_sha256=dc0ce633dbcc913dafafa4b89ac44d8ce683fdfc3f60c8bdf21213b9f2b534ba
APP37_C negative=tbs-tamper result=SIGNATURE_INVALID
APP37_C negative=signature-tamper result=SIGNATURE_INVALID
APP37_C negative=unsigned result=BAD_PACKAGE
APP37_C negative=wrong-spki result=PUBLISHER_UNTRUSTED
APP37_C cross-major-baseline=accepted_v3 result=trusted_compatible
APP37_C negative=cross-major result=ABI_INCOMPATIBLE
APP37_C install=live-only result=APP_INSTALL_OK
APP37_C case=enable package_len=395 result=trusted_compatible publisher=dev-publisher caps=0x00000173 tbs_sha256=26df9606642ae23fe14ca570e8ad5c3b88097a1e8d88a18008dae7e0b40127f4 image_sha256=845e09288e0f1d876832dcda553580c6aa64ef17f08b9e529550aefa4db12cd1 spki_sha256=dc0ce633dbcc913dafafa4b89ac44d8ce683fdfc3f60c8bdf21213b9f2b534ba
APP37_C install=enable result=APP_INSTALL_OK
APP37_C authorization=live-only supported_caps=0x000001ff result=ABI_INCOMPATIBLE
app_native_vnext: 77 checks, 0 failures
```

The direct session/install checks used the same App bytes and explicit `dev-publisher` test trust:

```text
make -s -C tests/app_host_session test
APP37_SESSION case=live-only install=0 session=-3 reason=ABI_INCOMPATIBLE
APP37_SESSION case=enable install=0 session=-3 reason=ABI_INCOMPATIBLE
app_host_session: total=592 fail=0
```

## Negative coverage and F3 boundary

The parser tests accept a generic v4 package with caps `0x23` and `control_max=0`, and the App live-only package with caps `0x223`, bit 9 plus bit 5, no bit 6, and zero control quota. They reject `ENABLE_CONTROL` without its bit 4/5/6 dependencies as `BAD_PACKAGE`; the live closure vector with `live_publish_max=9007` and 64 tracks is `RESOURCE_LIMIT` because its closed maximum is 9008. The C interop test also covers TBS/signature mutation, unsigned input, wrong SPKI, and v3 input under the v4 policy. The same v3 package is accepted under its matching v3 policy and returns `ABI_INCOMPATIBLE` under the v4 policy.

Existing install/session C checks remain active: `test_install_floor_and_confirmed_retirement` covers floor enforcement, owner conflict, and downgrade refusal; `test_v4_session_and_paused_floor_rollback` covers stale receipt generation, floor identity/generation conflicts, rollback, and floor persistence. No floor, intent, uninstall, or v3 rule was changed.

Against frozen F3 comment `6258534821`, the change removes only the v4-wide requirement for bit 6 and makes the 32-byte control wire minimum conditional on bit 6. Bit 9 still requires bit 5 and retains its live quota/track closure; bit 8 still requires bits 4, 5, and 6 and retains its existing ENABLE_CONTROL/ENB1 quota checks. Line Crossing's stricter business profile remains enforced only for that profile. F3 and v3 admission semantics are unchanged.

The exact nine-suite acceptance set from the prior PR snapshot was rerun at 3,559 checks / 0 failures: admission `421/0`, session `592/0`, control `548/0`, run `214/0`, sidecar lifecycle `351/0`, report uninstall `130/0`, installer `778/0`, state `448/0`, and vNext `77/0`. This is the prior 3,531-check baseline plus 9 new session checks and 19 new vNext checks. The report-uninstall suite was run directly with `CPATH=/private/tmp/ne301-104-r2-version-headers PYTHONDONTWRITEBYTECODE=1 make -s -C tests/app_report_uninstall test`.

The extended 11-suite list from `tests/app-platform-offline/run.py` also completed with exit 0. Its emitted counts were admission `421/0`, installer `778/0`, Host run `214/0`, session `592/0`, Host control `548/0`, sidecar `155/0`, sidecar lifecycle `351/0`, state `448/0`, report `1,127/0`, and storage gate `3,112/0`; the offline asset audit and signed NEUI checks both printed `PASS`. These suites overlap with the nine-suite acceptance set. The wrapper's cleanup preflight refused to remove the pre-existing `tests/app_sidecar/build`, so each listed `make test` suite was run directly with generated version-header `CPATH`; that pre-existing build tree was preserved. Device tests: `NOT_RUN`.
