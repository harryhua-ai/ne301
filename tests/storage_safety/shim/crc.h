#ifndef HOST_SHIM_CRC_H
#define HOST_SHIM_CRC_H
/* Host shim for the firmware CRC32 hook littlefs is configured with
 * (lfs_util.h: LFS_CRC32 -> CRC_Accumulate). littlefs computes its metadata
 * CRCs internally, so this only needs to be correct if someone calls it. */
#include <stdint.h>
#include <stddef.h>

static inline uint32_t CRC_Accumulate(uint32_t seed, uint32_t *data, uint32_t size)
{
    /* Standard CRC-32 (reflected, poly 0xEDB88320), byte-wise over the
     * buffer; word pointer is only the firmware API shape. */
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = seed ^ 0xFFFFFFFFU;
    for (uint32_t i = 0; i < size; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return crc ^ 0xFFFFFFFFU;
}
#endif
