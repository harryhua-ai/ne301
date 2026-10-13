# App Management Run Once eligibility

Issue #39 uses a conservative eligibility rule for the v1 Run Once action because `GET /api/v1/app/installed` does not report an ABI or compatibility profile.

The UI offers Run Once only when both conditions hold:

- No business UI is registered for the committed app ID.
- The committed record's `package_sha256` exactly matches a known signed package whose v1 Host ABI compatibility and installer acceptance are confirmed by checked-in fixtures.

App ID and business package version do not prove ABI compatibility. The version shown below is recorded for traceability only; eligibility is determined by the exact full-package digest and the fixture's Host result.

## Byte-level fixture evidence

The package arrays are in `tests/app_installer/installer_vectors.h`. SHA-256 values below were recalculated over each complete C byte array. The 16-byte container header begins with `NEAP` and the package type/version bytes `P 01 00 00`; the 160-byte `NMF1` manifest begins at package offset 16. Its required Host ABI is the little-endian 32-bit value at manifest offset 68 (package offset 84). The native image starts at package offset 176 with the `NEA1` image header; its ABI is at image-header offset 8 (package offset 184). The manifest and image-header ABI fields agree for every listed vector.

| Vector | Complete package SHA-256 | Container / manifest / image magic | Business version | Manifest ABI / image ABI | Installer or admission result | Run Once |
| --- | --- | --- | --- | --- | --- | --- |
| `HELLO_V1_PACKAGE` | `f98811f8f5eab265ad9e0df532bf71b1e54bdb2b8cc111866c2c4c87f1b7dcd8` | `NEAP` v1 / `NMF1` / `NEA1` | 1.0.0 | `0x00010000` / `0x00010000` | Fresh install accepted (`test_fresh_install_v1`) | Included |
| `HELLO_V2_PACKAGE` | `2a86a2982c042385850daac8962b3cef1d49f838e03a503032b0c6a5874a644f` | `NEAP` v1 / `NMF1` / `NEA1` | 2.0.0 | `0x00010000` / `0x00010000` | v1 1.0.0 to 2.0.0 upgrade accepted (`test_upgrade_v1_to_v2_and_uninstall`, `test_install_lifecycle_end_to_end`) | Included |
| `HELLO_V2_HISIG_PACKAGE` | `d803bfe64a59f39b3472ef6af1c29e85fa98a6634246460db3466e38fa641a02` | `NEAP` v1 / `NMF1` / `NEA1` | 2.0.0 | `0x00010000` / `0x00010000` | Signature verification returns `APP_ADMISSION_OK`; fresh installer accepts it (`test_same_tbs_alt_signature_idempotent`) | Included |
| `HELLO_V2_ALT_PACKAGE` | `c431baf86273a18554db312d804f27b46a0d83052c14241cf918507a0eef27c0` | `NEAP` v1 / `NMF1` / `NEA1` | 2.0.0 | `0x00010000` / `0x00010000` | Installer refuses a different TBS at the already-installed 2.0.0 version with `VERSION_CONFLICT` (`test_version_conflicts`, `test_install_lifecycle_end_to_end`); no committed-package positive fixture is present | Excluded conservatively |
| `HELLO_BADABI_PACKAGE` | `ea23ec9425cf19aafe6dedde29be72b7e49cd97a3c50a90593e5500e31aac1b5` | `NEAP` v1 / `NMF1` / `NEA1` | 1.2.0 | `0x00010001` / `0x00010001` | Signed fixture's required ABI differs from the Host policy `0x00010000`; admission rejects the mismatch | Excluded |
| `HELLO_BADBOARD_PACKAGE` | `f5eecda289fc32301841c87f7580d086c5d8126a5f27708907fcd6853d955616` | `NEAP` v1 / `NMF1` / `NEA1` | 1.1.0 | `0x00010000` / `0x00010000` | Installer refuses target board `0x00003110` as `TARGET_INCOMPATIBLE` (`test_install_lifecycle_end_to_end`) | Excluded |
| `HELLO_FOREIGN_PACKAGE` | `fd1ceacd4cec2805dec1d534f83ec73b147a3e93fbc7c708cbfb41d2d6f7e212` | `NEAP` v1 / `NMF1` / `NEA1` | 9.9.9 | `0x00010000` / `0x00010000` | Installer refuses its untrusted signature as `SIGNATURE_INVALID` (`test_install_lifecycle_end_to_end`) | Excluded |

`HELLO_V2_HISIG_PACKAGE` is the accepted high-S ECDSA signature form of the v2 package: `test_same_tbs_alt_signature_idempotent` verifies both package forms and confirms that the high-S variant can be installed from empty state. The eligibility list records the exact bytes that could be committed; when the base v2 package is already installed, the installer treats the high-S form as the same signed TBS and retains the committed base package digest.

`HELLO_V2_ALT_PACKAGE` is excluded because checked-in installer evidence only shows the same-version/different-TBS refusal. The `VERSION_CONFLICT` is not an ABI mismatch, but the current fixtures do not establish a committed ALT package that the UI can safely identify. Adding an exact package digest therefore requires a positive committed-install fixture.

The separate `tbs_sha256` covers the signed-to-executed portion and is not used for matching. `app_installer.c` computes `package_sha256` over the complete submitted package, and `api_app_install_module.c` returns it as lowercase hexadecimal.

Missing, unknown, or different package digests fail closed. A registered business UI suppresses Run Once even for a listed digest. v2/v3 session containers, the ABI-mismatched signed fixture, unsupported target profiles, and future packages are not inferred from app ID or business version and remain ineligible until exact compatibility evidence is added.

The Web regression in `Web/src/test/appManagement/regression.test.tsx` covers the accepted 1.0.0 and 2.0.0 v1 ABI packages, the accepted high-S fixture, the installer-refused ALT fixture, the ABI-mismatch fixture, an unknown digest, business UI suppression, and the Chinese Run Once copy. Hardware execution remains NOT_RUN.
