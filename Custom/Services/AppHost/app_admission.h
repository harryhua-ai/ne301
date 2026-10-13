#ifndef APP_ADMISSION_H
#define APP_ADMISSION_H

#include <stddef.h>
#include <stdint.h>

#include "aicam_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_ADMISSION_CONTAINER_MAGIC_SIZE 8u
#define APP_ADMISSION_LENGTHS_SIZE 8u
#define APP_ADMISSION_MANIFEST_SIZE 160u
#define APP_ADMISSION_NATIVE_HEADER_SIZE 32u
#define APP_ADMISSION_CONTAINER_HEADER_SIZE (APP_ADMISSION_CONTAINER_MAGIC_SIZE + APP_ADMISSION_LENGTHS_SIZE)
#define APP_ADMISSION_MANIFEST_OFFSET APP_ADMISSION_CONTAINER_HEADER_SIZE
#define APP_ADMISSION_IMAGE_OFFSET (APP_ADMISSION_CONTAINER_HEADER_SIZE + APP_ADMISSION_MANIFEST_SIZE)
#define APP_ADMISSION_DER_LEN_MIN 8u
#define APP_ADMISSION_DER_LEN_MAX 72u
#define APP_ADMISSION_MAX_NATIVE_FILE_LEN 2097152u
#define APP_ADMISSION_MAX_PACKAGE_LEN 2097400u
#define APP_ADMISSION_PUBLISHER_ID_SLOT 16u
#define APP_ADMISSION_APP_ID_SLOT 32u
#define APP_ADMISSION_SHA256_LEN 32u
#define APP_ADMISSION_CAP_LOG 0x00000001u
#define APP_ADMISSION_CAP_TICK_MS 0x00000002u
#define APP_ADMISSION_KNOWN_CAPS_MASK (APP_ADMISSION_CAP_LOG | APP_ADMISSION_CAP_TICK_MS)
#define APP_ADMISSION_EXPERIMENT_BOARD_ID 0x00003010u

typedef enum {
    APP_ADMISSION_OK = 0,
    APP_ADMISSION_ERR_PARAM = -1,
    APP_ADMISSION_ERR_BAD_PACKAGE = -2,
    APP_ADMISSION_ERR_SIGNATURE_INVALID = -3,
    APP_ADMISSION_ERR_PUBLISHER_UNTRUSTED = -4,
    APP_ADMISSION_ERR_TARGET_INCOMPATIBLE = -5,
    APP_ADMISSION_ERR_ABI_INCOMPATIBLE = -6,
    APP_ADMISSION_ERR_RESOURCE_LIMIT = -7,
    APP_ADMISSION_ERR_TRUST_UNPROVEN = -8
} app_admission_result_t;

typedef struct {
    uint32_t target_board_id;
    uint32_t psram_mib;
    uint32_t abi_version;
    uint32_t supported_caps_mask;
    uint32_t exec_region_base;
    uint32_t exec_region_size;
    size_t max_package_len;
} app_admission_policy_t;

typedef struct {
    const char *publisher_id;
    size_t publisher_id_len;
    const uint8_t *spki_der;
    size_t spki_der_len;
} app_admission_trust_view_t;

typedef struct {
    char publisher_id[APP_ADMISSION_PUBLISHER_ID_SLOT + 1];
    char app_id[APP_ADMISSION_APP_ID_SLOT + 1];
    uint16_t version_major;
    uint16_t version_minor;
    uint16_t version_patch;
    uint32_t required_psram_mib;
    uint32_t required_host_abi;
    uint32_t required_host_caps;
    uint32_t required_exec_region_bytes;
    uint32_t native_image_size;
    uint32_t native_entry_offset;
    uint32_t native_target_addr;
    size_t tbs_len;
    size_t signature_len;
    uint8_t tbs_sha256[APP_ADMISSION_SHA256_LEN];
    uint8_t image_sha256[APP_ADMISSION_SHA256_LEN];
    uint8_t publisher_key_sha256[APP_ADMISSION_SHA256_LEN];
} app_admission_verdict_t;

app_admission_result_t app_admission_verify_package(const uint8_t *data,
                                                    size_t len,
                                                    const app_admission_policy_t *policy,
                                                    const app_admission_trust_view_t *trust,
                                                    app_admission_verdict_t *out);

aicam_bool_t app_admission_identifier_valid(const uint8_t *field, size_t slot_len);

const char *app_admission_result_str(app_admission_result_t result);

#ifdef __cplusplus
}
#endif

#endif
