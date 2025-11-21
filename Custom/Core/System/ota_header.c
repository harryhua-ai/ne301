/**
 * @file ota_header.c
 * @brief OTA firmware package header information processing implementation
 * @version 1.0
 * @date 2025-10-15
 */

#include "ota_header.h"
#include "generic_math.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

/**
 * @brief Safe string copy
 */

int ota_header_verify(const ota_header_t *header)
{
    if (!header) {
        return -1;
    }
    
    // Check magic number
    if (header->magic != OTA_MAGIC_NUMBER) {
        printf("Error: Invalid magic number 0x%08lX\r\n", header->magic);
        return -1;
    }
    
    // Check header version
    if (header->header_version != OTA_HEADER_VERSION) {
        printf("Error: Unsupported header version 0x%04X\r\n", header->header_version);
        return -1;
    }
    
    // Check header size
    if (header->header_size != OTA_HEADER_SIZE) {
        printf("Error: Invalid header size %d\r\n", header->header_size);
        return -1;
    }
    
    // Verify header CRC32
    ota_header_t temp_header = *header;
    temp_header.header_crc32 = 0;  // Exclude CRC32 field during calculation
    uint32_t calculated_crc = generic_crc32((const uint8_t*)&temp_header, sizeof(ota_header_t));
    
    if (calculated_crc != header->header_crc32) {
        printf("Error: Header CRC32 verification failed (expected: 0x%08lX, actual: 0x%08lX)\r\n", 
               header->header_crc32, calculated_crc);
        return -1;
    }
    
    printf("Header verification passed\r\n");
    return 0;
}

