#include "app_trust_anchor.h"

#include <string.h>

#include "mbedtls/sha256.h"

#include "app_admission.h"

#define ANCHOR_PUBLISHER_ID "ne301-exp-pub"

static const uint8_t g_anchor_spki_der[] = {
    0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02,
    0x01, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03,
    0x42, 0x00, 0x04, 0x8c, 0x0e, 0x4e, 0x0a, 0x97, 0x00, 0xd1, 0xf4, 0x98,
    0x18, 0x5b, 0x5d, 0xfa, 0x68, 0x6a, 0xfd, 0xc5, 0x87, 0xc8, 0x6a, 0xf9,
    0x2c, 0x88, 0x25, 0xfc, 0x47, 0xed, 0xc8, 0xb3, 0xa4, 0xbb, 0x28, 0x4c,
    0xa9, 0xb7, 0xe2, 0x7c, 0xa4, 0x34, 0xc7, 0xc3, 0x22, 0x22, 0x06, 0xd2,
    0x11, 0xea, 0xc0, 0x54, 0x68, 0x6e, 0xc8, 0xc1, 0x33, 0x2c, 0x5c, 0x07,
    0xff, 0x56, 0xfa, 0xc2, 0xb1, 0xcd, 0x6d,
};

static const uint8_t g_anchor_spki_sha256[] = {
    0xf5, 0xca, 0x10, 0x1c, 0x1a, 0x3c, 0xb2, 0x07, 0x2d, 0xb7, 0xae, 0x4b,
    0x00, 0x0c, 0x81, 0xbe, 0xc6, 0x55, 0x79, 0x36, 0xdb, 0x74, 0xcc, 0x2f,
    0x6f, 0x68, 0xa0, 0x35, 0x3e, 0x1f, 0xd0, 0x31,
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
