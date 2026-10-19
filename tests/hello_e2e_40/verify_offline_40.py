import hashlib
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import neapp_pkg_40 as np

HERE = os.path.dirname(os.path.abspath(__file__))
PKG_DIR = os.path.join(HERE, "packages")

POSITIVES = ("hello-app_v1.0.0.neapp", "hello-app_v2.0.0.neapp")

NEGATIVES = {
    "hello-app_v1.0.0_badsig.neapp": "SIGNATURE_INVALID",
    "hello-app_v9.9.9_foreign.neapp": "PUBLISHER_UNTRUSTED",
    "hello-app_v1.1.0_badboard.neapp": "TARGET_INCOMPATIBLE",
    "hello-app_v1.2.0_badabi.neapp": "ABI_INCOMPATIBLE",
}

EXPECT_VERSION = {
    "hello-app_v1.0.0.neapp": (1, 0, 0),
    "hello-app_v2.0.0.neapp": (2, 0, 0),
}


def read_package(name):
    with open(os.path.join(PKG_DIR, name), "rb") as handle:
        return handle.read()


def main():
    failures = 0
    total = 0

    def check(cond, label):
        nonlocal failures, total
        total += 1
        if not cond:
            failures += 1
            print("FAIL %s" % label)

    anchor_q = np.anchor_public_point()
    check(np.ANCHOR_SPKI_SHA256 == "06e732b1b94daeeef433297f7a8e797bf72b2fd3b882a4ec6a5490352931ebda",
          "anchor constant pin")

    for name in POSITIVES:
        package = read_package(name)
        got, fields = np.classify(package, anchor_q)
        check(got == "OK", "%s class=%s" % (name, got))
        if got != "OK":
            continue
        check(fields["publisher_id"] == np.PUBLISHER_ID, "%s publisher" % name)
        check(fields["app_id"] == np.APP_ID, "%s app_id" % name)
        check(fields["version"] == EXPECT_VERSION[name], "%s version" % name)
        check(fields["board"] == np.BOARD_ID, "%s board" % name)
        check(fields["psram_mib"] == np.PSRAM_MIB, "%s psram" % name)
        check(fields["abi"] == np.ABI_VERSION, "%s abi" % name)
        check(fields["target"] == np.EXEC_BASE, "%s target" % name)
        manifest, image, sig = np.split_package(package)
        native = np.parse_native_header(image)
        check(fields["image_sha256"] == hashlib.sha256(image).hexdigest(),
              "%s image sha" % name)
        check(fields["native_image_size"] == native["image_size"],
              "%s image size" % name)
        sidecar = name + ".provenance.json"
        path = os.path.join(PKG_DIR, sidecar)
        if os.path.exists(path):
            with open(path, "r", encoding="utf-8") as handle:
                provenance = json.load(handle)
            check(provenance["package_sha256"] == hashlib.sha256(package).hexdigest(),
                  "%s provenance sha" % name)
            check(provenance["tbs_sha256"] == hashlib.sha256(
                package[:16 + len(manifest) + len(image)]).hexdigest(),
                "%s provenance tbs" % name)
            check(provenance["signer_spki_sha256"] == np.ANCHOR_SPKI_SHA256,
                  "%s provenance signer" % name)
            blob = json.dumps(provenance)
            check("PRIVATE KEY" not in blob and "BEGIN" not in blob,
                  "%s provenance carries no key material" % name)
        print("PASS %s sha256=%s tbs=%s v%d.%d.%d" % (
            name, hashlib.sha256(package).hexdigest(),
            hashlib.sha256(package[:16 + len(manifest) + len(image)]).hexdigest(),
            fields["version"][0], fields["version"][1], fields["version"][2]))

    for name, want in NEGATIVES.items():
        package = read_package(name)
        got, _ = np.classify(package, anchor_q)
        check(got == want, "%s expect=%s got=%s" % (name, want, got))
        print("PASS %s reject=%s sha256=%s" % (name, got, hashlib.sha256(package).hexdigest()))

    v1 = read_package("hello-app_v1.0.0.neapp")
    v2 = read_package("hello-app_v2.0.0.neapp")
    _, v1_image, _ = np.split_package(v1)
    _, v2_image, _ = np.split_package(v2)
    check(v1_image != v2_image, "v1 and v2 images differ")
    print("image v1 sha256=%s" % hashlib.sha256(v1_image).hexdigest())
    print("image v2 sha256=%s" % hashlib.sha256(v2_image).hexdigest())

    print("verify_offline_40: %d checks, %d failures" % (total, failures))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
