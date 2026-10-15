#ifndef LV_SHIM_MBEDTLS_ECP_H
#define LV_SHIM_MBEDTLS_ECP_H

#include "mbedtls/bignum.h"

#define MBEDTLS_ECP_DP_NONE 0
#define MBEDTLS_ECP_DP_SECP256R1 1

typedef struct {
    mbedtls_mpi N;
} mbedtls_ecp_group;

typedef struct {
    int dummy;
} mbedtls_ecp_point;

void mbedtls_ecp_group_init(mbedtls_ecp_group *grp);
void mbedtls_ecp_group_free(mbedtls_ecp_group *grp);
int mbedtls_ecp_group_load(mbedtls_ecp_group *grp, int id);
void mbedtls_ecp_point_init(mbedtls_ecp_point *pt);
void mbedtls_ecp_point_free(mbedtls_ecp_point *pt);
int mbedtls_ecp_point_read_binary(mbedtls_ecp_group *grp, mbedtls_ecp_point *pt,
                                  const unsigned char *buf, size_t ilen);
int mbedtls_ecp_check_pubkey(const mbedtls_ecp_group *grp, const mbedtls_ecp_point *pt);

#endif
