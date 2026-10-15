#ifndef LV_SHIM_MBEDTLS_ASN1_H
#define LV_SHIM_MBEDTLS_ASN1_H

#include <stddef.h>
#include <stdint.h>

#define MBEDTLS_ASN1_CONSTRUCTED 0x20
#define MBEDTLS_ASN1_SEQUENCE 0x10

typedef struct {
    int tag;
    size_t len;
    unsigned char *p;
} mbedtls_asn1_buf;

typedef struct {
    int unused_bits;
    size_t len;
    unsigned char *p;
} mbedtls_asn1_bitstring;

int mbedtls_asn1_get_tag(unsigned char **p, const unsigned char *end, size_t *len, int tag);
int mbedtls_asn1_get_alg(unsigned char **p, const unsigned char *end,
                         mbedtls_asn1_buf *alg, mbedtls_asn1_buf *params);
int mbedtls_asn1_get_bitstring(unsigned char **p, const unsigned char *end,
                               mbedtls_asn1_bitstring *bs);

#endif
