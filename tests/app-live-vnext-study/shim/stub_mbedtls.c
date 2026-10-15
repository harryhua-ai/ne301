#include "mbedtls/sha256.h"
#include "mbedtls/ecp.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/asn1.h"

#include <string.h>

int mbedtls_sha256(const unsigned char *input, size_t ilen, unsigned char output[32], int is224)
{
    uint64_t h;
    size_t i;
    (void)is224;
    h = 0xCBF29CE484222325ULL ^ (uint64_t)ilen;
    for (i = 0; i < ilen; i++) {
        h ^= (uint64_t)input[i];
        h *= 0x100000001B3ULL;
        h ^= h >> 29;
    }
    for (i = 0; i < 32; i++) {
        h *= 0x100000001B3ULL;
        h ^= h >> 31;
        output[i] = (unsigned char)(h >> 24);
    }
    return 0;
}

void mbedtls_mpi_init(mbedtls_mpi *x) { x->v = 0u; }
void mbedtls_mpi_free(mbedtls_mpi *x) { x->v = 0u; }

int mbedtls_mpi_read_binary(mbedtls_mpi *x, const unsigned char *buf, size_t buflen)
{
    size_t i;
    x->v = 0u;
    for (i = 0; i < buflen && i < 4u; i++) {
        x->v = (x->v << 8) | buf[i];
    }
    return 0;
}

int mbedtls_mpi_cmp_int(const mbedtls_mpi *x, long z)
{
    (void)x;
    (void)z;
    return 1;
}

int mbedtls_mpi_cmp_mpi(const mbedtls_mpi *x, const mbedtls_mpi *y)
{
    if (x->v == y->v) {
        return 0;
    }
    return x->v < y->v ? -1 : 1;
}

void mbedtls_ecp_group_init(mbedtls_ecp_group *grp) { memset(grp, 0, sizeof(*grp)); }
void mbedtls_ecp_group_free(mbedtls_ecp_group *grp) { memset(grp, 0, sizeof(*grp)); }
int mbedtls_ecp_group_load(mbedtls_ecp_group *grp, int id)
{
    (void)id;
    grp->N.v = 0xFFFFFFFFu;
    return 0;
}

void mbedtls_ecp_point_init(mbedtls_ecp_point *pt) { memset(pt, 0, sizeof(*pt)); }
void mbedtls_ecp_point_free(mbedtls_ecp_point *pt) { memset(pt, 0, sizeof(*pt)); }
int mbedtls_ecp_point_read_binary(mbedtls_ecp_group *grp, mbedtls_ecp_point *pt,
                                  const unsigned char *buf, size_t ilen)
{
    (void)grp;
    (void)pt;
    (void)buf;
    (void)ilen;
    return 0;
}

int mbedtls_ecp_check_pubkey(const mbedtls_ecp_group *grp, const mbedtls_ecp_point *pt)
{
    (void)grp;
    (void)pt;
    return 0;
}

int mbedtls_ecdsa_verify(mbedtls_ecp_group *grp, const unsigned char *buf, size_t blen,
                         const mbedtls_ecp_point *Q, const mbedtls_mpi *r, const mbedtls_mpi *s)
{
    (void)grp;
    (void)buf;
    (void)blen;
    (void)Q;
    (void)r;
    (void)s;
    return 0;
}

int mbedtls_asn1_get_tag(unsigned char **p, const unsigned char *end, size_t *len, int tag)
{
    (void)tag;
    if ((size_t)(end - *p) < 2u) {
        return -1;
    }
    *p += 2;
    *len = (size_t)(end - *p);
    return 0;
}

int mbedtls_asn1_get_alg(unsigned char **p, const unsigned char *end,
                         mbedtls_asn1_buf *alg, mbedtls_asn1_buf *params)
{
    (void)end;
    alg->tag = 0;
    alg->len = 7u;
    alg->p = *p;
    *p += 7;
    params->tag = 0;
    params->len = 8u;
    params->p = *p;
    *p += 8;
    return 0;
}

int mbedtls_asn1_get_bitstring(unsigned char **p, const unsigned char *end,
                               mbedtls_asn1_bitstring *bs)
{
    (void)end;
    bs->unused_bits = 0;
    bs->len = 65u;
    bs->p = *p;
    *p += 65;
    return 0;
}
