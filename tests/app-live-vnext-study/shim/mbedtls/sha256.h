#ifndef LV_SHIM_MBEDTLS_SHA256_H
#define LV_SHIM_MBEDTLS_SHA256_H

#include <stddef.h>
#include <stdint.h>

int mbedtls_sha256(const unsigned char *input, size_t ilen, unsigned char output[32], int is224);

#endif
