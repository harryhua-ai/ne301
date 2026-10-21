Base: `a20d055cff0334ec3a483088dc8d80e9ad451e96`
Workspace: `/Users/harryhua/Documents/GitHub/ne301/.worktree/host-prod-spki`

Task 1 started. Baseline suites: app_admission 421 checks, app_installer 735 checks, app-platform-offline 2 validations; all passed. The default installer gate reported `signed_pair_fixtures: NOT_RUN`.

Ruling: Add profile, public SPKI SHA-256, and provenance fields to the existing read-only admission status response — H3 requires the compiled identity to be observable; this adds no workflow or signature wire changes; cost if wrong: a small additive API response change.

Ruling: Keep feature code in a focused source commit and generated Docs sync in its own small commit because Docs CI requires committed manifests; review fixes are grouped into one focused follow-up commit.

Baseline false-accept reproduction: compiling `app_trust_anchor.c` and `.h` from `a20d055cff0334ec3a483088dc8d80e9ad451e96` against the existing `HELLO_V1_PACKAGE` returned `baseline_publisher=ne301-exp-pub baseline_status=provisioned exp_package_result=0`; the package was accepted before the profile fix. The production synthetic gate now refuses that same test package.

Final host validation: `tests/app_admission` 179 + 84 + 61 + 100 + 8 = 432 checks, 0 failures; 13 trust-profile compile cases, 0 failures. `tests/app_installer` 271 + 280 + 130 + 54 = 735 existing checks, 0 failures; the separate per-run synthetic production Pair gate adds 157 checks, 0 failures. `tests/app-platform-offline` 2 validations, both passed. Default root `make app` completed successfully; the ELF build response selected `APP_TRUST_PROFILE_TEST`, and the binary contains the existing test provenance and SHA-256. Invalid profile selection failed at the Appli Makefile guard.

H1-H3 boundary: no designated production publisher, SPKI, or authoritative provenance was supplied. Production without a provision header reports `unprovisioned`; actual production identity and device acceptance remain blocked. No private key is written by the synthetic fixture generator.

Independent review of `b5de268d22b76a28f4c7321aaf972bc8c4775f07` found that GNU Make does not track profile flag changes and that a generated synthetic header could be passed to the firmware production profile. Fixed with a profile/header-content stamp on `app_trust_anchor.o`, a firmware-only production provenance-class guard, a firmware compile-time rejection of the synthetic provenance prefix, and a compile-time test-publisher assertion; the existing runtime rejection remains. Host matrix now proves all three compiler rejections and the runtime test-publisher rejection.

Build-switch verification: root `make app` test profile, `make app APP_TRUST_PROFILE=production` with no header, then default `make app` all succeeded in the same Appli build directory. Each switch recompiled `app_trust_anchor.c`; production ELF strings identified `production` and `unprovisioned`, and switching back restored test-only provenance and the expected SHA-256. With ARM GNU 15.2.1, injecting a synthetic-class header and a `ne301-exp-pub` production header both failed compilation at the intended guards.

Final tests after review fixes: admission 179 + 84 + 61 + 100 + 8 = 432 checks, 0 failures; trust-profile matrix 14 cases, 0 failures, plus 3 firmware compiler rejections. Installer 271 + 280 + 130 + 54 = 735 checks, 0 failures; synthetic production Pair gate 157 checks, 0 failures. Offline suite: 2 validations passed. `python3 Script/gen_web_api_docs.py --check` passed.

Review boundary decision: retain the additive fields in the existing read-only App admission state response. H3 requires identifiable compiled profile, exact SPKI digest, and provenance; the authorized boundary prohibits NEPK/native/NEUI wire changes and Web UX, neither of which is changed. The API path is documented as an H3-only read-only diagnostic extension. The reviewer finding was evaluated and not applied.

Docs CI initially failed because the changed admission source invalidated its generated source hash. The two generated manifests were updated, local sync check passed, and PR #128 Docs sync passed at head `536134fd764a89d4958f50abbc113f3eb0c5d590`. Latest source fixes are pending push and CI rerun.

Scope rationale: `Custom/Services/Web/api/api_app_admission_module.c` adds only read-only profile/SHA/provenance fields required to identify the compiled trust anchor under H3. The two generated endpoint manifests update only the source hash for that API module as required by Docs CI. `.plans/` contains the task plan, ledger, and final summary required by the repository's AGENTS.md workflow. No package wire, product UX, #124 runtime, OTA, storage, or hardware path is changed.
