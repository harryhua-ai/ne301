#ifndef GENERIC_MATH_H
#define GENERIC_MATH_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t generic_crc32(const uint8_t *data, size_t length);

#ifdef __cplusplus
}
#endif
#endif