# NEAPP05 unsigned single-file package

This document defines the byte format accepted by the Host app installer. The package contains one Native member and one UI member. The Host parser is implemented by `app_admission_verify_package`, `app_pair_verify_package`, and `app_sidecar_verify_bundle` in `Custom/Services/AppHost/`.

SHA-256 fields below check that members and stored content match one another. They do not authenticate a publisher or source, and they do not provide trusted anti-rollback protection. Installation still requires high-privilege administrator authentication and an explicit install action.

## Outer package

All integers are unsigned little-endian. The complete package is exactly the 16-byte header followed by the Native member and then the UI member.

| Offset | Size | Meaning |
| --- | ---: | --- |
| 0 | 8 | Magic `4e45415050050000` (`NEAPP`, version 5) |
| 8 | 4 | Native member length |
| 12 | 4 | UI member length |
| 16 | Native length | Native member |
| 16 + Native length | UI length | UI member |

The member lengths must be nonzero, within their limits, and sum exactly to the input length using overflow-safe arithmetic. Native member length is at most 2,097,376 bytes; UI member length is at most 2,113,552 bytes; the whole package is at most 4,210,944 bytes.

## Native member

The Native member uses the existing 16-byte member header and 208-byte v4 policy layout, with the new version markers and trust fields removed. Its exact length is `16 + manifest_len + image_len`.

| Offset | Size | Meaning |
| --- | ---: | --- |
| 0 | 8 | Magic `4e45415050050000` |
| 8 | 4 | Manifest length; exactly 208 |
| 12 | 4 | NEA1 image length; at least 32 and at most 2,097,152 |
| 16 | 208 | NMF5 manifest |
| 224 | image length | NEA1 image header followed by Native payload |

NMF5 manifest offsets are:

| Offset | Size | Meaning |
| --- | ---: | --- |
| 0 | 4 | `NMF5` |
| 4 | 16 | Zero-filled former publisher slot |
| 20 | 32 | App ID, lower-case ASCII letters, digits, and hyphens; zero-padded |
| 52 | 6 | Native major, minor, patch as three u16 values |
| 58 | 2 | Reserved zero |
| 60 | 4 | Target board ID |
| 64 | 4 | Required PSRAM in MiB |
| 68 | 4 | Required Host ABI; current value `0x00040000` |
| 72 | 4 | Required Host capability mask |
| 76 | 4 | Required execution-region bytes |
| 80 | 4 | NEA1 image length, including its 32-byte header |
| 84 | 4 | Native payload length, excluding the NEA1 header |
| 88 | 4 | Native entry offset |
| 92 | 4 | Target address |
| 96 | 32 | SHA-256 of the complete NEA1 image (header and payload) |
| 128 | 32 | Zero-filled former publisher-key digest slot |
| 160 | 4 | Run profile; current session profile is 3 |
| 164 | 4 | Event maximum |
| 168 | 4 | State quota |
| 172 | 4 | Report maximum |
| 176 | 4 | Control maximum |
| 180 | 4 | Snapshot maximum |
| 184 | 4 | Live publish maximum |
| 188 | 4 | Live track maximum |
| 192 | 16 | Reserved zero |

The NEA1 header retains its existing 32-byte structure: magic `NEA1`, u16 header size 32, u16 format 1, u32 ABI, target address, payload size, entry offset, zero reserved word, and CRC32 of the Native payload. Header fields must agree with NMF5 and Host policy. The Host also checks the NMF5 image SHA-256.

The Host v5 implementation rejects a Native manifest whose major version is zero.

## UI member

The UI member has no signature tail. Its exact length is `16 + manifest_len + payload_len`.

| Offset | Size | Meaning |
| --- | ---: | --- |
| 0 | 8 | Magic `4e45554902000000` (`NEUI`, version 2) |
| 8 | 4 | Manifest length; 175 through 16,384 |
| 12 | 4 | Asset payload length; 1 through 2,097,152 |
| 16 | manifest length | UI manifest |
| 16 + manifest length | payload length | Asset file contents, concatenated in manifest order |

UI manifest fixed fields occupy 136 bytes:

| Offset | Size | Meaning |
| --- | ---: | --- |
| 0 | 16 | Zero-filled former publisher slot |
| 16 | 32 | App ID, equal to NMF5 App ID; zero-padded |
| 48 | 32 | Zero-filled former publisher-key digest slot |
| 80 | 32 | SHA-256 of the complete Native member |
| 112 | 6 | UI major, minor, patch as three u16 values; the Host v5 implementation rejects a UI version if any component is zero |
| 118 | 2 | File count; 1 through 64 |
| 120 | 4 | Host UI ABI; current value 1 |
| 124 | 6 | Bound Native major, minor, patch; equal to NMF5 |
| 130 | 6 | Reserved zero |

Each file record follows the fixed fields and has this layout:

| Record offset | Size | Meaning |
| --- | ---: | --- |
| 0 | 2 | UTF-8-independent ASCII path length; 1 through 160 |
| 2 | 4 | File length; 1 through 1,048,576 |
| 6 | 32 | SHA-256 of this file's payload bytes |
| 38 | path length | Relative path bytes |

Records are sorted in strictly increasing byte order. Paths use printable ASCII, `/` as the only separator, and segments beginning with an ASCII letter or digit; later segment bytes may also contain `.`, `_`, and `-`. Absolute paths, empty segments, `.` and `..` segments, backslashes, controls, and case-insensitive duplicate paths are rejected. Exactly one path must be `index.html`. The sum of file lengths must equal `payload_len`, and all records must end exactly at `manifest_len`.

## Legacy and persistent state

Host explicitly rejects signed Native containers `NEAPP01` through `NEAPP04`, signed UI `NEUI01`, and signed outer pair `NEPK`. Unknown magic, truncation, inconsistent lengths, arithmetic overflow, invalid paths, binding mismatches, and digest mismatches are rejected. No legacy bytes are reinterpreted as NEAPP05.

The existing Native install record has no format-version field. The Host identifies a new local install by publisher identity `admin-installed` and an all-zero publisher-key digest. A different identity or nonzero key digest is treated as a preserved legacy signed record and blocks unsigned installation. Existing pair-history floors, Native vNext floors owned by a signed identity, staged transactions, corrupt slots, and uncertain storage are not cleared to permit an install. Explicit administrator uninstall retains its existing confirmation and transaction path.

New packages carry no trusted release sequence. The Host does not create a pair-history release sequence for them and does not claim cryptographically trustworthy anti-rollback protection.
