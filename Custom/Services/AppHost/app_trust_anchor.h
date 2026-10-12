#ifndef APP_TRUST_ANCHOR_H
#define APP_TRUST_ANCHOR_H

#include <stddef.h>
#include <stdint.h>

#include "aicam_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_TRUST_ANCHOR_FORMAT_VERSION 1u

typedef enum {
    APP_TRUST_ANCHOR_PROVISIONED = 0,
    APP_TRUST_ANCHOR_MISSING = -1,
    APP_TRUST_ANCHOR_MALFORMED = -2
} app_trust_anchor_status_t;

typedef struct {
    uint32_t format_version;
    const char *publisher_id;
    size_t publisher_id_len;
    const uint8_t *spki_der;
    size_t spki_der_len;
    const uint8_t *spki_sha256;
} app_trust_anchor_t;

const app_trust_anchor_t *app_trust_anchor_get(void);

app_trust_anchor_status_t app_trust_anchor_status(void);

const char *app_trust_anchor_status_str(app_trust_anchor_status_t status);

#ifdef __cplusplus
}
#endif

#endif
