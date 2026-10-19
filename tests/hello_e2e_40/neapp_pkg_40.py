import base64
import hashlib
import hmac
import zlib

ANCHOR_SPKI_SHA256 = "06e732b1b94daeeef433297f7a8e797bf72b2fd3b882a4ec6a5490352931ebda"
PUBLISHER_ID = "ne301-exp-pub"
APP_ID = "hello-app"
BOARD_ID = 0x00003010
PSRAM_MIB = 64
ABI_VERSION = 0x00010000
CAPS = 0x00000003
EXEC_BASE = 0x93E00000
MANIFEST_SIZE = 160
NATIVE_HEADER_SIZE = 32
CONTAINER_MAGIC = bytes([0x4E, 0x45, 0x41, 0x50, 0x50, 0x01, 0x00, 0x00])
MANIFEST_MAGIC = b"NMF1"
NATIVE_MAGIC = 0x3141454E
SPKI_PREFIX = bytes.fromhex("3059301306072a8648ce3d020106082a8648ce3d03010703420004")

P = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF
A = P - 3
GX = 0x6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296
GY = 0x4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5
N = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551


def inv_mod(x, m):
    return pow(x, -1, m)


def ec_add(p1, p2):
    if p1 is None:
        return p2
    if p2 is None:
        return None if p1 is None else p2
    if p1[0] == p2[0] and (p1[1] + p2[1]) % P == 0:
        return None
    if p1 == p2:
        lam = (3 * p1[0] * p1[0] + A) * inv_mod(2 * p1[1], P) % P
    else:
        lam = (p2[1] - p1[1]) * inv_mod(p2[0] - p1[0], P) % P
    x3 = (lam * lam - p1[0] - p2[0]) % P
    y3 = (lam * (p1[0] - x3) - p1[1]) % P
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


def spki_der(q):
    return SPKI_PREFIX + q[0].to_bytes(32, "big") + q[1].to_bytes(32, "big")


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
    if len(sig_der) < 8 or sig_der[0] != 0x30:
        return False
    seq_len = sig_der[1]
    if 2 + seq_len > len(sig_der):
        return False
    body = sig_der[2:2 + seq_len]
    if not body or body[0] != 0x02:
        return False
    r_len = body[1]
    r = int.from_bytes(body[2:2 + r_len], "big")
    pos = 2 + r_len
    if pos + 2 > len(body) or body[pos] != 0x02:
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


def der_element(data, pos):
    tag = data[pos]
    first = data[pos + 1]
    if first & 0x80:
        n = first & 0x7F
        length = int.from_bytes(data[pos + 2:pos + 2 + n], "big")
        start = pos + 2 + n
    else:
        length = first
        start = pos + 2
    return tag, data[start:start + length], start + length


def der_sequence_body(der):
    tag, body, _ = der_element(der, 0)
    if tag != 0x30:
        raise ValueError("not a sequence")
    return body


def scalar_from_sec1(der):
    body = der_sequence_body(der)
    tag, version, pos = der_element(body, 0)
    if tag != 0x02 or version != b"\x01":
        raise ValueError("not SEC1")
    tag, d_raw, _ = der_element(body, pos)
    if tag != 0x04:
        raise ValueError("not SEC1 scalar")
    d = int.from_bytes(d_raw, "big")
    if not 1 <= d < N:
        raise ValueError("scalar out of range")
    return d


def load_signing_key(path):
    with open(path, "r", encoding="ascii") as handle:
        text = handle.read()
    if "PRIVATE KEY" not in text:
        raise ValueError("not a private key PEM")
    body = "".join(line.strip() for line in text.splitlines() if "-----" not in line)
    der = base64.b64decode(body)
    return scalar_from_sec1(der)


def key_fingerprint(path):
    d = load_signing_key(path)
    q = ec_mul(d, (GX, GY))
    return hashlib.sha256(spki_der(q)).hexdigest()


def parse_native_header(image):
    if len(image) < NATIVE_HEADER_SIZE:
        raise ValueError("image too short")
    magic = int.from_bytes(image[0:4], "little")
    header_size = int.from_bytes(image[4:6], "little")
    format_version = int.from_bytes(image[6:8], "little")
    abi = int.from_bytes(image[8:12], "little")
    target = int.from_bytes(image[12:16], "little")
    image_size = int.from_bytes(image[16:20], "little")
    entry = int.from_bytes(image[20:24], "little")
    crc = int.from_bytes(image[28:32], "little")
    payload = image[NATIVE_HEADER_SIZE:NATIVE_HEADER_SIZE + image_size]
    if magic != NATIVE_MAGIC or header_size != NATIVE_HEADER_SIZE:
        raise ValueError("bad native header")
    if len(payload) != image_size:
        raise ValueError("image_size mismatch")
    if (zlib.crc32(payload) & 0xFFFFFFFF) != crc:
        raise ValueError("payload crc mismatch")
    return {
        "abi": abi,
        "target": target,
        "image_size": image_size,
        "entry": entry,
    }


def split_package(package):
    if package[0:8] != CONTAINER_MAGIC:
        raise ValueError("bad container magic")
    manifest_len = int.from_bytes(package[8:12], "little")
    image_len = int.from_bytes(package[12:16], "little")
    manifest = package[16:16 + manifest_len]
    image = package[16 + manifest_len:16 + manifest_len + image_len]
    sig = package[16 + manifest_len + image_len:]
    if len(manifest) != manifest_len or len(image) != image_len:
        raise ValueError("truncated container")
    return manifest, image, sig


def parse_manifest(manifest):
    if manifest[0:4] != MANIFEST_MAGIC:
        raise ValueError("bad manifest magic")
    pub = manifest[4:20].split(b"\x00")[0].decode("ascii")
    app = manifest[20:52].split(b"\x00")[0].decode("ascii")
    return {
        "publisher_id": pub,
        "app_id": app,
        "version": (
            int.from_bytes(manifest[52:54], "little"),
            int.from_bytes(manifest[54:56], "little"),
            int.from_bytes(manifest[56:58], "little"),
        ),
        "board": int.from_bytes(manifest[60:64], "little"),
        "psram_mib": int.from_bytes(manifest[64:68], "little"),
        "abi": int.from_bytes(manifest[68:72], "little"),
        "caps": int.from_bytes(manifest[72:76], "little"),
        "exec_bytes": int.from_bytes(manifest[76:80], "little"),
        "native_file_len": int.from_bytes(manifest[80:84], "little"),
        "native_image_size": int.from_bytes(manifest[84:88], "little"),
        "entry": int.from_bytes(manifest[88:92], "little"),
        "target": int.from_bytes(manifest[92:96], "little"),
        "image_sha256": manifest[96:128].hex(),
        "spki_sha256": manifest[128:160].hex(),
    }


ANCHOR_SPKI_DER = bytes.fromhex(
    "3059301306072a8648ce3d020106082a8648ce3d030107034200047fcf75cd6569a58f9c2b26c6fa991666aac39d443f7c8febb614eb9d6d8923bceb89a3ec4238a0dd9b383c2bfd94904542a6a8b57032cd4dfb7b4ce5da86ad3f"
)


def anchor_public_point():
    body = der_sequence_body(ANCHOR_SPKI_DER)
    _, _, pos = der_element(body, 0)
    tag, bits, _ = der_element(body, pos)
    if tag != 0x03 or len(bits) != 66 or bits[0] != 0x00 or bits[1] != 0x04:
        raise ValueError("bad anchor SPKI")
    q = (int.from_bytes(bits[2:34], "big"), int.from_bytes(bits[34:66], "big"))
    if hashlib.sha256(ANCHOR_SPKI_DER).hexdigest() != ANCHOR_SPKI_SHA256:
        raise ValueError("anchor SPKI sha mismatch")
    if ec_mul(N, q) is not None:
        raise ValueError("anchor point not on curve")
    return q


def classify(package, anchor_q):
    try:
        manifest, image, sig = split_package(package)
        fields = parse_manifest(manifest)
        native = parse_native_header(image)
    except (ValueError, IndexError):
        return "BAD_PACKAGE", None
    tbs = package[:16 + len(manifest) + len(image)]
    if fields["spki_sha256"] != ANCHOR_SPKI_SHA256:
        return "PUBLISHER_UNTRUSTED", fields
    if not ecdsa_verify(anchor_q, tbs, sig):
        return "SIGNATURE_INVALID", fields
    if fields["board"] != BOARD_ID:
        return "TARGET_INCOMPATIBLE", fields
    if fields["abi"] != ABI_VERSION or native["abi"] != ABI_VERSION:
        return "ABI_INCOMPATIBLE", fields
    return "OK", fields


def build_package(d, image, publisher_id, app_id, version, board=BOARD_ID,
                  abi=ABI_VERSION, caps=CAPS, exec_bytes=None):
    native = parse_native_header(image)
    if exec_bytes is None:
        exec_bytes = native["image_size"]
    q = ec_mul(d, (GX, GY))
    spki = spki_der(q)
    manifest = bytearray(MANIFEST_SIZE)
    manifest[0:4] = MANIFEST_MAGIC
    pub = publisher_id.encode("ascii")
    app = app_id.encode("ascii")
    manifest[4:4 + len(pub)] = pub
    manifest[20:20 + len(app)] = app
    manifest[52:54] = version[0].to_bytes(2, "little")
    manifest[54:56] = version[1].to_bytes(2, "little")
    manifest[56:58] = version[2].to_bytes(2, "little")
    manifest[60:64] = board.to_bytes(4, "little")
    manifest[64:68] = PSRAM_MIB.to_bytes(4, "little")
    manifest[68:72] = abi.to_bytes(4, "little")
    manifest[72:76] = caps.to_bytes(4, "little")
    manifest[76:80] = exec_bytes.to_bytes(4, "little")
    manifest[80:84] = len(image).to_bytes(4, "little")
    manifest[84:88] = native["image_size"].to_bytes(4, "little")
    manifest[88:92] = native["entry"].to_bytes(4, "little")
    manifest[92:96] = native["target"].to_bytes(4, "little")
    manifest[96:128] = hashlib.sha256(image).digest()
    manifest[128:160] = hashlib.sha256(spki).digest()
    tbs = CONTAINER_MAGIC + len(manifest).to_bytes(4, "little")
    tbs += len(image).to_bytes(4, "little") + bytes(manifest) + image
    sig = der_sig_bytes(*ecdsa_sign(d, tbs))
    package = tbs + sig
    if not ecdsa_verify(q, tbs, sig):
        raise ValueError("self verify failed")
    return package
