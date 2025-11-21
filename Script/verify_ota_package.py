#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OTA Package Verification Tool
Verifies OTA package header structure and displays information
"""

import sys
import struct
import zlib
import hashlib
from datetime import datetime
import io

# Fix encoding for Windows console
if sys.platform == 'win32':
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')

OTA_HEADER_SIZE = 1024
OTA_MAGIC_NUMBER = 0x4F544155  # "OTAU"

FW_TYPE_NAMES = {
    0x00: "Unknown",
    0x01: "FSBL",
    0x02: "Application",
    0x03: "Web Assets",
    0x04: "AI Model",
    0x05: "Configuration",
    0x06: "Patch",
    0x07: "Full Package",
}

def verify_ota_package(filename):
    """Verify and display OTA package information"""
    try:
        with open(filename, 'rb') as f:
            header = f.read(OTA_HEADER_SIZE)
            firmware = f.read()
        
        if len(header) < OTA_HEADER_SIZE:
            print(f"Error: File too small (header: {len(header)} < {OTA_HEADER_SIZE} bytes)")
            return False
        
        # Parse header
        offset = 0
        
        # Basic Information
        magic = struct.unpack_from('<I', header, offset)[0]
        offset += 4
        header_version = struct.unpack_from('<H', header, offset)[0]
        offset += 2
        header_size = struct.unpack_from('<H', header, offset)[0]
        offset += 2
        header_crc32 = struct.unpack_from('<I', header, offset)[0]
        offset += 4
        fw_type = struct.unpack_from('<B', header, offset)[0]
        offset += 1
        encrypt_type = struct.unpack_from('<B', header, offset)[0]
        offset += 1
        compress_type = struct.unpack_from('<B', header, offset)[0]
        offset += 1
        reserved1 = struct.unpack_from('<B', header, offset)[0]
        offset += 1
        timestamp = struct.unpack_from('<I', header, offset)[0]
        offset += 4
        sequence = struct.unpack_from('<I', header, offset)[0]
        offset += 4
        total_package_size = struct.unpack_from('<I', header, offset)[0]
        offset += 4
        offset += 33  # reserved2
        
        # Firmware Information (starting at 0x40)
        fw_name = header[0x40:0x60].rstrip(b'\x00').decode('utf-8', errors='ignore')
        fw_desc = header[0x60:0xA0].rstrip(b'\x00').decode('utf-8', errors='ignore')
        
        # Version info at 0xA0
        fw_ver = list(struct.unpack_from('<8B', header, 0xA0))
        min_ver = list(struct.unpack_from('<8B', header, 0xA8))
        
        fw_size = struct.unpack_from('<I', header, 0xB0)[0]
        fw_size_compressed = struct.unpack_from('<I', header, 0xB4)[0]
        fw_crc32_stored = struct.unpack_from('<I', header, 0xB8)[0]
        fw_hash_stored = header[0xBC:0xDC]
        
        # Verify magic number
        print("=" * 70)
        print("OTA Package Verification")
        print("=" * 70)
        
        if magic != OTA_MAGIC_NUMBER:
            print(f"[FAIL] Invalid magic number: 0x{magic:08X} (expected: 0x{OTA_MAGIC_NUMBER:08X})")
            return False
        print(f"[PASS] Magic number: 0x{magic:08X} (OTAU)")
        
        # Verify header size
        if header_size != OTA_HEADER_SIZE:
            print(f"[FAIL] Invalid header size: {header_size} (expected: {OTA_HEADER_SIZE})")
            return False
        print(f"[PASS] Header size: {header_size} bytes")
        
        # Verify header CRC32
        header_for_crc = bytearray(header)
        struct.pack_into('<I', header_for_crc, 8, 0)
        calculated_header_crc = zlib.crc32(bytes(header_for_crc)) & 0xFFFFFFFF
        if calculated_header_crc != header_crc32:
            print(f"[FAIL] Header CRC32 mismatch: 0x{header_crc32:08X} (calculated: 0x{calculated_header_crc:08X})")
            return False
        print(f"[PASS] Header CRC32: 0x{header_crc32:08X}")
        
        # Verify firmware CRC32
        calculated_fw_crc = zlib.crc32(firmware) & 0xFFFFFFFF
        if calculated_fw_crc != fw_crc32_stored:
            print(f"[FAIL] Firmware CRC32 mismatch: 0x{fw_crc32_stored:08X} (calculated: 0x{calculated_fw_crc:08X})")
            return False
        print(f"[PASS] Firmware CRC32: 0x{fw_crc32_stored:08X}")
        
        # Verify firmware SHA256
        calculated_fw_hash = hashlib.sha256(firmware).digest()
        if calculated_fw_hash != fw_hash_stored:
            print(f"[FAIL] Firmware SHA256 mismatch")
            print(f"  Stored:     {fw_hash_stored.hex()}")
            print(f"  Calculated: {calculated_fw_hash.hex()}")
            return False
        print(f"[PASS] Firmware SHA256: {calculated_fw_hash.hex()[:32]}...")
        
        # Display information
        print("\n" + "=" * 70)
        print("Package Information")
        print("=" * 70)
        print(f"Firmware Type:        {FW_TYPE_NAMES.get(fw_type, f'Unknown (0x{fw_type:02X})')}")
        print(f"Firmware Name:        {fw_name}")
        print(f"Description:          {fw_desc}")
        print(f"Version:              {fw_ver[0]}.{fw_ver[1]}.{fw_ver[2]}.{fw_ver[3]}")
        print(f"Min Compatible Ver:   {min_ver[0]}.{min_ver[1]}.{min_ver[2]}.{min_ver[3]}")
        print(f"Timestamp:            {datetime.fromtimestamp(timestamp).strftime('%Y-%m-%d %H:%M:%S')}")
        print(f"Sequence:             {sequence}")
        print(f"Firmware Size:        {fw_size:,} bytes")
        print(f"Total Package Size:   {total_package_size:,} bytes")
        print(f"Encryption:           {'None' if encrypt_type == 0 else f'Type {encrypt_type}'}")
        print(f"Compression:          {'None' if compress_type == 0 else f'Type {compress_type}'}")
        
        print("\n" + "=" * 70)
        print("[SUCCESS] OTA Package Verification PASSED")
        print("=" * 70)
        return True
        
    except FileNotFoundError:
        print(f"Error: File not found: {filename}")
        return False
    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()
        return False

def main():
    if len(sys.argv) < 2:
        print("Usage: python verify_ota_package.py <ota_package_file.bin>")
        return 1
    
    filename = sys.argv[1]
    success = verify_ota_package(filename)
    return 0 if success else 1

if __name__ == '__main__':
    sys.exit(main())
