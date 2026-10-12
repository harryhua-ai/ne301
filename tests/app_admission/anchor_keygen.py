import argparse
import base64
import hashlib
import hmac
import json
import os
import re
import sys
import zlib

SEED_PREFIX = b"ne301-app-admission-fixture-v1|"
PUB = "ne301-exp-pub"
APP_ID = "anchor-fixture"
BOARD_ID = 0x00003010
PSRAM_MIB = 64
ABI_VERSION = 0x00010000
CAPS = 0x00000003
EXEC_BYTES = 4
TARGET_ADDR = 0x93E00000
NATIVE_PAYLOAD = bytes([0x00, 0x20, 0x70, 0x47])
IMAGE_FORMAT_VERSION = 1
IMAGE_HEADER_SIZE = 32
MANIFEST_SIZE = 160
CONTAINER_MAGIC = bytes([0x4E, 0x45, 0x41, 0x50, 0x50, 0x01, 0x00, 0x00])
MANIFEST_MAGIC = b"NMF1"

P = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF
A = P - 3
GX = 0x6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296
GY = 0x4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5
N = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551

KEY_LABELS = (
    ("current", b"ne301-exp-pub|anchor-current-v1"),
    ("superseded", b"ne301-exp-pub|anchor-superseded-v0"),
    ("foreign", b"unrelated-pub|never-trusted-v1"),
)


def inv_mod(x, m):
    return pow(x, -1, m)


def ec_add(p1, p2):
    if p1 is None:
        return p2
    if p2 is None:
        return p1
    x1, y1 = p1
    x2, y2 = p2
    if x1 == x2 and (y1 + y2) % P == 0:
        return None
    if p1 == p2:
        lam = (3 * x1 * x1 + A) * inv_mod(2 * y1, P) % P
    else:
        lam = (y2 - y1) * inv_mod(x2 - x1, P) % P
    x3 = (lam * lam - x1 - x2) % P
    y3 = (lam * (x1 - x3) - y1) % P
    return (x3, y3)


def ec_mul(k, point):
    result = None
    addend = point
    while k:
        if k & 1:
            result = ec_add(result, addend)
        addend = ec_add(addend, addend)
        k >>= 1
    return result


def derive_scalar(label):
    seed = hashlib.sha256(SEED_PREFIX + label).digest()
    d = int.from_bytes(seed, "big")
    while d < 1 or d >= N:
        seed = hashlib.sha256(seed).digest()
        d = int.from_bytes(seed, "big")
    return d


def keypair(label):
    d = derive_scalar(label)
    q = ec_mul(d, (GX, GY))
    return d, q


def spki_der(q):
    prefix = bytes.fromhex("3059301306072a8648ce3d020106082a8648ce3d03010703420004")
    return prefix + q[0].to_bytes(32, "big") + q[1].to_bytes(32, "big")


def spki_sha256(q):
    return hashlib.sha256(spki_der(q)).digest()


def bits2int(data):
    return int.from_bytes(data, "big")


def bits2octets(data):
    return (bits2int(data) % N).to_bytes(32, "big")


def rfc6979_k(d, h1):
    x = d.to_bytes(32, "big")
    h_o = bits2octets(h1)
    v = b"\x01" * 32
    k = b"\x00" * 32
    k = hmac.new(k, v + b"\x00" + x + h_o, hashlib.sha256).digest()
    v = hmac.new(k, v, hashlib.sha256).digest()
    k = hmac.new(k, v + b"\x01" + x + h_o, hashlib.sha256).digest()
    v = hmac.new(k, v, hashlib.sha256).digest()
    while True:
        v = hmac.new(k, v, hashlib.sha256).digest()
        candidate = bits2int(v)
        if 1 <= candidate <= N - 1:
            return candidate
        k = hmac.new(k, v + b"\x00", hashlib.sha256).digest()
        v = hmac.new(k, v, hashlib.sha256).digest()


def der_int(value):
    raw = value.to_bytes((value.bit_length() + 7) // 8 or 1, "big")
    if raw[0] & 0x80:
        raw = b"\x00" + raw
    return raw


def ecdsa_sign(d, tbs):
    h1 = hashlib.sha256(tbs).digest()
    e = bits2int(h1)
    while True:
        k = rfc6979_k(d, h1)
        point = ec_mul(k, (GX, GY))
        r = point[0] % N
        if r != 0:
            s = inv_mod(k, N) * (e + r * d) % N
            if s != 0:
                if s > N // 2:
                    s = N - s
                return r, s
        h1 = hashlib.sha256(h1).digest()
        e = bits2int(h1)


def der_sig_bytes(r, s):
    r_bytes = der_int(r)
    s_bytes = der_int(s)
    body = b"\x02" + bytes([len(r_bytes)]) + r_bytes
    body += b"\x02" + bytes([len(s_bytes)]) + s_bytes
    return b"\x30" + bytes([len(body)]) + body


def ecdsa_verify(q, tbs, sig_der):
    if sig_der[0] != 0x30:
        return False
    seq_len = sig_der[1]
    body = sig_der[2:2 + seq_len]
    if body[0] != 0x02:
        return False
    r_len = body[1]
    r = int.from_bytes(body[2:2 + r_len], "big")
    pos = 2 + r_len
    if body[pos] != 0x02:
        return False
    s_len = body[pos + 1]
    s = int.from_bytes(body[pos + 2:pos + 2 + s_len], "big")
    if not (1 <= r < N and 1 <= s < N):
        return False
    e = bits2int(hashlib.sha256(tbs).digest())
    w = inv_mod(s, N)
    p1 = ec_mul(e * w % N, (GX, GY))
    p2 = ec_mul(r * w % N, q)
    point = ec_add(p1, p2)
    if point is None:
        return False
    return point[0] % N == r


def native_image(payload):
    header = (
        (0x3141454E).to_bytes(4, "little")
        + IMAGE_HEADER_SIZE.to_bytes(2, "little")
        + IMAGE_FORMAT_VERSION.to_bytes(2, "little")
        + ABI_VERSION.to_bytes(4, "little")
        + TARGET_ADDR.to_bytes(4, "little")
        + len(payload).to_bytes(4, "little")
        + (0).to_bytes(4, "little")
        + (0).to_bytes(4, "little")
        + (zlib.crc32(payload) & 0xFFFFFFFF).to_bytes(4, "little")
    )
    return header + payload


def build_manifest(signer_spki):
    manifest = bytearray(MANIFEST_SIZE)
    manifest[0:4] = MANIFEST_MAGIC
    pub = PUB.encode("ascii")
    app = APP_ID.encode("ascii")
    manifest[4:4 + len(pub)] = pub
    manifest[20:20 + len(app)] = app
    manifest[52:58] = (1).to_bytes(2, "little") + (0).to_bytes(4, "little")
    manifest[60:64] = BOARD_ID.to_bytes(4, "little")
    manifest[64:68] = PSRAM_MIB.to_bytes(4, "little")
    manifest[68:72] = ABI_VERSION.to_bytes(4, "little")
    manifest[72:76] = CAPS.to_bytes(4, "little")
    manifest[76:80] = EXEC_BYTES.to_bytes(4, "little")
    image = native_image(NATIVE_PAYLOAD)
    manifest[80:84] = len(image).to_bytes(4, "little")
    manifest[84:88] = len(NATIVE_PAYLOAD).to_bytes(4, "little")
    manifest[88:92] = (0).to_bytes(4, "little")
    manifest[92:96] = TARGET_ADDR.to_bytes(4, "little")
    manifest[96:128] = hashlib.sha256(image).digest()
    manifest[128:160] = hashlib.sha256(signer_spki).digest()
    return bytes(manifest), image


def build_signed_package(d, q, fingerprint_spki=None):
    manifest, image = build_manifest(spki_der(q) if fingerprint_spki is None else fingerprint_spki)
    tbs = CONTAINER_MAGIC + len(manifest).to_bytes(4, "little")
    tbs += len(image).to_bytes(4, "little") + manifest + image
    return tbs + der_sig_bytes(*ecdsa_sign(d, tbs)), tbs


def split_sig(package):
    off = len(package) - len(package) + package.find(b"\x30", 16 + MANIFEST_SIZE)
    off = 16 + MANIFEST_SIZE + int.from_bytes(package[12:16], "little")
    seq_len = package[off + 1]
    body = package[off + 2:off + 2 + seq_len]
    r_len = body[1]
    pos = 2 + r_len
    s_len = body[pos + 1]
    return package[:off], body[2:2 + r_len], body[pos + 2:pos + 2 + s_len], off


def parse_c_byte_array(text, name):
    match = re.search(name + r"\[\]\s*=\s*\{(.*?)\}", text, re.S)
    if match is None:
        raise SystemExit("anchor constant not found: " + name)
    return bytes(int(tok, 16) for tok in re.findall(r"0x([0-9a-fA-F]{2})", match.group(1)))


def check_anchor_binding(repo_root, current_q):
    anchor_c = os.path.join(repo_root, "Custom", "Services", "AppHost", "app_trust_anchor.c")
    with open(anchor_c, "r", encoding="utf-8") as handle:
        text = handle.read()
    c_spki = parse_c_byte_array(text, "g_anchor_spki_der")
    c_digest = parse_c_byte_array(text, "g_anchor_spki_sha256")
    fixture_spki = spki_der(current_q)
    fixture_digest = spki_sha256(current_q)
    recomputed = hashlib.sha256(c_spki).digest()
    report = {
        "anchor_c": os.path.relpath(anchor_c, repo_root),
        "compiled_pinned_digest": c_digest.hex(),
        "recomputed_compiled_spki_digest": recomputed.hex(),
        "fixture_key_spki_digest": fixture_digest.hex(),
        "compiled_spki_equals_fixture_bytes": c_spki == fixture_spki,
        "pinned_digest_equals_fixture_digest": c_digest == fixture_digest,
        "recomputed_digest_equals_pinned": recomputed == c_digest,
    }
    print(json.dumps(report, indent=2, sort_keys=True))
    if not (report["compiled_spki_equals_fixture_bytes"]
            and report["pinned_digest_equals_fixture_digest"]
            and report["recomputed_digest_equals_pinned"]):
        raise SystemExit("ANCHOR BINDING FAILED: fixture key is not the compiled anchor key")
    return True


def c_array(name, data, per_line=12):
    lines = []
    for start in range(0, len(data), per_line):
        chunk = data[start:start + per_line]
        lines.append("    " + ", ".join("0x%02x" % b for b in chunk) + ",")
    return "static const uint8_t %s[] = {\n%s\n};\n" % (name, "\n".join(lines))


def emit_header(path, packages, keys):
    current_q = keys["current"][1]
    superseded_q = keys["superseded"][1]
    positive = packages["current"]
    superseded = packages["superseded"]
    foreign = packages["foreign"]
    image_len = IMAGE_HEADER_SIZE + len(NATIVE_PAYLOAD)
    tamper_offset = 16 + MANIFEST_SIZE + image_len - 1
    sig_offset = 16 + MANIFEST_SIZE + image_len
    parts = []
    parts.append("#ifndef ANCHOR_VECTORS_H\n")
    parts.append("#define ANCHOR_VECTORS_H\n\n")
    parts.append("#include <stddef.h>\n")
    parts.append("#include <stdint.h>\n\n")
    parts.append("#define ANCHOR_VECTOR_SPKI_DER_LEN 91u\n")
    parts.append("#define ANCHOR_VECTOR_SPKI_SHA256_LEN 32u\n")
    parts.append("#define ANCHOR_POSITIVE_PACKAGE_LEN %du\n" % len(positive))
    parts.append("#define ANCHOR_POSITIVE_SIG_OFFSET %du\n" % sig_offset)
    parts.append("#define ANCHOR_POSITIVE_TAMPER_OFFSET %du\n" % tamper_offset)
    parts.append("#define ANCHOR_SUPERSEDED_PACKAGE_LEN %du\n" % len(superseded))
    parts.append("#define ANCHOR_FOREIGN_PACKAGE_LEN %du\n" % len(foreign))
    parts.append("\n")
    parts.append(c_array("ANCHOR_VECTOR_SPKI_DER", spki_der(current_q)))
    parts.append("\n")
    parts.append(c_array("ANCHOR_VECTOR_SPKI_SHA256", spki_sha256(current_q)))
    parts.append("\n")
    parts.append(c_array("ANCHOR_POSITIVE_PACKAGE", positive))
    parts.append("\n")
    parts.append(c_array("ANCHOR_SUPERSEDED_SPKI_DER", spki_der(superseded_q)))
    parts.append("\n")
    parts.append(c_array("ANCHOR_SUPERSEDED_SPKI_SHA256", spki_sha256(superseded_q)))
    parts.append("\n")
    parts.append(c_array("ANCHOR_SUPERSEDED_PACKAGE", superseded))
    parts.append("\n")
    parts.append(c_array("ANCHOR_FOREIGN_PACKAGE", foreign))
    parts.append("\n#endif\n")
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("".join(parts))


def export_sec1_pem(out_dir, name, d):
    os.makedirs(out_dir, exist_ok=True)
    raw = d.to_bytes(32, "big")
    inner = b"\x02\x01\x01\x04\x20" + raw
    inner += bytes.fromhex("a00a06082a8648ce3d030107")
    der = b"\x30" + bytes([len(inner)]) + inner
    b64 = base64.encodebytes(der).replace(b"\n", b"")
    lines = [b64[i:i + 64] for i in range(0, len(b64), 64)]
    pem = b"-----BEGIN EC PRIVATE KEY-----\n" + b"\n".join(lines)
    pem += b"\n-----END EC PRIVATE KEY-----\n"
    out_path = os.path.join(out_dir, "anchor_fixture_%s.sec1.pem" % name)
    with open(out_path, "wb") as handle:
        handle.write(pem)
    print("exported " + out_path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--emit-header", default=None)
    parser.add_argument("--export-key-dir", default=None)
    args = parser.parse_args()

    repo_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    keys = {}
    for name, label in KEY_LABELS:
        keys[name] = keypair(label)

    packages = {}
    tbs_map = {}
    for name, _ in KEY_LABELS:
        d, q = keys[name]
        packages[name], tbs_map[name] = build_signed_package(d, q)
    foreign_d, foreign_q = keys["foreign"]
    packages["foreign"], tbs_map["foreign"] = build_signed_package(
        foreign_d, foreign_q, fingerprint_spki=spki_der(keys["current"][1]))

    for name, _ in KEY_LABELS:
        tbs, r, s, off = split_sig(packages[name])
        if not ecdsa_verify(keys[name][1], tbs, packages[name][off:]):
            raise SystemExit("self verify FAILED for " + name + " package")

    check_anchor_binding(repo_root, keys["current"][1])

    if args.export_key_dir is not None:
        for name, _ in KEY_LABELS:
            export_sec1_pem(args.export_key_dir, name, keys[name][0])

    if args.emit_header is not None:
        emit_header(args.emit_header, packages, keys)
        print("wrote " + args.emit_header)

    summary = {
        "current_spki_sha256": spki_sha256(keys["current"][1]).hex(),
        "superseded_spki_sha256": spki_sha256(keys["superseded"][1]).hex(),
        "foreign_spki_sha256": spki_sha256(keys["foreign"][1]).hex(),
        "positive_len": len(packages["current"]),
        "superseded_len": len(packages["superseded"]),
        "foreign_len": len(packages["foreign"]),
    }
    print(json.dumps(summary, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
