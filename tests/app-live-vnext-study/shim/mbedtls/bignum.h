#ifndef LV_SHIM_MBEDTLS_BIGNUM_H
#define LV_SHIM_MBEDTLS_BIGNUM_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t v;
} mbedtls_mpi;

void mbedtls_mpi_init(mbedtls_mpi *x);
void mbedtls_mpi_free(mbedtls_mpi *x);
int mbedtls_mpi_read_binary(mbedtls_mpi *x, const unsigned char *buf, size_t buflen);
int mbedtls_mpi_cmp_int(const mbedtls_mpi *x, long z);
int mbedtls_mpi_cmp_mpi(const mbedtls_mpi *x, const mbedtls_mpi *y);

#endif
