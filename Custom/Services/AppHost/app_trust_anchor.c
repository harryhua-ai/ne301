#include "app_trust_anchor.h"

#include <string.h>

#include "mbedtls/sha256.h"

#include "app_admission.h"

#define ANCHOR_PUBLISHER_ID "ne301-exp-pub"

static const uint8_t g_anchor_spki_der[] = {
    0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02,
    0x01, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03,
    0x42, 0x00, 0x04, 0x7f, 0xcf, 0x75, 0xcd, 0x65, 0x69, 0xa5, 0x8f, 0x9c,
    0x2b, 0x26, 0xc6, 0xfa, 0x99, 0x16, 0x66, 0xaa, 0xc3, 0x9d, 0x44, 0x3f,
    0x7c, 0x8f, 0xeb, 0xb6, 0x14, 0xeb, 0x9d, 0x6d, 0x89, 0x23, 0xbc, 0xeb,
    0x89, 0xa3, 0xec, 0x42, 0x38, 0xa0, 0xdd, 0x9b, 0x38, 0x3c, 0x2b, 0xfd,
    0x94, 0x90, 0x45, 0x42, 0xa6, 0xa8, 0xb5, 0x70, 0x32, 0xcd, 0x4d, 0xfb,
    0x7b, 0x4c, 0xe5, 0xda, 0x86, 0xad, 0x3f,
};

static const uint8_t g_anchor_spki_sha256[] = {
    0x06, 0xe7, 0x32, 0xb1, 0xb9, 0x4d, 0xae, 0xee, 0xf4, 0x33, 0x29, 0x7f,
    0x7a, 0x8e, 0x79, 0x7b, 0xf7, 0x2b, 0x2f, 0xd3, 0xb8, 0x82, 0xa4, 0xec,
    0x6a, 0x54, 0x90, 0x35, 0x29, 0x31, 0xeb, 0xda,
};

static const app_trust_anchor_t g_app_trust_anchor = {
    APP_TRUST_ANCHOR_FORMAT_VERSION,
    ANCHOR_PUBLISHER_ID,
    sizeof(ANCHOR_PUBLISHER_ID) - 1u,
    g_anchor_spki_der,
    sizeof(g_anchor_spki_der),
    g_anchor_spki_sha256
};

const app_trust_anchor_t *app_trust_anchor_get(void)
{
    return &g_app_trust_anchor;
}

app_trust_anchor_status_t app_trust_anchor_status(void)
{
    const app_trust_anchor_t *anchor = &g_app_trust_anchor;
    uint8_t computed[32];
    volatile uint8_t diff = 0;
    size_t i;

    if (anchor->format_version != APP_TRUST_ANCHOR_FORMAT_VERSION ||
        anchor->publisher_id == NULL ||
        anchor->publisher_id_len == 0u ||
        anchor->spki_der == NULL ||
        anchor->spki_sha256 == NULL) {
        return APP_TRUST_ANCHOR_MISSING;
    }

    if (anchor->publisher_id_len != strlen(ANCHOR_PUBLISHER_ID) ||
        memcmp(anchor->publisher_id, ANCHOR_PUBLISHER_ID, anchor->publisher_id_len) != 0) {
        return APP_TRUST_ANCHOR_MALFORMED;
    }

    if (anchor->spki_der_len != 91u) {
        return APP_TRUST_ANCHOR_MALFORMED;
    }

    if (!app_admission_identifier_valid((const uint8_t *)anchor->publisher_id, anchor->publisher_id_len)) {
        return APP_TRUST_ANCHOR_MALFORMED;
    }

    mbedtls_sha256(anchor->spki_der, anchor->spki_der_len, computed, 0);
    for (i = 0; i < 32u; i++) {
        diff |= (uint8_t)(computed[i] ^ anchor->spki_sha256[i]);
    }
    if (diff != 0) {
        return APP_TRUST_ANCHOR_MALFORMED;
    }

    return APP_TRUST_ANCHOR_PROVISIONED;
}

const char *app_trust_anchor_status_str(app_trust_anchor_status_t status)
{
    switch (status) {
    case APP_TRUST_ANCHOR_PROVISIONED:
        return "provisioned";
    case APP_TRUST_ANCHOR_MISSING:
        return "missing";
    case APP_TRUST_ANCHOR_MALFORMED:
        return "malformed";
    default:
        return "unknown";
    }
}
