#!/bin/bash
# OTA firmware package batch creation script

echo "=== OTA Firmware Package Batch Creation Tool ==="

# Parse command line arguments
BIN_DIR="../bin"  # Default bin directory

if [ $# -gt 0 ]; then
    BIN_DIR="$1"
fi

echo "Bin directory: $BIN_DIR"

# Check if bin directory exists
if [ ! -d "$BIN_DIR" ]; then
    echo "Error: Bin directory does not exist: $BIN_DIR"
    echo "Usage: $0 [bin_directory]"
    echo "Example: $0 ../bin"
    exit 1
fi

echo "Starting to process all firmware files in $BIN_DIR directory..."

# Check if Python script exists
if [ ! -f "ota_packer.py" ]; then
    echo "Error: ota_packer.py script not found in current directory"
    echo "Please run this script from the Script directory"
    exit 1
fi

# Create output directory in bin directory
OUTPUT_DIR="$BIN_DIR/output_ota_packages"
mkdir -p "$OUTPUT_DIR"
echo "Output directory: $OUTPUT_DIR"

# ==================== Version Configuration ====================
# Configure independent version for each firmware type
# Format: major.minor.patch.build
FSBL_VERSION="1.0.0.1"
APP_VERSION="1.0.0.1"
WEB_VERSION="1.0.0.1"
AI_MODEL_VERSION="1.0.0.1"
# ==============================================================

# Process FSBL firmware
FSBL_FILE="$BIN_DIR/ne301_FSBL_signed.bin"
if [ -f "$FSBL_FILE" ]; then
    echo "Processing FSBL firmware..."
    python ota_packer.py "$FSBL_FILE" \
        -o "$OUTPUT_DIR/ne301_FSBL_v${FSBL_VERSION}_ota.bin" \
        -n "NE301_FSBL" \
        -d "NE301 First Stage Boot Loader" \
        -t fsbl \
        -v "${FSBL_VERSION}"
else
    echo "Skipping FSBL: $FSBL_FILE not found"
fi

# Process APP firmware
APP_FILE="$BIN_DIR/ne301_Appli_signed.bin"
if [ -f "$APP_FILE" ]; then
    echo "Processing APP firmware..."
    python ota_packer.py "$APP_FILE" \
        -o "$OUTPUT_DIR/ne301_Appli_v${APP_VERSION}_ota.bin" \
        -n "NE301_APP" \
        -d "NE301 Main Application" \
        -t app \
        -v "${APP_VERSION}"
else
    echo "Skipping APP: $APP_FILE not found"
fi

# Process Web assets
WEB_FILE="$BIN_DIR/web-assets.bin"
if [ -f "$WEB_FILE" ]; then
    echo "Processing Web assets..."
    python ota_packer.py "$WEB_FILE" \
        -o "$OUTPUT_DIR/web-assets_v${WEB_VERSION}_ota.bin" \
        -n "NE301_WEB" \
        -d "NE301 Web Assets" \
        -t web \
        -v "${WEB_VERSION}"
else
    echo "Skipping Web assets: $WEB_FILE not found"
fi

# Process AI model files
MODEL_COUNT=0
for model_file in "$BIN_DIR"/model_*.bin; do
    if [ -f "$model_file" ]; then
        model_basename=$(basename "$model_file")
        echo "Processing AI model: $model_basename"
        output_name="$OUTPUT_DIR/${model_basename%.bin}_v${AI_MODEL_VERSION}_ota.bin"
        model_name=$(echo "$model_basename" | sed 's/\.bin$//' | tr '[:lower:]' '[:upper:]')
        
        python ota_packer.py "$model_file" \
            -o "$output_name" \
            -n "$model_name" \
            -d "AI Model for NE301" \
            -t ai_model \
            -v "${AI_MODEL_VERSION}"
        
        ((MODEL_COUNT++))
    fi
done

if [ $MODEL_COUNT -eq 0 ]; then
    echo "No AI model files found in $BIN_DIR"
fi

echo ""
echo "=== Processing Complete ==="
echo "Generated OTA firmware packages:"
ls -la "$OUTPUT_DIR/" 2>/dev/null || echo "No files generated"

echo ""
echo "=== File Size Statistics ==="
echo "Original files in $BIN_DIR:"
ls -lh "$BIN_DIR"/*.bin 2>/dev/null | grep -v "_ota.bin" || echo "No bin files found"

echo ""
echo "OTA firmware packages (1K header + original file):"
ls -lh "$OUTPUT_DIR/" 2>/dev/null || echo "No OTA packages generated"

echo ""
echo "=== Summary ==="
FSBL_COUNT=$(find "$OUTPUT_DIR" -name "*FSBL*_ota.bin" 2>/dev/null | wc -l)
APP_COUNT=$(find "$OUTPUT_DIR" -name "*Appli*_ota.bin" 2>/dev/null | wc -l)
WEB_COUNT=$(find "$OUTPUT_DIR" -name "*web-assets*_ota.bin" 2>/dev/null | wc -l)
MODEL_COUNT=$(find "$OUTPUT_DIR" -name "model_*_ota.bin" 2>/dev/null | wc -l)
TOTAL_COUNT=$((FSBL_COUNT + APP_COUNT + WEB_COUNT + MODEL_COUNT))

echo "  FSBL packages:  $FSBL_COUNT"
echo "  APP packages:   $APP_COUNT"
echo "  Web packages:   $WEB_COUNT"
echo "  Model packages: $MODEL_COUNT"
echo "  Total:          $TOTAL_COUNT"