#ifndef LV_SHIM_MBEDTLS_ECDSA_H
#define LV_SHIM_MBEDTLS_ECDSA_H

#include "mbedtls/ecp.h"

int mbedtls_ecdsa_verify(mbedtls_ecp_group *grp, const unsigned char *buf, size_t blen,
                         const mbedtls_ecp_point *Q, const mbedtls_mpi *r, const mbedtls_mpi *s);

#endif
