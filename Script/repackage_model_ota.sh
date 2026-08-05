#!/bin/bash
# Repackage per-model NE301 OTA packages with correct OTA headers.
#
# Problem: quantize.py's stage3 (`make model` + `make pkg-model`) always writes
# the OTA package to the SAME path (build/ne301_Model_v4.4.0.0_pkg.bin). When
# several models are quantized in sequence, each build overwrites the previous
# pkg, so only the LAST model keeps its OTA package. Worse, the DB stored the
# RAW model bin (N6M1 header) instead of the OTA-wrapped pkg (OTAU header),
# which the device rejects with "OTA Firmware Header Validation Failed".
#
# This script rebuilds stage3 for each model and immediately copies its OTA pkg
# to a per-model unique filename so it can't be overwritten.
#
# Usage: ./Script/repackage_model_ota.sh [model_slug ...]
#   (defaults to the 3 models already quantized: helmetguard pothole visdrone)
#
# Output: build/model_ota_backup/<slug>_v4.4.0.0_pkg.bin  (OTAU header)

set -euo pipefail
cd "$(dirname "$0")/.."   # repo root

MODELS=("$@")
if [ ${#MODELS[@]} -eq 0 ]; then
    MODELS=(helmetguard-detection-yolov11n pothole-segmentation-yolov8n visdrone-detection-yolov8n)
fi

STEDGEAI_VARIANT=4.0
MODEL_VERSION_OVERRIDE=4.0.0
MODEL_VERSION="4.4.0.0"          # STEDGEAI_BIT(4).MODEL_VERSION_OVERRIDE
IMG=camthink/ne301-dev:v4.0
OUT=build/model_ota_backup
mkdir -p "$OUT"

run_docker() { # command...
    docker run --rm -v "$(pwd)":/workspace -w /workspace "$IMG" bash -lc \
        "source /etc/profile.d/stedgeai.sh 2>/dev/null; $*"
}

for slug in "${MODELS[@]}"; do
    echo "=================================================="
    echo "[$slug] stage3 build + OTA package (STEDGEAI_VARIANT=$STEDGEAI_VARIANT)"
    # 1) clear previous stedgeai artifacts so `make model` actually re-runs
    rm -rf Model/build Model/st_ai_bin Model/st_ai_c
    # 2) build + package inside the container
    run_docker "rm -rf Model/build Model/st_ai_bin Model/st_ai_c && \
        make model MODEL_NAME=$slug STEDGEAI_VARIANT=$STEDGEAI_VARIANT && \
        make pkg-model STEDGEAI_VARIANT=$STEDGEAI_VARIANT MODEL_VERSION_OVERRIDE=$MODEL_VERSION_OVERRIDE"
    # 3) copy the OTA pkg to a unique per-model name BEFORE the next build
    src="build/ne301_Model_v${MODEL_VERSION}_pkg.bin"
    dst="$OUT/${slug}_v${MODEL_VERSION}_pkg.bin"
    if [ ! -f "$src" ]; then
        echo "ERROR [$slug]: $src missing after build" >&2; exit 1
    fi
    cp "$src" "$dst"
    # 4) verify OTA magic ("UATO" = 0x4F544155 little-endian) at offset 0
    magic=$(xxd -p -l4 "$dst")
    if [ "$magic" != "5541544f" ]; then
        echo "ERROR [$slug]: bad magic 0x$magic (expected 5541544f 'UATO')" >&2; exit 1
    fi
    echo "[$slug] OK → $dst ($(stat -f%z "$dst") bytes, OTA magic verified)"
done

echo "=================================================="
echo "Done. Packages saved under $OUT/:"
ls -la "$OUT" | grep -E "pkg.bin"
