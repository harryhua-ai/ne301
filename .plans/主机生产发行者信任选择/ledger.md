Base: `a20d055cff0334ec3a483088dc8d80e9ad451e96`
Workspace: `/Users/harryhua/Documents/GitHub/ne301/.worktree/host-prod-spki`

Task 1 started. Baseline suites: app_admission 421 checks, app_installer 735 checks, app-platform-offline 2 validations; all passed. The default installer gate reported `signed_pair_fixtures: NOT_RUN`.

Ruling: Add profile, public SPKI SHA-256, and provenance fields to the existing read-only admission status response — H3 requires the compiled identity to be observable; this adds no workflow or signature wire changes; cost if wrong: a small additive API response change.

Ruling: Keep a single focused final source commit — the user's explicit delivery flow calls for a focused commit after all tests, while intermediate commits would make this PR harder to review; cost if wrong: the final commit will combine the tightly related trust profile, build wiring, and fixtures.

Baseline false-accept reproduction: compiling `app_trust_anchor.c` and `.h` from `a20d055cff0334ec3a483088dc8d80e9ad451e96` against the existing `HELLO_V1_PACKAGE` returned `baseline_publisher=ne301-exp-pub baseline_status=provisioned exp_package_result=0`; the package was accepted before the profile fix. The production synthetic gate now refuses that same test package.

Final host validation: `tests/app_admission` 179 + 84 + 61 + 100 + 8 = 432 checks, 0 failures; 13 trust-profile compile cases, 0 failures. `tests/app_installer` 271 + 280 + 130 + 54 = 735 existing checks, 0 failures; the separate per-run synthetic production Pair gate adds 157 checks, 0 failures. `tests/app-platform-offline` 2 validations, both passed. Default root `make app` completed successfully; the ELF build response selected `APP_TRUST_PROFILE_TEST`, and the binary contains the existing test provenance and SHA-256. Invalid profile selection failed at the Appli Makefile guard.

H1-H3 boundary: no designated production publisher, SPKI, or authoritative provenance was supplied. Production without a provision header reports `unprovisioned`; actual production identity and device acceptance remain blocked. No private key is written by the synthetic fixture generator.

Scope rationale: `Custom/Services/Web/api/api_app_admission_module.c` adds only read-only profile/SHA/provenance fields required to identify the compiled trust anchor under H3. `.plans/` contains the task plan, ledger, and final summary required by the repository's AGENTS.md workflow. No product UX, protocol, #124 runtime, OTA, storage, or hardware path is changed.
