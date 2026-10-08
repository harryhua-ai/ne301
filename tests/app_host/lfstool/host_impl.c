#include <stdint.h>
#include <stddef.h>
uint32_t CRC_Accumulate(uint32_t crc, const uint32_t *buffer, size_t size) {
    const uint8_t *p = (const uint8_t *)buffer;
    crc = ~crc;
    for (size_t i = 0; i < size; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0x82F63B78u & (uint32_t)-(int32_t)(crc & 1));
        }
    }
    return ~crc;
}
