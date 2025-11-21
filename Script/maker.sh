#!/bin/bash

# STM32 Flasher Script with Multiple Functions
# Usage: ./maker.sh <command> [options]
# Commands:
#   flash <file> [flash_addr]        - Flash file to device via SWD (ST-LINK)
#   sign <bin_file>                   - Sign binary file for secure boot
#   hex <bin_file> <hex_addr>         - Convert binary to hex file with specified address

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to show usage
show_usage() {
    echo "STM32 Flasher Script - Multiple Functions"
    echo ""
    echo "Usage: $0 <command> [options]"
    echo ""
    echo "Commands:"
    echo "  flash <file> [flash_addr]        Flash via ST-LINK (SWD) with external loader"
    echo "  sign <bin_file>                   Sign binary file for secure boot"
    echo "  hex <bin_file> <hex_addr>         Convert binary to hex file with address"
    echo ""
    echo "Examples:"
    echo "  $0 flash firmware.bin 0x70100000    # Flash binary to QSPI flash"
    echo "  $0 flash firmware.hex               # Flash HEX file (address embedded)"
    echo "  $0 sign firmware.bin                # Sign binary file"
    echo "  $0 hex firmware.bin 0x70100000      # Convert binary to hex with address"
    echo ""
    echo "Environment Variables:"
    echo "  STM32_PROGRAMMER_CLI    Path to STM32_Programmer_CLI"
    echo "  STM32_SIGNING_TOOL      Path to STM32_SigningTool_CLI"
    echo "  OBJCOPY                 Path to arm-none-eabi-objcopy"
}

# Function to check dependencies
check_dependencies() {
    # Check STM32_Programmer_CLI
    FLASHER="${STM32_PROGRAMMER_CLI:-STM32_Programmer_CLI}"
    if ! command -v "$FLASHER" &> /dev/null; then
        print_error "STM32_Programmer_CLI not found!"
        print_info "Please install STM32CubeProgrammer or set STM32_PROGRAMMER_CLI environment variable"
        return 1
    fi

    # Check STM32_SigningTool_CLI
    SIGNER="${STM32_SIGNING_TOOL:-STM32_SigningTool_CLI}"
    if ! command -v "$SIGNER" &> /dev/null; then
        print_error "STM32_SigningTool_CLI not found!"
        print_info "Please install STM32CubeProgrammer or set STM32_SIGNING_TOOL environment variable"
        return 1
    fi

    # Check objcopy
    OBJCOPY_TOOL="${OBJCOPY:-arm-none-eabi-objcopy}"
    if ! command -v "$OBJCOPY_TOOL" &> /dev/null; then
        print_error "arm-none-eabi-objcopy not found!"
        print_info "Please install ARM GCC toolchain or set OBJCOPY environment variable"
        return 1
    fi

    return 0
}

# Function to find external loader
find_external_loader() {
    EL_FILE="MX66UW1G45G_STM32N6570-DK.stldr"
    
    # Try to find the external loader automatically
    EL_DIR="$(dirname "$(which $FLASHER)")/ExternalLoader"
    
    if [ -f "$EL_DIR/$EL_FILE" ]; then
        echo "$EL_DIR/$EL_FILE"
    elif [ -f "./ExternalLoader/$EL_FILE" ]; then
        echo "./ExternalLoader/$EL_FILE"
    elif [ -f "../../ExternalLoader/$EL_FILE" ]; then
        echo "../../ExternalLoader/$EL_FILE"
    else
        echo ""
    fi
}



# Function to flash file (binary or hex)
flash_file() {
    local file_path="$1"
    local flash_addr="$2"
    
    print_info "Starting flash operation..."
    print_info "File: $file_path"
    
    # Check if file exists
    if [ ! -f "$file_path" ]; then
        print_error "File '$file_path' not found!"
        return 1
    fi
    
    # Determine file type and handle accordingly
    local file_ext="${file_path##*.}"
    local file_name=$(basename "$file_path")
    
    
    if [ "$file_ext" = "hex" ]; then
        print_info "Detected HEX file: $file_name"
        print_info "HEX files contain embedded address information - tool will auto-detect"
        
        # For HEX files, we don't need to specify address in the flash command
        # as the address is embedded in the HEX file
        # FLASH_CMD will be built after external loader check
        
    elif [ "$file_ext" = "bin" ]; then
        print_info "Detected BIN file: $file_name"
        
        # Binary files require explicit address
        if [ -z "$flash_addr" ]; then
            print_error "Binary file requires explicit flash address"
            print_info "Usage: $0 flash $file_path <address>"
            return 1
        fi
        
        # Check if flash address is valid (should start with 0x)
        if [[ ! "$flash_addr" =~ ^0x[0-9a-fA-F]+$ ]]; then
            print_error "Invalid flash address format. Must be hexadecimal starting with 0x"
            print_info "Example: 0x70100000"
            return 1
        fi
        
        # FLASH_CMD will be built after external loader check
        
    else
        print_error "Unsupported file type: $file_ext"
        print_info "Supported types: .bin, .hex"
        return 1
    fi
    
    # Find external loader
    local el=$(find_external_loader)
    if [ -n "$el" ]; then
        print_info "External loader found: $el"
        # Build command with external loader
        if [ "$file_ext" = "hex" ]; then
            FLASH_CMD="$FLASHER -c port=SWD mode=HOTPLUG -el $el -hardRst -w $file_path"
        else
            FLASH_CMD="$FLASHER -c port=SWD mode=HOTPLUG -el $el -hardRst -w $file_path $flash_addr"
        fi
    else
        print_warning "External loader not found, proceeding without it"
        # Build command without external loader
        if [ "$file_ext" = "hex" ]; then
            FLASH_CMD="$FLASHER -c port=SWD mode=HOTPLUG -hardRst -w $file_path"
        else
            FLASH_CMD="$FLASHER -c port=SWD mode=HOTPLUG -hardRst -w $file_path $flash_addr"
        fi
    fi
    
    print_info "Executing: $FLASH_CMD"
    echo ""
    
    # Execute the flash command
    local flash_result=0
    if $FLASH_CMD; then
        print_success "Flash operation completed successfully!"
        if [ "$file_ext" = "hex" ]; then
            print_info "HEX file '$file_name' has been written to device"
        else
            print_info "Binary file '$file_name' has been written to address $flash_addr"
        fi
        flash_result=0
    else
        print_error "Flash operation failed!"
        flash_result=1
    fi
    
    
    return $flash_result
}

# Function to flash via USB DFU (no external loader)
flash_file_usb() {
    local file_path="$1"
    local flash_addr="$2"

    print_info "Starting USB DFU flash..."
    print_info "File: $file_path"

    if [ ! -f "$file_path" ]; then
        print_error "File '$file_path' not found!"
        return 1
    fi

    local file_ext="${file_path##*.}"
    local file_name=$(basename "$file_path")

    if [ "$file_ext" = "hex" ]; then
        print_info "Detected HEX file: $file_name"
        FLASH_CMD="$FLASHER -c port=USB1 mode=HOTPLUG -hardRst -w $file_path"
    elif [ "$file_ext" = "bin" ]; then
        print_info "Detected BIN file: $file_name"
        if [ -z "$flash_addr" ]; then
            print_error "Binary file requires explicit flash address in USB mode"
            print_info "Usage: $0 flash-usb $file_path <address>"
            return 1
        fi
        if [[ ! "$flash_addr" =~ ^0x[0-9a-fA-F]+$ ]]; then
            print_error "Invalid flash address format. Must be hexadecimal starting with 0x"
            print_info "Example: 0x08000000"
            return 1
        fi
        # For USB DFU, only allow internal Flash addresses (0x08xxxxxx). Reject external XSPI (0x7xxxxxxx)
        if [[ "$flash_addr" =~ ^0x7[0-9a-fA-F]+$ ]]; then
            print_error "USB DFU does not support external QSPI address $flash_addr, please use SWD and load external loader"
            print_info "Example: $0 flash $file_path 0x70100000"
            return 1
        fi
        if [[ ! "$flash_addr" =~ ^0x08[0-9a-fA-F]+$ ]]; then
            print_warning "Address $flash_addr does not look like internal Flash (0x08xxxxxx), USB mode may fail"
        fi
        FLASH_CMD="$FLASHER -c port=USB1 mode=HOTPLUG -hardRst -w $file_path $flash_addr"
    else
        print_error "Unsupported file type: $file_ext"
        print_info "Supported types: .bin, .hex"
        return 1
    fi

    print_info "Executing: $FLASH_CMD"
    echo ""
    if $FLASH_CMD; then
        print_success "USB DFU flash completed!"
        return 0
    else
        print_error "USB DFU flash failed!"
        return 1
    fi
}

# Function to sign binary file
sign_binary() {
    local bin_file="$1"
    
    print_info "Starting signing operation..."
    print_info "Binary file: $bin_file"
    
    # Check if binary file exists
    if [ ! -f "$bin_file" ]; then
        print_error "Binary file '$bin_file' not found!"
        return 1
    fi
    
    # Generate output filename
    local base_name=$(basename "$bin_file" .bin)
    local signed_file="${base_name}_signed.bin"
    
    print_info "Output signed file: $signed_file"
    
    # Build signing command (based on Makefile)
    SIGN_CMD="$SIGNER -s -bin $bin_file -nk -t ssbl -hv 2.3 -o $signed_file"
    
    print_info "Executing: $SIGN_CMD"
    echo ""
    
    # Execute the signing command
    if $SIGN_CMD; then
        print_success "Signing operation completed successfully!"
        print_info "Signed file created: $signed_file"
        return 0
    else
        print_error "Signing operation failed!"
        return 1
    fi
}

# Function to convert binary to hex
convert_to_hex() {
    local bin_file="$1"
    local hex_addr="$2"
    
    print_info "Starting hex conversion..."
    print_info "Binary file: $bin_file"
    print_info "Hex address: $hex_addr"
    
    # Check if binary file exists
    if [ ! -f "$bin_file" ]; then
        print_error "Binary file '$bin_file' not found!"
        return 1
    fi
    
    # Check if hex address is valid (should start with 0x)
    if [[ ! "$hex_addr" =~ ^0x[0-9a-fA-F]+$ ]]; then
        print_error "Invalid hex address format. Must be hexadecimal starting with 0x"
        print_info "Example: 0x70100000"
        return 1
    fi
    
    # Generate output filename
    local base_name=$(basename "$bin_file" .bin)
    local hex_file="${base_name}.hex"
    
    print_info "Output hex file: $hex_file"
    
    # Build objcopy command to convert binary to hex
    # Using Intel HEX format (-i) with 32-bit addresses (-I)
    HEX_CMD="$OBJCOPY_TOOL -I binary -O ihex --change-addresses $hex_addr $bin_file $hex_file"
    
    print_info "Executing: $HEX_CMD"
    echo ""
    
    # Execute the conversion command
    if $HEX_CMD; then
        print_success "Hex conversion completed successfully!"
        print_info "Hex file created: $hex_file"
        return 0
    else
        print_error "Hex conversion failed!"
        return 1
    fi
}

# Main script logic
main() {
    # Check if command is provided
    if [ $# -eq 0 ]; then
        show_usage
        exit 1
    fi
    
    local command="$1"
    
    case "$command" in
        "flash")
            if [ $# -lt 2 ] || [ $# -gt 3 ]; then
                print_error "Flash command requires 1 or 2 arguments: <file> [flash_addr]"
                print_info "Examples:"
                print_info "  $0 flash firmware.bin 0x70100000    # Binary with address"
                print_info "  $0 flash firmware.hex               # HEX file (tool auto-detects address)"
                print_info "  $0 flash model.bin 0x70200000       # BIN file (model package binary)"
                exit 1
            fi
            
            # Check dependencies
            if ! check_dependencies; then
                exit 1
            fi
            
            # Execute flash
            if flash_file "$2" "$3"; then
                exit 0
            else
                exit 1
            fi
            ;;

        "flash-usb")
            if [ $# -lt 2 ] || [ $# -gt 3 ]; then
                print_error "flash-usb requires 1 or 2 parameters: <file> [flash_addr]"
                print_info "Examples:"
                print_info "  $0 flash-usb app.hex                # HEX (internal Flash)"
                print_info "  $0 flash-usb app.bin 0x08000000     # BIN to internal Flash"
                exit 1
            fi
            if ! check_dependencies; then
                exit 1
            fi
            if flash_file_usb "$2" "$3"; then
                exit 0
            else
                exit 1
            fi
            ;;
            
        "sign")
            if [ $# -ne 2 ]; then
                print_error "Sign command requires exactly 1 argument: <bin_file>"
                print_info "Example: $0 sign firmware.bin"
                exit 1
            fi
            
            # Check dependencies
            if ! check_dependencies; then
                exit 1
            fi
            
            # Execute signing
            if sign_binary "$2"; then
                exit 0
            else
                exit 1
            fi
            ;;
            
        "hex")
            if [ $# -ne 3 ]; then
                print_error "Hex command requires exactly 2 arguments: <bin_file> <hex_addr>"
                print_info "Example: $0 hex firmware.bin 0x70100000"
                exit 1
            fi
            
            # Check dependencies
            if ! check_dependencies; then
                exit 1
            fi
            
            # Execute hex conversion
            if convert_to_hex "$2" "$3"; then
                exit 0
            else
                exit 1
            fi
            ;;
            
        "help"|"-h"|"--help")
            show_usage
            exit 0
            ;;
            
        *)
            print_error "Unknown command: $command"
            echo ""
            show_usage
            exit 1
            ;;
    esac
}

# Execute main function with all arguments
main "$@"
