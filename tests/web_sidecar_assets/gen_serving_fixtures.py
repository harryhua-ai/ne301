import argparse
import importlib.util
import os

SOURCE = os.path.join(os.path.dirname(__file__), "..", "app_sidecar", "gen_sidecar_fixtures.py")
SPEC = importlib.util.spec_from_file_location("sidecar_fixture_source", SOURCE)
FIXTURES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FIXTURES)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    os.makedirs(args.output, exist_ok=True)
    key = FIXTURES.derive_key(FIXTURES.ANCHOR_SEED)
    files = FIXTURES.make_files(FIXTURES.SMALL_FILES)
    bundle = FIXTURES.build_bundle(key, files, version=(1, 4, 2))
    with open(os.path.join(args.output, "serving_small.bin"), "wb") as stream:
        stream.write(bundle)
    upgraded = FIXTURES.build_bundle(key, files, version=(1, 5, 0))
    with open(os.path.join(args.output, "serving_upgrade.bin"), "wb") as stream:
        stream.write(upgraded)
    print("serving fixtures written to", args.output)


if __name__ == "__main__":
    main()
