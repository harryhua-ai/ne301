import argparse
import hashlib
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "app_admission"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from anchor_keygen import load_signing_key, ec_mul, GX, GY, spki_der, spki_sha256
import neapp_pkg_40 as np

PKG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "packages")


def read_package(name):
    with open(os.path.join(PKG_DIR, name), "rb") as handle:
        return handle.read()


def write_package(name, data):
    path = os.path.join(PKG_DIR, name)
    with open(path, "wb") as handle:
        handle.write(data)
    return path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--issuer-key", default=os.path.expanduser(
        "~/.ne301-keys/ne301-app-issuer-v2.sec1.pem"))
    parser.add_argument("--foreign-key", default=os.path.expanduser(
        "~/.ne301-keys/ne301-fixture-reject-foreign.sec1.pem"))
    args = parser.parse_args()

    issuer_fp = spki_sha256(ec_mul(load_signing_key(args.issuer_key), (GX, GY)))
    if issuer_fp.hex() != np.ANCHOR_SPKI_SHA256:
        raise SystemExit("issuer key does not pair with the host anchor")
    foreign_d = load_signing_key(args.foreign_key)
    foreign_q = ec_mul(foreign_d, (GX, GY))
    foreign_fp = spki_sha256(foreign_q)
    if foreign_fp.hex() == np.ANCHOR_SPKI_SHA256:
        raise SystemExit("foreign key must not pair with the host anchor")

    v1 = read_package("hello-app_v1.0.0.neapp")
    v2 = read_package("hello-app_v2.0.0.neapp")
    _, v1_image, _ = np.split_package(v1)
    _, v2_image, _ = np.split_package(v2)

    anchor_q = np.anchor_public_point()
    if not np.ecdsa_verify(anchor_q, v1[:16 + 160 + int.from_bytes(v1[12:16], "little")],
                           np.split_package(v1)[2]):
        raise SystemExit("committed v1 package does not verify against the anchor")
    if not np.ecdsa_verify(anchor_q, v2[:16 + 160 + int.from_bytes(v2[12:16], "little")],
                           np.split_package(v2)[2]):
        raise SystemExit("committed v2 package does not verify against the anchor")

    badsig = bytearray(v1)
    badsig[-1] ^= 0x41
    write_package("hello-app_v1.0.0_badsig.neapp", bytes(badsig))

    foreign = np.build_package(foreign_d, v2_image, np.PUBLISHER_ID, np.APP_ID,
                               (9, 9, 9))
    write_package("hello-app_v9.9.9_foreign.neapp", foreign)

    badboard = np.build_package(load_signing_key(args.issuer_key), v1_image,
                                np.PUBLISHER_ID, np.APP_ID, (1, 1, 0),
                                board=0x00003110)
    write_package("hello-app_v1.1.0_badboard.neapp", badboard)

    badabi_image = bytearray(v1_image)
    badabi_image[8:12] = (np.ABI_VERSION + 1).to_bytes(4, "little")
    badabi = np.build_package(load_signing_key(args.issuer_key), bytes(badabi_image),
                              np.PUBLISHER_ID, np.APP_ID, (1, 2, 0),
                              abi=np.ABI_VERSION + 1)
    write_package("hello-app_v1.2.0_badabi.neapp", badabi)

    conflict = np.build_package(load_signing_key(args.issuer_key), v1_image,
                                np.PUBLISHER_ID, np.APP_ID, (2, 0, 0))
    write_package("hello-app_v2.0.0_altcontent.neapp", conflict)

    anchor_q = np.anchor_public_point()
    expectations = {
        "hello-app_v1.0.0.neapp": "OK",
        "hello-app_v2.0.0.neapp": "OK",
        "hello-app_v1.0.0_badsig.neapp": "SIGNATURE_INVALID",
        "hello-app_v9.9.9_foreign.neapp": "PUBLISHER_UNTRUSTED",
        "hello-app_v1.1.0_badboard.neapp": "TARGET_INCOMPATIBLE",
        "hello-app_v1.2.0_badabi.neapp": "ABI_INCOMPATIBLE",
        "hello-app_v2.0.0_altcontent.neapp": "OK",
    }
    failures = 0
    for name, want in expectations.items():
        got, fields = np.classify(read_package(name), anchor_q)
        status = "PASS" if got == want else "FAIL"
        if got != want:
            failures += 1
        digest = hashlib.sha256(read_package(name)).hexdigest()
        print("%s %s expect=%s got=%s sha256=%s" % (status, name, want, got, digest))
    if failures:
        raise SystemExit("gen self-check failures: %d" % failures)
    print("gen_vectors_40: all vector expectations hold")


if __name__ == "__main__":
    main()
