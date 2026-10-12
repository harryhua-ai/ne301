#include "app_trust_anchor.h"

#include <string.h>

#include "mbedtls/sha256.h"

#include "app_admission.h"

#define ANCHOR_PUBLISHER_ID "ne301-exp-pub"

static const uint8_t g_anchor_spki_der[] = {
    0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02,
    0x01, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03,
    0x42, 0x00, 0x04, 0xe0, 0x07, 0x2a, 0xf0, 0x71, 0x96, 0x00, 0xa2, 0x6a,
    0x64, 0x57, 0x24, 0x9f, 0xc3, 0x40, 0x1a, 0x88, 0x49, 0x3c, 0x09, 0x40,
    0x34, 0xba, 0x72, 0x97, 0x58, 0x27, 0x32, 0x3e, 0xd9, 0xca, 0xe4, 0x02,
    0x1d, 0x85, 0x17, 0xe7, 0xb7, 0x8c, 0xaa, 0x85, 0x14, 0x77, 0x59, 0x74,
    0xca, 0x6b, 0x64, 0xf6, 0xfd, 0xdd, 0x14, 0x16, 0x23, 0x10, 0x68, 0x66,
    0x19, 0x97, 0xf1, 0xef, 0xe4, 0xd8, 0x22,
};

static const uint8_t g_anchor_spki_sha256[] = {
    0x39, 0x61, 0x65, 0x56, 0x2e, 0x93, 0x35, 0x4c, 0x48, 0xca, 0xd7, 0xcb,
    0xf6, 0xb9, 0x29, 0xf3, 0x63, 0x6a, 0x9a, 0x6b, 0x82, 0x29, 0x71, 0x5a,
    0x77, 0x23, 0xa6, 0xd2, 0xf3, 0x6d, 0xa8, 0x56,
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
