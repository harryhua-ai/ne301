#include "app_admission.h"

#include <string.h>

#include "generic_math.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/sha256.h"
#include "mbedtls/asn1.h"

#include "app_host_abi.h"

#define APP_ADMISSION_MANIFEST_MAGIC "NMF1"
#define APP_ADMISSION_MANIFEST_MAGIC_V2 "NMF2"
#define APP_ADMISSION_MANIFEST_MAGIC_V3 "NMF3"
#define APP_ADMISSION_MANIFEST_MAGIC_V4 "NMF4"
#define APP_ADMISSION_NATIVE_HEADER_MAGIC 0x3141454Eu

static const uint8_t g_ad_container_magic_v1[APP_ADMISSION_CONTAINER_MAGIC_SIZE] = {
    0x4Eu, 0x45u, 0x41u, 0x50u, 0x50u, 0x01u, 0x00u, 0x00u
};

static const uint8_t g_ad_container_magic_v2[APP_ADMISSION_CONTAINER_MAGIC_SIZE] = {
    0x4Eu, 0x45u, 0x41u, 0x50u, 0x50u, 0x02u, 0x00u, 0x00u
};

static const uint8_t g_ad_container_magic_v3[APP_ADMISSION_CONTAINER_MAGIC_SIZE] = {
    0x4Eu, 0x45u, 0x41u, 0x50u, 0x50u, 0x03u, 0x00u, 0x00u
};

static const uint8_t g_ad_container_magic_v4[APP_ADMISSION_CONTAINER_MAGIC_SIZE] = {
    0x4Eu, 0x45u, 0x41u, 0x50u, 0x50u, 0x04u, 0x00u, 0x00u
};

typedef struct {
    size_t image_offset;
    size_t image_len;
    size_t sig_offset;
    size_t sig_len;
} app_admission_layout_t;

typedef struct {
    uint16_t version_major;
    uint16_t version_minor;
    uint16_t version_patch;
    uint32_t target_board_id;
    uint32_t required_psram_mib;
    uint32_t required_host_abi;
    uint32_t required_host_caps;
    uint32_t required_exec_region_bytes;
    uint32_t native_file_len;
    uint32_t native_image_size;
    uint32_t native_entry_offset;
    uint32_t native_target_addr;
    const uint8_t *native_file_sha256;
    const uint8_t *publisher_key_sha256;
} app_admission_manifest_fields_t;

static uint32_t ad_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t ad_u16le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int ad_ct_equal(const uint8_t *a, const uint8_t *b, size_t len)
{
    volatile uint8_t diff = 0;
    size_t i;
    for (i = 0; i < len; i++) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff == 0;
}

aicam_bool_t app_admission_identifier_valid(const uint8_t *field, size_t slot_len)
{
    size_t text_len = 0;
    size_t i;

    if (field == NULL || slot_len == 0u) {
        return AICAM_FALSE;
    }

    while (text_len < slot_len && field[text_len] != 0x00u) {
        text_len++;
    }

    for (i = text_len; i < slot_len; i++) {
        if (field[i] != 0x00u) {
            return AICAM_FALSE;
        }
    }

    if (text_len == 0u) {
        return AICAM_FALSE;
    }

    if (field[0] < (uint8_t)'a' || field[0] > (uint8_t)'z') {
        return AICAM_FALSE;
    }

    for (i = 0; i < text_len; i++) {
        uint8_t c = field[i];
        if (!((c >= (uint8_t)'a' && c <= (uint8_t)'z') ||
              (c >= (uint8_t)'0' && c <= (uint8_t)'9') ||
              c == (uint8_t)'-')) {
            return AICAM_FALSE;
        }
    }

    if (text_len > 1u) {
        uint8_t last = field[text_len - 1u];
        if (last == (uint8_t)'-') {
            return AICAM_FALSE;
        }
    }

    return AICAM_TRUE;
}

static app_admission_result_t ad_parse_layout(const uint8_t *data,
                                              size_t len,
                                              size_t max_package_len,
                                              app_admission_layout_t *layout)
{
    uint32_t manifest_len;
    uint32_t image_len;
    uint64_t signed_region;
    uint64_t min_total;

    if (len < (size_t)APP_ADMISSION_CONTAINER_HEADER_SIZE) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    if (data[0] != 0x4Eu || data[1] != 0x45u || data[2] != 0x41u || data[3] != 0x50u ||
        data[4] != 0x50u || data[5] != 0x01u || data[6] != 0x00u || data[7] != 0x00u) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    manifest_len = ad_u32le(data + 8);
    image_len = ad_u32le(data + 12);

    if (manifest_len != APP_ADMISSION_MANIFEST_SIZE) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    if (image_len < APP_ADMISSION_NATIVE_HEADER_SIZE) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    if ((uint64_t)image_len > (uint64_t)APP_ADMISSION_MAX_NATIVE_FILE_LEN) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    signed_region = (uint64_t)APP_ADMISSION_IMAGE_OFFSET + (uint64_t)image_len;
    min_total = signed_region + (uint64_t)APP_ADMISSION_DER_LEN_MIN;

    if (min_total > (uint64_t)len) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    if (min_total > (uint64_t)APP_ADMISSION_MAX_PACKAGE_LEN ||
        (max_package_len != 0u && min_total > (uint64_t)max_package_len)) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    layout->image_offset = APP_ADMISSION_IMAGE_OFFSET;
    layout->image_len = (size_t)image_len;
    layout->sig_offset = (size_t)signed_region;
    layout->sig_len = len - (size_t)signed_region;

    if (layout->sig_len > APP_ADMISSION_DER_LEN_MAX) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    return APP_ADMISSION_OK;
}

static app_admission_result_t ad_parse_manifest(const uint8_t *manifest,
                                                app_admission_manifest_fields_t *m)
{
    if (memcmp(manifest, APP_ADMISSION_MANIFEST_MAGIC, 4) != 0) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    if (!app_admission_identifier_valid(manifest + 4, APP_ADMISSION_PUBLISHER_ID_SLOT)) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    if (!app_admission_identifier_valid(manifest + 20, APP_ADMISSION_APP_ID_SLOT)) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    if (manifest[58] != 0x00u || manifest[59] != 0x00u) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    if ((uint64_t)ad_u32le(manifest + 72) & ~(uint64_t)APP_ADMISSION_KNOWN_CAPS_MASK) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    m->version_major = ad_u16le(manifest + 52);
    m->version_minor = ad_u16le(manifest + 54);
    m->version_patch = ad_u16le(manifest + 56);
    m->target_board_id = ad_u32le(manifest + 60);
    m->required_psram_mib = ad_u32le(manifest + 64);
    m->required_host_abi = ad_u32le(manifest + 68);
    m->required_host_caps = ad_u32le(manifest + 72);
    m->required_exec_region_bytes = ad_u32le(manifest + 76);
    m->native_file_len = ad_u32le(manifest + 80);
    m->native_image_size = ad_u32le(manifest + 84);
    m->native_entry_offset = ad_u32le(manifest + 88);
    m->native_target_addr = ad_u32le(manifest + 92);
    m->native_file_sha256 = manifest + 96;
    m->publisher_key_sha256 = manifest + 128;

    return APP_ADMISSION_OK;
}

static app_admission_result_t ad_parse_layout_v2(const uint8_t *data,
                                                 size_t len,
                                                 size_t max_package_len,
                                                 app_admission_layout_t *layout)
{
    uint32_t manifest_len;
    uint32_t image_len;
    uint64_t signed_region;
    uint64_t min_total;

    if (len < (size_t)APP_ADMISSION_CONTAINER_HEADER_SIZE) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    manifest_len = ad_u32le(data + 8);
    image_len = ad_u32le(data + 12);

    if (manifest_len != APP_ADMISSION_MANIFEST_SIZE_V2) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    if (image_len < APP_ADMISSION_NATIVE_HEADER_SIZE) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    if ((uint64_t)image_len > (uint64_t)APP_ADMISSION_MAX_NATIVE_FILE_LEN) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    signed_region = (uint64_t)APP_ADMISSION_IMAGE_OFFSET_V2 + (uint64_t)image_len;
    min_total = signed_region + (uint64_t)APP_ADMISSION_DER_LEN_MIN;

    if (min_total > (uint64_t)len) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    if ((uint64_t)len > (uint64_t)APP_ADMISSION_MAX_PACKAGE_LEN_V2 ||
        (max_package_len != 0u && (uint64_t)len > (uint64_t)max_package_len)) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    layout->image_offset = APP_ADMISSION_IMAGE_OFFSET_V2;
    layout->image_len = (size_t)image_len;
    layout->sig_offset = (size_t)signed_region;
    layout->sig_len = len - (size_t)signed_region;

    if (layout->sig_len > APP_ADMISSION_DER_LEN_MAX) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    return APP_ADMISSION_OK;
}

static app_admission_result_t ad_parse_layout_v3(const uint8_t *data,
                                                 size_t len,
                                                 size_t max_package_len,
                                                 app_admission_layout_t *layout)
{
    uint32_t manifest_len;
    uint32_t image_len;
    uint64_t signed_region;
    uint64_t min_total;

    if (len < (size_t)APP_ADMISSION_CONTAINER_HEADER_SIZE) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    if (data[0] != 0x4Eu || data[1] != 0x45u || data[2] != 0x41u || data[3] != 0x50u ||
        data[4] != 0x50u || data[5] != 0x03u || data[6] != 0x00u || data[7] != 0x00u) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    manifest_len = ad_u32le(data + 8);
    image_len = ad_u32le(data + 12);

    if (manifest_len != APP_ADMISSION_MANIFEST_SIZE_V3) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    if (image_len < APP_ADMISSION_NATIVE_HEADER_SIZE) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    if ((uint64_t)image_len > (uint64_t)APP_ADMISSION_MAX_NATIVE_FILE_LEN) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    if ((uint64_t)len > (uint64_t)APP_ADMISSION_MAX_PACKAGE_LEN_V3 ||
        (max_package_len != 0u && (uint64_t)len > (uint64_t)max_package_len)) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    signed_region = (uint64_t)APP_ADMISSION_IMAGE_OFFSET_V3 + (uint64_t)image_len;
    min_total = signed_region + (uint64_t)APP_ADMISSION_DER_LEN_MIN;

    if (min_total > (uint64_t)len) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    layout->image_offset = APP_ADMISSION_IMAGE_OFFSET_V3;
    layout->image_len = (size_t)image_len;
    layout->sig_offset = (size_t)signed_region;
    layout->sig_len = len - (size_t)signed_region;

    if (layout->sig_len < APP_ADMISSION_DER_LEN_MIN ||
        layout->sig_len > APP_ADMISSION_DER_LEN_MAX) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    return APP_ADMISSION_OK;
}

static app_admission_result_t ad_parse_layout_v4(const uint8_t *data,
                                                 size_t len,
                                                 size_t max_package_len,
                                                 app_admission_layout_t *layout)
{
    uint32_t manifest_len;
    uint32_t image_len;
    uint64_t signed_region;
    uint64_t total;

    if (len < (size_t)APP_ADMISSION_CONTAINER_HEADER_SIZE ||
        memcmp(data, g_ad_container_magic_v4, APP_ADMISSION_CONTAINER_MAGIC_SIZE) != 0) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    manifest_len = ad_u32le(data + 8u);
    image_len = ad_u32le(data + 12u);
    if (manifest_len != APP_ADMISSION_MANIFEST_SIZE_V4 ||
        image_len < APP_ADMISSION_NATIVE_HEADER_SIZE ||
        image_len > APP_ADMISSION_MAX_NATIVE_FILE_LEN) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    signed_region = (uint64_t)APP_ADMISSION_IMAGE_OFFSET_V4 + image_len;
    total = signed_region + (uint64_t)APP_ADMISSION_DER_LEN_MIN;
    if (total > len || len > APP_ADMISSION_MAX_PACKAGE_LEN_V4 ||
        (max_package_len != 0u && len > max_package_len) ||
        (uint64_t)len < signed_region + APP_ADMISSION_DER_LEN_MIN ||
        (uint64_t)len > signed_region + APP_ADMISSION_DER_LEN_MAX) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    layout->image_offset = APP_ADMISSION_IMAGE_OFFSET_V4;
    layout->image_len = image_len;
    layout->sig_offset = (size_t)signed_region;
    layout->sig_len = len - (size_t)signed_region;
    return APP_ADMISSION_OK;
}

static app_admission_result_t ad_parse_manifest_v2(const uint8_t *manifest,
                                                   app_admission_manifest_fields_t *m,
                                                   uint32_t *run_profile,
                                                   uint32_t *event_max,
                                                   uint32_t *state_quota,
                                                   uint32_t *report_max)
{
    uint32_t caps;

    if (memcmp(manifest, APP_ADMISSION_MANIFEST_MAGIC_V2, 4) != 0) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    if (!app_admission_identifier_valid(manifest + 4, APP_ADMISSION_PUBLISHER_ID_SLOT)) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    if (!app_admission_identifier_valid(manifest + 20, APP_ADMISSION_APP_ID_SLOT)) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    if (manifest[58] != 0x00u || manifest[59] != 0x00u) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    if (ad_u32le(manifest + 68) != APP_ADMISSION_ABI_VERSION_V2) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    if (ad_u32le(manifest + 160) != APP_ADMISSION_V2_RUN_PROFILE_SESSION) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    {
        size_t i;
        for (i = 176u; i < APP_ADMISSION_MANIFEST_SIZE_V2; i++) {
            if (manifest[i] != 0x00u) {
                return APP_ADMISSION_ERR_BAD_PACKAGE;
            }
        }
    }

    caps = ad_u32le(manifest + 72);
    if ((caps & ~(uint32_t)APP_ADMISSION_V2_KNOWN_CAPS_MASK) != 0u) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    *event_max = ad_u32le(manifest + 164);
    *state_quota = ad_u32le(manifest + 168);
    *report_max = ad_u32le(manifest + 172);
    *run_profile = ad_u32le(manifest + 160);

    if ((caps & APP_ADMISSION_V2_CAP_AI_EVENTS) != 0u && *event_max == 0u) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    if ((caps & APP_ADMISSION_V2_CAP_REPORT_SUBMIT) != 0u && *report_max == 0u) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    if ((caps & APP_ADMISSION_V2_CAP_APP_STATE) != 0u && *state_quota == 0u) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    if ((caps & APP_ADMISSION_V2_CAP_AI_EVENTS) == 0u && *event_max != 0u) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    if ((caps & APP_ADMISSION_V2_CAP_REPORT_SUBMIT) == 0u && *report_max != 0u) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    if ((caps & APP_ADMISSION_V2_CAP_APP_STATE) == 0u && *state_quota != 0u) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    if ((caps & APP_ADMISSION_V2_CAP_SESSION_LIFECYCLE) == 0u) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    m->version_major = ad_u16le(manifest + 52);
    m->version_minor = ad_u16le(manifest + 54);
    m->version_patch = ad_u16le(manifest + 56);
    m->target_board_id = ad_u32le(manifest + 60);
    m->required_psram_mib = ad_u32le(manifest + 64);
    m->required_host_abi = ad_u32le(manifest + 68);
    m->required_host_caps = caps;
    m->required_exec_region_bytes = ad_u32le(manifest + 76);
    m->native_file_len = ad_u32le(manifest + 80);
    m->native_image_size = ad_u32le(manifest + 84);
    m->native_entry_offset = ad_u32le(manifest + 88);
    m->native_target_addr = ad_u32le(manifest + 92);
    m->native_file_sha256 = manifest + 96;
    m->publisher_key_sha256 = manifest + 128;

    return APP_ADMISSION_OK;
}

static app_admission_result_t ad_parse_manifest_v3(const uint8_t *manifest,
                                                   app_admission_manifest_fields_t *m,
                                                   uint32_t *run_profile,
                                                   uint32_t *event_max,
                                                   uint32_t *state_quota,
                                                   uint32_t *report_max,
                                                   uint32_t *control_max,
                                                   uint32_t *snapshot_max)
{
    uint32_t caps;
    size_t i;

    if (memcmp(manifest, APP_ADMISSION_MANIFEST_MAGIC_V3, 4) != 0) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    if (!app_admission_identifier_valid(manifest + 4, APP_ADMISSION_PUBLISHER_ID_SLOT) ||
        !app_admission_identifier_valid(manifest + 20, APP_ADMISSION_APP_ID_SLOT)) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    if (manifest[58] != 0x00u || manifest[59] != 0x00u ||
        ad_u32le(manifest + 68) != APP_ADMISSION_ABI_VERSION_V3 ||
        ad_u32le(manifest + 160) != APP_ADMISSION_V3_RUN_PROFILE_HOT_COMMAND_SESSION) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    for (i = 184u; i < APP_ADMISSION_MANIFEST_SIZE_V3; i++) {
        if (manifest[i] != 0x00u) {
            return APP_ADMISSION_ERR_BAD_PACKAGE;
        }
    }

    caps = ad_u32le(manifest + 72);
    if ((caps & ~(uint32_t)APP_ADMISSION_V3_KNOWN_CAPS_MASK) != 0u ||
        (caps & APP_ADMISSION_V2_CAP_SESSION_LIFECYCLE) == 0u ||
        (caps & APP_ADMISSION_V3_CAP_CONTROL_RECEIVE) == 0u) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    *run_profile = ad_u32le(manifest + 160);
    *event_max = ad_u32le(manifest + 164);
    *state_quota = ad_u32le(manifest + 168);
    *report_max = ad_u32le(manifest + 172);
    *control_max = ad_u32le(manifest + 176);
    *snapshot_max = ad_u32le(manifest + 180);

    if (((caps & APP_ADMISSION_V2_CAP_AI_EVENTS) != 0u) != (*event_max != 0u) ||
        ((caps & APP_ADMISSION_V2_CAP_APP_STATE) != 0u) != (*state_quota != 0u) ||
        ((caps & APP_ADMISSION_V2_CAP_REPORT_SUBMIT) != 0u) != (*report_max != 0u) ||
        ((caps & APP_ADMISSION_V3_CAP_CONTROL_RECEIVE) != 0u) != (*control_max != 0u) ||
        ((caps & APP_ADMISSION_V3_CAP_BUSINESS_SNAPSHOT) != 0u) != (*snapshot_max != 0u)) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    m->version_major = ad_u16le(manifest + 52);
    m->version_minor = ad_u16le(manifest + 54);
    m->version_patch = ad_u16le(manifest + 56);
    m->target_board_id = ad_u32le(manifest + 60);
    m->required_psram_mib = ad_u32le(manifest + 64);
    m->required_host_abi = ad_u32le(manifest + 68);
    m->required_host_caps = caps;
    m->required_exec_region_bytes = ad_u32le(manifest + 76);
    m->native_file_len = ad_u32le(manifest + 80);
    m->native_image_size = ad_u32le(manifest + 84);
    m->native_entry_offset = ad_u32le(manifest + 88);
    m->native_target_addr = ad_u32le(manifest + 92);
    m->native_file_sha256 = manifest + 96;
    m->publisher_key_sha256 = manifest + 128;

    return APP_ADMISSION_OK;
}

static app_admission_result_t ad_parse_manifest_v4(const uint8_t *manifest,
                                                   app_admission_manifest_fields_t *m,
                                                   uint32_t *run_profile,
                                                   uint32_t *event_max,
                                                   uint32_t *state_quota,
                                                   uint32_t *report_max,
                                                   uint32_t *control_max,
                                                   uint32_t *snapshot_max,
                                                   uint32_t *live_publish_max,
                                                   uint32_t *live_track_max)
{
    uint32_t caps;
    size_t i;

    if (memcmp(manifest, APP_ADMISSION_MANIFEST_MAGIC_V4, 4u) != 0 ||
        !app_admission_identifier_valid(manifest + 4u, APP_ADMISSION_PUBLISHER_ID_SLOT) ||
        !app_admission_identifier_valid(manifest + 20u, APP_ADMISSION_APP_ID_SLOT) ||
        manifest[58] != 0u || manifest[59] != 0u ||
        ad_u32le(manifest + 68u) != APP_ADMISSION_ABI_VERSION_V4 ||
        ad_u32le(manifest + 160u) != APP_ADMISSION_V4_RUN_PROFILE_SESSION) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    for (i = 192u; i < APP_ADMISSION_MANIFEST_SIZE_V4; i++) {
        if (manifest[i] != 0u) {
            return APP_ADMISSION_ERR_BAD_PACKAGE;
        }
    }
    caps = ad_u32le(manifest + 72u);
    if ((caps & ~APP_ADMISSION_V4_KNOWN_CAPS_MASK) != 0u ||
        (caps & APP_ADMISSION_V2_CAP_SESSION_LIFECYCLE) == 0u) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    (*event_max) = ad_u32le(manifest + 164u);
    (*state_quota) = ad_u32le(manifest + 168u);
    (*report_max) = ad_u32le(manifest + 172u);
    (*control_max) = ad_u32le(manifest + 176u);
    (*snapshot_max) = ad_u32le(manifest + 180u);
    (*live_publish_max) = ad_u32le(manifest + 184u);
    (*live_track_max) = ad_u32le(manifest + 188u);
    (*run_profile) = APP_ADMISSION_V4_RUN_PROFILE_SESSION;
    if (((caps & APP_ADMISSION_V2_CAP_AI_EVENTS) != 0u) != (*event_max != 0u) ||
        ((caps & APP_ADMISSION_V2_CAP_APP_STATE) != 0u) != (*state_quota != 0u) ||
        ((caps & APP_ADMISSION_V2_CAP_REPORT_SUBMIT) != 0u) != (*report_max != 0u) ||
        ((caps & APP_ADMISSION_V3_CAP_CONTROL_RECEIVE) != 0u) != (*control_max != 0u) ||
        ((caps & APP_ADMISSION_V3_CAP_BUSINESS_SNAPSHOT) != 0u) != (*snapshot_max != 0u)) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    if ((caps & APP_ADMISSION_V4_CAP_ENABLE_CONTROL) != 0u &&
        (caps & (APP_ADMISSION_V2_CAP_APP_STATE |
                 APP_ADMISSION_V2_CAP_SESSION_LIFECYCLE |
                 APP_ADMISSION_V3_CAP_CONTROL_RECEIVE)) !=
            (APP_ADMISSION_V2_CAP_APP_STATE |
             APP_ADMISSION_V2_CAP_SESSION_LIFECYCLE |
             APP_ADMISSION_V3_CAP_CONTROL_RECEIVE)) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    if ((caps & APP_ADMISSION_V4_CAP_ENABLE_CONTROL) != 0u &&
        (*control_max) < APP_ADMISSION_V4_CONTROL_WIRE_MIN) {
        return APP_ADMISSION_ERR_RESOURCE_LIMIT;
    }
    if ((caps & APP_ADMISSION_V4_CAP_LIVE_PUBLISH) != 0u) {
        uint64_t closed_max;
        if ((caps & APP_ADMISSION_V2_CAP_SESSION_LIFECYCLE) == 0u ||
            (*live_track_max) == 0u || (*live_track_max) > APP_ADMISSION_V4_LIVE_TRACK_MAX) {
            return APP_ADMISSION_ERR_BAD_PACKAGE;
        }
        closed_max = (uint64_t)APP_ADMISSION_V4_LIVE_HEADER_SIZE +
                     (uint64_t)*live_track_max *
                         ((uint64_t)APP_ADMISSION_V4_LIVE_TRACK_HEADER_SIZE +
                          (uint64_t)APP_ADMISSION_V4_LIVE_TRACK_POINT_MAX *
                              APP_ADMISSION_V4_LIVE_TRACK_POINT_SIZE);
        if (*live_publish_max < APP_ADMISSION_V4_LIVE_MIN_PUBLISH ||
            (uint64_t)*live_publish_max < closed_max) {
            return APP_ADMISSION_ERR_RESOURCE_LIMIT;
        }
    } else if (*live_publish_max != 0u || *live_track_max != 0u) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    m->version_major = ad_u16le(manifest + 52u);
    m->version_minor = ad_u16le(manifest + 54u);
    m->version_patch = ad_u16le(manifest + 56u);
    m->target_board_id = ad_u32le(manifest + 60u);
    m->required_psram_mib = ad_u32le(manifest + 64u);
    m->required_host_abi = ad_u32le(manifest + 68u);
    m->required_host_caps = caps;
    m->required_exec_region_bytes = ad_u32le(manifest + 76u);
    m->native_file_len = ad_u32le(manifest + 80u);
    m->native_image_size = ad_u32le(manifest + 84u);
    m->native_entry_offset = ad_u32le(manifest + 88u);
    m->native_target_addr = ad_u32le(manifest + 92u);
    m->native_file_sha256 = manifest + 96u;
    m->publisher_key_sha256 = manifest + 128u;
    return APP_ADMISSION_OK;
}

static int ad_der_read_len(const uint8_t *buf, size_t buf_limit, size_t *pos, size_t *out_len)
{
    size_t idx = *pos;
    uint8_t b;
    size_t count;
    size_t value;
    size_t i;

    if (idx >= buf_limit) {
        return -1;
    }
    b = buf[idx];
    idx++;
    if (b < 0x80u) {
        *pos = idx;
        *out_len = b;
        return 0;
    }
    count = (size_t)(b & 0x7Fu);
    if (count == 0u || count > sizeof(size_t)) {
        return -1;
    }
    if (idx + count > buf_limit) {
        return -1;
    }
    if (buf[idx] == 0x00u) {
        return -1;
    }
    value = 0;
    for (i = 0; i < count; i++) {
        value = (value << 8) | buf[idx + i];
    }
    idx += count;
    if (value < 0x80u) {
        return -1;
    }
    *pos = idx;
    *out_len = value;
    return 0;
}

static int ad_der_parse_integer(const uint8_t *sig, size_t seq_end, size_t *pos, mbedtls_mpi *out)
{
    size_t idx = *pos;
    size_t int_len;
    const uint8_t *body;
    size_t i;
    int all_zero = 1;

    if (idx >= seq_end) {
        return -1;
    }
    if (sig[idx] != 0x02u) {
        return -1;
    }
    idx++;
    if (ad_der_read_len(sig, seq_end, &idx, &int_len) != 0) {
        return -1;
    }
    if (int_len == 0u || int_len > 33u || idx + int_len > seq_end) {
        return -1;
    }

    body = sig + idx;
    if ((body[0] & 0x80u) != 0u) {
        return -1;
    }
    if (int_len > 1u && body[0] == 0x00u && ((body[1] & 0x80u) == 0u)) {
        return -1;
    }

    for (i = 0; i < int_len; i++) {
        if (body[i] != 0x00u) {
            all_zero = 0;
            break;
        }
    }
    if (all_zero) {
        return -1;
    }

    if (mbedtls_mpi_read_binary(out, body, int_len) != 0) {
        return -1;
    }

    *pos = idx + int_len;
    return 0;
}

static int ad_parse_der_signature(const uint8_t *sig, size_t sig_len, mbedtls_mpi *r, mbedtls_mpi *s)
{
    size_t pos = 0;
    size_t seq_len;
    size_t seq_end;

    if (sig_len < 8u) {
        return -1;
    }
    if (sig[pos] != 0x30u) {
        return -1;
    }
    pos++;
    if (ad_der_read_len(sig, sig_len, &pos, &seq_len) != 0) {
        return -1;
    }
    if (seq_len < 6u || pos != 2u || pos + seq_len != sig_len) {
        return -1;
    }
    seq_end = pos + seq_len;

    if (ad_der_parse_integer(sig, seq_end, &pos, r) != 0) {
        return -1;
    }
    if (ad_der_parse_integer(sig, seq_end, &pos, s) != 0) {
        return -1;
    }
    if (pos != seq_end) {
        return -1;
    }
    return 0;
}

static int ad_validate_der_signature(const uint8_t *sig, size_t sig_len)
{
    mbedtls_mpi r;
    mbedtls_mpi s;
    mbedtls_ecp_group grp;
    int rc;

    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    rc = ad_parse_der_signature(sig, sig_len, &r, &s);
    mbedtls_ecp_group_init(&grp);
    if (rc == 0 && mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) != 0) {
        rc = -1;
    }
    if (rc == 0 && (mbedtls_mpi_cmp_int(&r, 1) < 0 ||
                    mbedtls_mpi_cmp_mpi(&r, &grp.N) >= 0 ||
                    mbedtls_mpi_cmp_int(&s, 1) < 0 ||
                    mbedtls_mpi_cmp_mpi(&s, &grp.N) >= 0)) {
        rc = -1;
    }
    mbedtls_ecp_group_free(&grp);
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    return rc;
}

static int ad_parse_spki_p256(const uint8_t *der, size_t len, mbedtls_ecp_group *grp, mbedtls_ecp_point *Q)
{
    mbedtls_asn1_buf alg_oid;
    mbedtls_asn1_buf alg_params;
    mbedtls_asn1_bitstring bitstring;
    unsigned char *p = (unsigned char *)(uintptr_t)der;
    const unsigned char *end = der + len;
    size_t seq_len;
    size_t parsed;
    int ret;

    if (mbedtls_asn1_get_tag(&p, end, &seq_len, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE) != 0) {
        return -1;
    }
    parsed = (size_t)(p - (const unsigned char *)der) + seq_len;
    if (parsed != len) {
        return -1;
    }

    if (mbedtls_asn1_get_alg(&p, end, &alg_oid, &alg_params) != 0) {
        return -1;
    }
    if (alg_oid.len != 7u || memcmp(alg_oid.p, "\x2A\x86\x48\xCE\x3D\x02\x01", 7) != 0) {
        return -1;
    }
    if (alg_params.len != 8u || memcmp(alg_params.p, "\x2A\x86\x48\xCE\x3D\x03\x01\x07", 8) != 0) {
        return -1;
    }

    if (mbedtls_asn1_get_bitstring(&p, end, &bitstring) != 0) {
        return -1;
    }
    if (bitstring.len != 65u || bitstring.unused_bits != 0 || bitstring.p == NULL) {
        return -1;
    }
    if ((size_t)(p - (const unsigned char *)der) != len) {
        return -1;
    }

    ret = mbedtls_ecp_group_load(grp, MBEDTLS_ECP_DP_SECP256R1);
    if (ret != 0) {
        return -1;
    }

    ret = mbedtls_ecp_point_read_binary(grp, Q, bitstring.p, bitstring.len);
    if (ret != 0) {
        return -1;
    }

    if (mbedtls_ecp_check_pubkey(grp, Q) != 0) {
        return -1;
    }
    return 0;
}

static app_admission_result_t ad_verify_signature(const uint8_t *data,
                                                  const app_admission_layout_t *layout,
                                                  const uint8_t *tbs_sha,
                                                  mbedtls_ecp_group *grp,
                                                  mbedtls_ecp_point *q_pub)
{
    mbedtls_mpi r;
    mbedtls_mpi s;
    app_admission_result_t result = APP_ADMISSION_ERR_SIGNATURE_INVALID;

    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    if (ad_parse_der_signature(data + layout->sig_offset, layout->sig_len, &r, &s) != 0) {
        result = APP_ADMISSION_ERR_BAD_PACKAGE;
        goto cleanup;
    }

    if (mbedtls_mpi_cmp_int(&r, 1) < 0 || mbedtls_mpi_cmp_mpi(&r, &grp->N) >= 0 ||
        mbedtls_mpi_cmp_int(&s, 1) < 0 || mbedtls_mpi_cmp_mpi(&s, &grp->N) >= 0) {
        result = APP_ADMISSION_ERR_BAD_PACKAGE;
        goto cleanup;
    }

    if (mbedtls_ecdsa_verify(grp, tbs_sha, 32u, q_pub, &r, &s) != 0) {
        result = APP_ADMISSION_ERR_SIGNATURE_INVALID;
        goto cleanup;
    }

    result = APP_ADMISSION_OK;

cleanup:
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    return result;
}

static size_t ad_manifest_identifier_len(const uint8_t *field, size_t slot_len)
{
    size_t text_len = 0;
    while (text_len < slot_len && field[text_len] != 0x00u) {
        text_len++;
    }
    return text_len;
}

static app_admission_result_t ad_verify_v1(const uint8_t *data,
                                           size_t len,
                                           const app_admission_policy_t *policy,
                                           const app_admission_trust_view_t *trust,
                                           app_admission_verdict_t *out)
{
    app_admission_layout_t layout;
    app_admission_manifest_fields_t mf;
    const uint8_t *manifest;
    const uint8_t *img;
    const app_host_image_header_t *hdr;
    uint8_t tbs_sha[APP_ADMISSION_SHA256_LEN];
    uint8_t image_sha[APP_ADMISSION_SHA256_LEN];
    uint8_t fp[APP_ADMISSION_SHA256_LEN];
    uint8_t trust_pub[APP_ADMISSION_PUBLISHER_ID_SLOT];
    size_t trust_pub_len;
    size_t pkg_pub_len;
    mbedtls_ecp_group grp;
    mbedtls_ecp_point q_pub;
    app_admission_result_t rc;

    rc = ad_parse_layout(data, len, policy->max_package_len, &layout);
    if (rc != APP_ADMISSION_OK) {
        return rc;
    }

    manifest = data + APP_ADMISSION_MANIFEST_OFFSET;
    rc = ad_parse_manifest(manifest, &mf);
    if (rc != APP_ADMISSION_OK) {
        return rc;
    }

    if (trust->publisher_id == NULL || trust->spki_der == NULL || trust->spki_der_len == 0u) {
        return APP_ADMISSION_ERR_TRUST_UNPROVEN;
    }

    mbedtls_sha256(data, layout.sig_offset, tbs_sha, 0);
    mbedtls_sha256(data + layout.image_offset, layout.image_len, image_sha, 0);
    mbedtls_sha256(trust->spki_der, trust->spki_der_len, fp, 0);

    if (!ad_ct_equal(mf.publisher_key_sha256, fp, APP_ADMISSION_SHA256_LEN)) {
        return APP_ADMISSION_ERR_PUBLISHER_UNTRUSTED;
    }

    trust_pub_len = strlen(trust->publisher_id);
    if (trust_pub_len == 0u || trust_pub_len > APP_ADMISSION_PUBLISHER_ID_SLOT) {
        return APP_ADMISSION_ERR_TRUST_UNPROVEN;
    }
    memset(trust_pub, 0, sizeof(trust_pub));
    memcpy(trust_pub, trust->publisher_id, trust_pub_len);
    pkg_pub_len = ad_manifest_identifier_len(manifest + 4, APP_ADMISSION_PUBLISHER_ID_SLOT);
    if (pkg_pub_len != trust_pub_len ||
        !ad_ct_equal(manifest + 4, trust_pub, trust_pub_len)) {
        return APP_ADMISSION_ERR_PUBLISHER_UNTRUSTED;
    }

    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&q_pub);

    if (ad_parse_spki_p256(trust->spki_der, trust->spki_der_len, &grp, &q_pub) != 0) {
        mbedtls_ecp_point_free(&q_pub);
        mbedtls_ecp_group_free(&grp);
        return APP_ADMISSION_ERR_TRUST_UNPROVEN;
    }

    rc = ad_verify_signature(data, &layout, tbs_sha, &grp, &q_pub);

    mbedtls_ecp_point_free(&q_pub);
    mbedtls_ecp_group_free(&grp);

    if (rc != APP_ADMISSION_OK) {
        return rc;
    }

    img = data + layout.image_offset;
    hdr = (const app_host_image_header_t *)(const void *)img;

    if ((uint64_t)mf.native_file_len != (uint64_t)layout.image_len) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    if ((uint64_t)mf.native_file_len !=
        (uint64_t)APP_ADMISSION_NATIVE_HEADER_SIZE + (uint64_t)mf.native_image_size) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    if (hdr->magic != APP_ADMISSION_NATIVE_HEADER_MAGIC ||
        hdr->header_size != APP_ADMISSION_NATIVE_HEADER_SIZE ||
        hdr->format_version != APP_HOST_IMAGE_FORMAT_VERSION ||
        hdr->reserved0 != 0u) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    if (hdr->image_size != mf.native_image_size ||
        hdr->entry_offset != mf.native_entry_offset ||
        hdr->target_addr != mf.native_target_addr ||
        hdr->abi_version != mf.required_host_abi) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    if ((uint64_t)mf.native_entry_offset >= (uint64_t)mf.native_image_size ||
        (mf.native_entry_offset & APP_HOST_ENTRY_THUMB_MASK) != 0u) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    if (!ad_ct_equal(mf.native_file_sha256, image_sha, APP_ADMISSION_SHA256_LEN)) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    if (generic_crc32(img + APP_ADMISSION_NATIVE_HEADER_SIZE,
                      (size_t)mf.native_image_size) != hdr->crc32) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    if (mf.target_board_id != policy->target_board_id) {
        return APP_ADMISSION_ERR_TARGET_INCOMPATIBLE;
    }
    if ((uint64_t)mf.required_psram_mib > (uint64_t)policy->psram_mib) {
        return APP_ADMISSION_ERR_TARGET_INCOMPATIBLE;
    }
    if (mf.native_target_addr != policy->exec_region_base) {
        return APP_ADMISSION_ERR_TARGET_INCOMPATIBLE;
    }

    if (mf.required_host_abi != policy->abi_version) {
        return APP_ADMISSION_ERR_ABI_INCOMPATIBLE;
    }
    if ((mf.required_host_caps & ~policy->supported_caps_mask) != 0u) {
        return APP_ADMISSION_ERR_ABI_INCOMPATIBLE;
    }

    if ((uint64_t)mf.native_file_len > (uint64_t)policy->exec_region_size) {
        return APP_ADMISSION_ERR_RESOURCE_LIMIT;
    }
    if ((uint64_t)mf.required_exec_region_bytes < (uint64_t)mf.native_image_size ||
        (uint64_t)mf.required_exec_region_bytes > (uint64_t)policy->exec_region_size) {
        return APP_ADMISSION_ERR_RESOURCE_LIMIT;
    }

    {
        size_t pub_len = ad_manifest_identifier_len(manifest + 4, APP_ADMISSION_PUBLISHER_ID_SLOT);
        size_t app_len = ad_manifest_identifier_len(manifest + 20, APP_ADMISSION_APP_ID_SLOT);

        memcpy(out->publisher_id, manifest + 4, pub_len);
        out->publisher_id[pub_len] = '\0';
        memcpy(out->app_id, manifest + 20, app_len);
        out->app_id[app_len] = '\0';

        out->version_major = mf.version_major;
        out->version_minor = mf.version_minor;
        out->version_patch = mf.version_patch;
        out->required_psram_mib = mf.required_psram_mib;
        out->required_host_abi = mf.required_host_abi;
        out->required_host_caps = mf.required_host_caps;
        out->required_exec_region_bytes = mf.required_exec_region_bytes;
        out->native_image_size = mf.native_image_size;
        out->native_entry_offset = mf.native_entry_offset;
        out->native_target_addr = mf.native_target_addr;
        out->tbs_len = layout.sig_offset;
        out->signature_len = layout.sig_len;
        memcpy(out->tbs_sha256, tbs_sha, APP_ADMISSION_SHA256_LEN);
        memcpy(out->image_sha256, image_sha, APP_ADMISSION_SHA256_LEN);
        memcpy(out->publisher_key_sha256, mf.publisher_key_sha256, APP_ADMISSION_SHA256_LEN);
    }

    return APP_ADMISSION_OK;
}

static app_admission_result_t ad_verify_v2(const uint8_t *data,
                                           size_t len,
                                           const app_admission_policy_t *policy,
                                           const app_admission_trust_view_t *trust,
                                           app_admission_verdict_t *out)
{
    app_admission_layout_t layout;
    app_admission_manifest_fields_t mf;
    const uint8_t *manifest;
    const uint8_t *img;
    const app_host_image_header_t *hdr;
    uint32_t run_profile = 0u;
    uint32_t event_max = 0u;
    uint32_t state_quota = 0u;
    uint32_t report_max = 0u;
    uint8_t tbs_sha[APP_ADMISSION_SHA256_LEN];
    uint8_t image_sha[APP_ADMISSION_SHA256_LEN];
    uint8_t fp[APP_ADMISSION_SHA256_LEN];
    uint8_t trust_pub[APP_ADMISSION_PUBLISHER_ID_SLOT];
    size_t trust_pub_len;
    size_t pkg_pub_len;
    mbedtls_ecp_group grp;
    mbedtls_ecp_point q_pub;
    app_admission_result_t rc;

    rc = ad_parse_layout_v2(data, len, policy->max_package_len, &layout);
    if (rc != APP_ADMISSION_OK) {
        return rc;
    }

    manifest = data + APP_ADMISSION_MANIFEST_OFFSET;
    rc = ad_parse_manifest_v2(manifest, &mf, &run_profile, &event_max, &state_quota,
                              &report_max);
    if (rc != APP_ADMISSION_OK) {
        return rc;
    }

    if (trust->publisher_id == NULL || trust->spki_der == NULL || trust->spki_der_len == 0u) {
        return APP_ADMISSION_ERR_TRUST_UNPROVEN;
    }

    mbedtls_sha256(data, layout.sig_offset, tbs_sha, 0);
    mbedtls_sha256(data + layout.image_offset, layout.image_len, image_sha, 0);
    mbedtls_sha256(trust->spki_der, trust->spki_der_len, fp, 0);

    if (!ad_ct_equal(mf.publisher_key_sha256, fp, APP_ADMISSION_SHA256_LEN)) {
        return APP_ADMISSION_ERR_PUBLISHER_UNTRUSTED;
    }

    trust_pub_len = strlen(trust->publisher_id);
    if (trust_pub_len == 0u || trust_pub_len > APP_ADMISSION_PUBLISHER_ID_SLOT) {
        return APP_ADMISSION_ERR_TRUST_UNPROVEN;
    }
    memset(trust_pub, 0, sizeof(trust_pub));
    memcpy(trust_pub, trust->publisher_id, trust_pub_len);
    pkg_pub_len = ad_manifest_identifier_len(manifest + 4, APP_ADMISSION_PUBLISHER_ID_SLOT);
    if (pkg_pub_len != trust_pub_len ||
        !ad_ct_equal(manifest + 4, trust_pub, trust_pub_len)) {
        return APP_ADMISSION_ERR_PUBLISHER_UNTRUSTED;
    }

    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&q_pub);

    if (ad_parse_spki_p256(trust->spki_der, trust->spki_der_len, &grp, &q_pub) != 0) {
        mbedtls_ecp_point_free(&q_pub);
        mbedtls_ecp_group_free(&grp);
        return APP_ADMISSION_ERR_TRUST_UNPROVEN;
    }

    rc = ad_verify_signature(data, &layout, tbs_sha, &grp, &q_pub);

    mbedtls_ecp_point_free(&q_pub);
    mbedtls_ecp_group_free(&grp);

    if (rc != APP_ADMISSION_OK) {
        return rc;
    }

    img = data + layout.image_offset;
    hdr = (const app_host_image_header_t *)(const void *)img;

    if ((uint64_t)mf.native_file_len != (uint64_t)layout.image_len) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    if ((uint64_t)mf.native_file_len !=
        (uint64_t)APP_ADMISSION_NATIVE_HEADER_SIZE + (uint64_t)mf.native_image_size) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    if (hdr->magic != APP_ADMISSION_NATIVE_HEADER_MAGIC ||
        hdr->header_size != APP_ADMISSION_NATIVE_HEADER_SIZE ||
        hdr->format_version != APP_HOST_IMAGE_FORMAT_VERSION ||
        hdr->reserved0 != 0u) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    if (hdr->image_size != mf.native_image_size ||
        hdr->entry_offset != mf.native_entry_offset ||
        hdr->target_addr != mf.native_target_addr ||
        hdr->abi_version != mf.required_host_abi) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    if ((uint64_t)mf.native_entry_offset >= (uint64_t)mf.native_image_size ||
        (mf.native_entry_offset & APP_HOST_ENTRY_THUMB_MASK) != 0u) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    if (!ad_ct_equal(mf.native_file_sha256, image_sha, APP_ADMISSION_SHA256_LEN)) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    if (generic_crc32(img + APP_ADMISSION_NATIVE_HEADER_SIZE,
                      (size_t)mf.native_image_size) != hdr->crc32) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    if (mf.target_board_id != policy->target_board_id) {
        return APP_ADMISSION_ERR_TARGET_INCOMPATIBLE;
    }
    if ((uint64_t)mf.required_psram_mib > (uint64_t)policy->psram_mib) {
        return APP_ADMISSION_ERR_TARGET_INCOMPATIBLE;
    }
    if (mf.native_target_addr != policy->exec_region_base) {
        return APP_ADMISSION_ERR_TARGET_INCOMPATIBLE;
    }

    if (mf.required_host_abi != policy->abi_version) {
        return APP_ADMISSION_ERR_ABI_INCOMPATIBLE;
    }
    if ((mf.required_host_caps & ~policy->supported_caps_mask) != 0u) {
        return APP_ADMISSION_ERR_ABI_INCOMPATIBLE;
    }

    if ((uint64_t)mf.native_file_len > (uint64_t)policy->exec_region_size) {
        return APP_ADMISSION_ERR_RESOURCE_LIMIT;
    }
    if ((uint64_t)mf.required_exec_region_bytes < (uint64_t)mf.native_image_size ||
        (uint64_t)mf.required_exec_region_bytes > (uint64_t)policy->exec_region_size) {
        return APP_ADMISSION_ERR_RESOURCE_LIMIT;
    }

    if ((mf.required_host_caps & APP_ADMISSION_V2_CAP_AI_EVENTS) != 0u) {
        if ((uint64_t)event_max < (uint64_t)APP_ADMISSION_V2_MIN_EVENT_MAX ||
            (uint64_t)event_max > (uint64_t)policy->event_capacity) {
            return APP_ADMISSION_ERR_RESOURCE_LIMIT;
        }
    }
    if ((mf.required_host_caps & APP_ADMISSION_V2_CAP_REPORT_SUBMIT) != 0u) {
        if ((uint64_t)report_max < (uint64_t)APP_ADMISSION_V2_MIN_REPORT_MAX ||
            (uint64_t)report_max > (uint64_t)policy->report_capacity) {
            return APP_ADMISSION_ERR_RESOURCE_LIMIT;
        }
    }
    if ((mf.required_host_caps & APP_ADMISSION_V2_CAP_APP_STATE) != 0u) {
        if ((uint64_t)state_quota > (uint64_t)policy->state_quota_capacity) {
            return APP_ADMISSION_ERR_RESOURCE_LIMIT;
        }
    }

    {
        size_t pub_len = ad_manifest_identifier_len(manifest + 4, APP_ADMISSION_PUBLISHER_ID_SLOT);
        size_t app_len = ad_manifest_identifier_len(manifest + 20, APP_ADMISSION_APP_ID_SLOT);

        memcpy(out->publisher_id, manifest + 4, pub_len);
        out->publisher_id[pub_len] = '\0';
        memcpy(out->app_id, manifest + 20, app_len);
        out->app_id[app_len] = '\0';

        out->version_major = mf.version_major;
        out->version_minor = mf.version_minor;
        out->version_patch = mf.version_patch;
        out->required_psram_mib = mf.required_psram_mib;
        out->required_host_abi = mf.required_host_abi;
        out->required_host_caps = mf.required_host_caps;
        out->required_exec_region_bytes = mf.required_exec_region_bytes;
        out->native_image_size = mf.native_image_size;
        out->native_entry_offset = mf.native_entry_offset;
        out->native_target_addr = mf.native_target_addr;
        out->run_profile = run_profile;
        out->event_max = event_max;
        out->state_quota = state_quota;
        out->report_max = report_max;
        out->tbs_len = layout.sig_offset;
        out->signature_len = layout.sig_len;
        memcpy(out->tbs_sha256, tbs_sha, APP_ADMISSION_SHA256_LEN);
        memcpy(out->image_sha256, image_sha, APP_ADMISSION_SHA256_LEN);
        memcpy(out->publisher_key_sha256, mf.publisher_key_sha256, APP_ADMISSION_SHA256_LEN);
    }

    return APP_ADMISSION_OK;
}

static app_admission_result_t ad_verify_v3(const uint8_t *data,
                                           size_t len,
                                           const app_admission_policy_t *policy,
                                           const app_admission_trust_view_t *trust,
                                           app_admission_verdict_t *out)
{
    app_admission_layout_t layout;
    app_admission_manifest_fields_t mf;
    const uint8_t *manifest;
    const uint8_t *img;
    const app_host_image_header_t *hdr;
    uint32_t run_profile = 0u;
    uint32_t event_max = 0u;
    uint32_t state_quota = 0u;
    uint32_t report_max = 0u;
    uint32_t control_max = 0u;
    uint32_t snapshot_max = 0u;
    uint8_t tbs_sha[APP_ADMISSION_SHA256_LEN];
    uint8_t image_sha[APP_ADMISSION_SHA256_LEN];
    uint8_t fp[APP_ADMISSION_SHA256_LEN];
    uint8_t trust_pub[APP_ADMISSION_PUBLISHER_ID_SLOT];
    size_t trust_pub_len;
    size_t pkg_pub_len;
    mbedtls_ecp_group grp;
    mbedtls_ecp_point q_pub;
    app_admission_result_t rc;

    if (policy->v3_business_profile != APP_ADMISSION_V3_BUSINESS_NONE &&
        policy->v3_business_profile != APP_ADMISSION_V3_BUSINESS_LINE_CROSSING_64 &&
        policy->v3_business_profile != APP_ADMISSION_V3_BUSINESS_GENERIC_FULL) {
        return APP_ADMISSION_ERR_PARAM;
    }

    rc = ad_parse_layout_v3(data, len, policy->max_package_len, &layout);
    if (rc != APP_ADMISSION_OK) {
        return rc;
    }

    manifest = data + APP_ADMISSION_MANIFEST_OFFSET;
    rc = ad_parse_manifest_v3(manifest, &mf, &run_profile, &event_max, &state_quota,
                              &report_max, &control_max, &snapshot_max);
    if (rc != APP_ADMISSION_OK) {
        return rc;
    }

    if (ad_validate_der_signature(data + layout.sig_offset, layout.sig_len) != 0) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    if (trust->publisher_id == NULL || trust->spki_der == NULL || trust->spki_der_len == 0u) {
        return APP_ADMISSION_ERR_TRUST_UNPROVEN;
    }

    mbedtls_sha256(data, layout.sig_offset, tbs_sha, 0);
    mbedtls_sha256(data + layout.image_offset, layout.image_len, image_sha, 0);
    mbedtls_sha256(trust->spki_der, trust->spki_der_len, fp, 0);

    if (!ad_ct_equal(mf.publisher_key_sha256, fp, APP_ADMISSION_SHA256_LEN)) {
        return APP_ADMISSION_ERR_PUBLISHER_UNTRUSTED;
    }

    trust_pub_len = trust->publisher_id_len;
    if (trust_pub_len == 0u || trust_pub_len > APP_ADMISSION_PUBLISHER_ID_SLOT) {
        return APP_ADMISSION_ERR_TRUST_UNPROVEN;
    }
    memset(trust_pub, 0, sizeof(trust_pub));
    memcpy(trust_pub, trust->publisher_id, trust_pub_len);
    pkg_pub_len = ad_manifest_identifier_len(manifest + 4, APP_ADMISSION_PUBLISHER_ID_SLOT);
    if (pkg_pub_len != trust_pub_len ||
        !ad_ct_equal(manifest + 4, trust_pub, trust_pub_len)) {
        return APP_ADMISSION_ERR_PUBLISHER_UNTRUSTED;
    }

    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&q_pub);

    if (ad_parse_spki_p256(trust->spki_der, trust->spki_der_len, &grp, &q_pub) != 0) {
        mbedtls_ecp_point_free(&q_pub);
        mbedtls_ecp_group_free(&grp);
        return APP_ADMISSION_ERR_TRUST_UNPROVEN;
    }

    rc = ad_verify_signature(data, &layout, tbs_sha, &grp, &q_pub);

    mbedtls_ecp_point_free(&q_pub);
    mbedtls_ecp_group_free(&grp);

    if (rc != APP_ADMISSION_OK) {
        return rc;
    }

    img = data + layout.image_offset;
    hdr = (const app_host_image_header_t *)(const void *)img;

    if ((uint64_t)mf.native_file_len != (uint64_t)layout.image_len ||
        (uint64_t)mf.native_file_len !=
            (uint64_t)APP_ADMISSION_NATIVE_HEADER_SIZE + (uint64_t)mf.native_image_size) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    if (hdr->magic != APP_ADMISSION_NATIVE_HEADER_MAGIC ||
        hdr->header_size != APP_ADMISSION_NATIVE_HEADER_SIZE ||
        hdr->format_version != APP_HOST_IMAGE_FORMAT_VERSION || hdr->reserved0 != 0u) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    if (hdr->image_size != mf.native_image_size ||
        hdr->entry_offset != mf.native_entry_offset ||
        hdr->target_addr != mf.native_target_addr ||
        hdr->abi_version != APP_ADMISSION_ABI_VERSION_V3 ||
        hdr->abi_version != mf.required_host_abi) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    if ((uint64_t)mf.native_entry_offset >= (uint64_t)mf.native_image_size ||
        (mf.native_entry_offset & APP_HOST_ENTRY_THUMB_MASK) != 0u) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    if (!ad_ct_equal(mf.native_file_sha256, image_sha, APP_ADMISSION_SHA256_LEN) ||
        generic_crc32(img + APP_ADMISSION_NATIVE_HEADER_SIZE,
                      (size_t)mf.native_image_size) != hdr->crc32) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    if (control_max < APP_ADMISSION_V3_CONTROL_WIRE_MIN) {
        return APP_ADMISSION_ERR_RESOURCE_LIMIT;
    }

    if (mf.target_board_id != policy->target_board_id ||
        (uint64_t)mf.required_psram_mib > (uint64_t)policy->psram_mib ||
        mf.native_target_addr != policy->exec_region_base) {
        return APP_ADMISSION_ERR_TARGET_INCOMPATIBLE;
    }

    if (mf.required_host_abi != policy->abi_version ||
        policy->abi_table_size != APP_ADMISSION_V3_ABI_TABLE_SIZE ||
        (mf.required_host_caps & ~policy->supported_caps_mask) != 0u) {
        return APP_ADMISSION_ERR_ABI_INCOMPATIBLE;
    }

    if (policy->v3_business_profile == APP_ADMISSION_V3_BUSINESS_NONE) {
        uint32_t business_caps = APP_ADMISSION_V2_CAP_AI_EVENTS |
                                 APP_ADMISSION_V2_CAP_REPORT_SUBMIT |
                                 APP_ADMISSION_V2_CAP_APP_STATE |
                                 APP_ADMISSION_V3_CAP_BUSINESS_SNAPSHOT;
        if ((mf.required_host_caps & business_caps) != 0u) {
            return APP_ADMISSION_ERR_ABI_INCOMPATIBLE;
        }
    }

    if ((uint64_t)mf.native_file_len > (uint64_t)policy->exec_region_size ||
        (uint64_t)mf.required_exec_region_bytes < (uint64_t)mf.native_image_size ||
        (uint64_t)mf.required_exec_region_bytes > (uint64_t)policy->exec_region_size) {
        return APP_ADMISSION_ERR_RESOURCE_LIMIT;
    }

    if (policy->v3_business_profile == APP_ADMISSION_V3_BUSINESS_LINE_CROSSING_64) {
        uint32_t required_caps = APP_ADMISSION_V2_CAP_AI_EVENTS |
                                 APP_ADMISSION_V2_CAP_REPORT_SUBMIT |
                                 APP_ADMISSION_V2_CAP_APP_STATE |
                                 APP_ADMISSION_V2_CAP_SESSION_LIFECYCLE |
                                 APP_ADMISSION_V3_CAP_CONTROL_RECEIVE |
                                 APP_ADMISSION_V3_CAP_BUSINESS_SNAPSHOT;
        if ((mf.required_host_caps & required_caps) != required_caps) {
            return APP_ADMISSION_ERR_BAD_PACKAGE;
        }
        if (event_max < APP_ADMISSION_V3_EVENT_MAX ||
            report_max < APP_ADMISSION_V3_REPORT_MAX) {
            return APP_ADMISSION_ERR_RESOURCE_LIMIT;
        }
    }

    if (((mf.required_host_caps & APP_ADMISSION_V2_CAP_AI_EVENTS) != 0u &&
         (uint64_t)event_max > (uint64_t)policy->event_capacity) ||
        ((mf.required_host_caps & APP_ADMISSION_V2_CAP_REPORT_SUBMIT) != 0u &&
         (uint64_t)report_max > (uint64_t)policy->report_capacity) ||
        ((mf.required_host_caps & APP_ADMISSION_V2_CAP_APP_STATE) != 0u &&
         (uint64_t)state_quota > (uint64_t)policy->state_quota_capacity) ||
        ((mf.required_host_caps & APP_ADMISSION_V3_CAP_CONTROL_RECEIVE) != 0u &&
         (uint64_t)control_max > (uint64_t)policy->control_capacity) ||
        ((mf.required_host_caps & APP_ADMISSION_V3_CAP_BUSINESS_SNAPSHOT) != 0u &&
         (uint64_t)snapshot_max > (uint64_t)policy->snapshot_capacity)) {
        return APP_ADMISSION_ERR_RESOURCE_LIMIT;
    }

    {
        size_t pub_len = ad_manifest_identifier_len(manifest + 4, APP_ADMISSION_PUBLISHER_ID_SLOT);
        size_t app_len = ad_manifest_identifier_len(manifest + 20, APP_ADMISSION_APP_ID_SLOT);

        memcpy(out->publisher_id, manifest + 4, pub_len);
        out->publisher_id[pub_len] = '\0';
        memcpy(out->app_id, manifest + 20, app_len);
        out->app_id[app_len] = '\0';

        out->version_major = mf.version_major;
        out->version_minor = mf.version_minor;
        out->version_patch = mf.version_patch;
        out->required_psram_mib = mf.required_psram_mib;
        out->required_host_abi = mf.required_host_abi;
        out->required_host_caps = mf.required_host_caps;
        out->required_exec_region_bytes = mf.required_exec_region_bytes;
        out->native_image_size = mf.native_image_size;
        out->native_entry_offset = mf.native_entry_offset;
        out->native_target_addr = mf.native_target_addr;
        out->run_profile = run_profile;
        out->event_max = event_max;
        out->state_quota = state_quota;
        out->report_max = report_max;
        out->control_max = control_max;
        out->snapshot_max = snapshot_max;
        out->tbs_len = layout.sig_offset;
        out->signature_len = layout.sig_len;
        memcpy(out->tbs_sha256, tbs_sha, APP_ADMISSION_SHA256_LEN);
        memcpy(out->image_sha256, image_sha, APP_ADMISSION_SHA256_LEN);
        memcpy(out->publisher_key_sha256, mf.publisher_key_sha256, APP_ADMISSION_SHA256_LEN);
    }

    return APP_ADMISSION_OK;
}

static app_admission_result_t ad_verify_v4(const uint8_t *data,
                                           size_t len,
                                           const app_admission_policy_t *policy,
                                           const app_admission_trust_view_t *trust,
                                           app_admission_verdict_t *out)
{
    app_admission_layout_t layout;
    app_admission_manifest_fields_t mf;
    const uint8_t *manifest;
    const uint8_t *img;
    const app_host_image_header_t *hdr;
    uint32_t run_profile = 0u;
    uint32_t event_max = 0u;
    uint32_t state_quota = 0u;
    uint32_t report_max = 0u;
    uint32_t control_max = 0u;
    uint32_t snapshot_max = 0u;
    uint32_t live_publish_max = 0u;
    uint32_t live_track_max = 0u;
    uint8_t tbs_sha[APP_ADMISSION_SHA256_LEN];
    uint8_t image_sha[APP_ADMISSION_SHA256_LEN];
    uint8_t fp[APP_ADMISSION_SHA256_LEN];
    uint8_t trust_pub[APP_ADMISSION_PUBLISHER_ID_SLOT];
    size_t trust_pub_len;
    size_t pkg_pub_len;
    mbedtls_ecp_group grp;
    mbedtls_ecp_point q_pub;
    app_admission_result_t rc;
    uint32_t caps;

    rc = ad_parse_layout_v4(data, len, policy->max_package_len, &layout);
    if (rc != APP_ADMISSION_OK) {
        return rc;
    }
    manifest = data + APP_ADMISSION_CONTAINER_HEADER_SIZE;
    rc = ad_parse_manifest_v4(manifest, &mf, &run_profile, &event_max,
                              &state_quota, &report_max, &control_max,
                              &snapshot_max, &live_publish_max, &live_track_max);
    if (rc != APP_ADMISSION_OK) {
        return rc;
    }
    if (ad_validate_der_signature(data + layout.sig_offset, layout.sig_len) != 0) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    if (trust->publisher_id == NULL || trust->spki_der == NULL ||
        trust->spki_der_len == 0u) {
        return APP_ADMISSION_ERR_TRUST_UNPROVEN;
    }
    mbedtls_sha256(data, layout.sig_offset, tbs_sha, 0);
    mbedtls_sha256(data + layout.image_offset, layout.image_len, image_sha, 0);
    mbedtls_sha256(trust->spki_der, trust->spki_der_len, fp, 0);
    if (!ad_ct_equal(mf.publisher_key_sha256, fp, APP_ADMISSION_SHA256_LEN)) {
        return APP_ADMISSION_ERR_PUBLISHER_UNTRUSTED;
    }
    trust_pub_len = trust->publisher_id_len;
    if (trust_pub_len == 0u || trust_pub_len > APP_ADMISSION_PUBLISHER_ID_SLOT) {
        return APP_ADMISSION_ERR_TRUST_UNPROVEN;
    }
    memset(trust_pub, 0, sizeof(trust_pub));
    memcpy(trust_pub, trust->publisher_id, trust_pub_len);
    pkg_pub_len = ad_manifest_identifier_len(manifest + 4u,
                                            APP_ADMISSION_PUBLISHER_ID_SLOT);
    if (pkg_pub_len != trust_pub_len ||
        !ad_ct_equal(manifest + 4u, trust_pub, trust_pub_len)) {
        return APP_ADMISSION_ERR_PUBLISHER_UNTRUSTED;
    }
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&q_pub);
    if (ad_parse_spki_p256(trust->spki_der, trust->spki_der_len, &grp, &q_pub) != 0) {
        mbedtls_ecp_point_free(&q_pub);
        mbedtls_ecp_group_free(&grp);
        return APP_ADMISSION_ERR_TRUST_UNPROVEN;
    }
    rc = ad_verify_signature(data, &layout, tbs_sha, &grp, &q_pub);
    mbedtls_ecp_point_free(&q_pub);
    mbedtls_ecp_group_free(&grp);
    if (rc != APP_ADMISSION_OK) {
        return rc;
    }
    img = data + layout.image_offset;
    hdr = (const app_host_image_header_t *)(const void *)img;
    if ((uint64_t)mf.native_file_len != (uint64_t)layout.image_len ||
        (uint64_t)mf.native_file_len !=
            (uint64_t)APP_ADMISSION_NATIVE_HEADER_SIZE + mf.native_image_size ||
        hdr->magic != APP_ADMISSION_NATIVE_HEADER_MAGIC ||
        hdr->header_size != APP_ADMISSION_NATIVE_HEADER_SIZE ||
        hdr->format_version != APP_HOST_IMAGE_FORMAT_VERSION || hdr->reserved0 != 0u ||
        hdr->image_size != mf.native_image_size ||
        hdr->entry_offset != mf.native_entry_offset ||
        hdr->target_addr != mf.native_target_addr ||
        hdr->abi_version != APP_ADMISSION_ABI_VERSION_V4 ||
        hdr->abi_version != mf.required_host_abi ||
        (uint64_t)mf.native_entry_offset >= mf.native_image_size ||
        (mf.native_entry_offset & APP_HOST_ENTRY_THUMB_MASK) != 0u ||
        !ad_ct_equal(mf.native_file_sha256, image_sha, APP_ADMISSION_SHA256_LEN) ||
        generic_crc32(img + APP_ADMISSION_NATIVE_HEADER_SIZE,
                      (size_t)mf.native_image_size) != hdr->crc32) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    if ((mf.required_host_caps & APP_ADMISSION_V3_CAP_CONTROL_RECEIVE) != 0u &&
        control_max < APP_ADMISSION_V3_CONTROL_WIRE_MIN) {
        return APP_ADMISSION_ERR_RESOURCE_LIMIT;
    }
    if (mf.target_board_id != policy->target_board_id ||
        (uint64_t)mf.required_psram_mib > policy->psram_mib ||
        mf.native_target_addr != policy->exec_region_base) {
        return APP_ADMISSION_ERR_TARGET_INCOMPATIBLE;
    }
    caps = mf.required_host_caps;
    if (mf.required_host_abi != policy->abi_version ||
        policy->abi_table_size != APP_ADMISSION_V4_ABI_TABLE_SIZE ||
        (caps & ~policy->supported_caps_mask) != 0u) {
        return APP_ADMISSION_ERR_ABI_INCOMPATIBLE;
    }
    if (policy->v3_business_profile == APP_ADMISSION_V3_BUSINESS_NONE) {
        uint32_t business_caps = APP_ADMISSION_V2_CAP_AI_EVENTS |
                                 APP_ADMISSION_V2_CAP_REPORT_SUBMIT |
                                 APP_ADMISSION_V2_CAP_APP_STATE |
                                 APP_ADMISSION_V3_CAP_BUSINESS_SNAPSHOT |
                                 APP_ADMISSION_V4_CAP_ENABLE_CONTROL |
                                 APP_ADMISSION_V4_CAP_LIVE_PUBLISH;
        if ((caps & business_caps) != 0u) {
            return APP_ADMISSION_ERR_ABI_INCOMPATIBLE;
        }
    }
    if ((uint64_t)mf.native_file_len > policy->exec_region_size ||
        (uint64_t)mf.required_exec_region_bytes < mf.native_image_size ||
        (uint64_t)mf.required_exec_region_bytes > policy->exec_region_size) {
        return APP_ADMISSION_ERR_RESOURCE_LIMIT;
    }
    if (policy->v3_business_profile == APP_ADMISSION_V3_BUSINESS_LINE_CROSSING_64) {
        uint32_t required_caps = APP_ADMISSION_V2_CAP_AI_EVENTS |
                                 APP_ADMISSION_V2_CAP_REPORT_SUBMIT |
                                 APP_ADMISSION_V2_CAP_APP_STATE |
                                 APP_ADMISSION_V2_CAP_SESSION_LIFECYCLE |
                                 APP_ADMISSION_V3_CAP_CONTROL_RECEIVE |
                                 APP_ADMISSION_V3_CAP_BUSINESS_SNAPSHOT;
        if ((caps & required_caps) != required_caps) {
            return APP_ADMISSION_ERR_BAD_PACKAGE;
        }
        if (event_max < APP_ADMISSION_V3_EVENT_MAX ||
            report_max < APP_ADMISSION_V3_REPORT_MAX) {
            return APP_ADMISSION_ERR_RESOURCE_LIMIT;
        }
    }
    if (((caps & APP_ADMISSION_V2_CAP_AI_EVENTS) != 0u && event_max > policy->event_capacity) ||
        ((caps & APP_ADMISSION_V2_CAP_REPORT_SUBMIT) != 0u && report_max > policy->report_capacity) ||
        ((caps & APP_ADMISSION_V2_CAP_APP_STATE) != 0u && state_quota > policy->state_quota_capacity) ||
        ((caps & APP_ADMISSION_V3_CAP_CONTROL_RECEIVE) != 0u && control_max > policy->control_capacity) ||
        ((caps & APP_ADMISSION_V3_CAP_BUSINESS_SNAPSHOT) != 0u && snapshot_max > policy->snapshot_capacity) ||
        ((caps & APP_ADMISSION_V4_CAP_LIVE_PUBLISH) != 0u &&
         (live_publish_max > policy->live_publish_capacity ||
          live_track_max > policy->live_track_capacity))) {
        return APP_ADMISSION_ERR_RESOURCE_LIMIT;
    }
    {
        size_t pub_len = ad_manifest_identifier_len(manifest + 4u,
                                                    APP_ADMISSION_PUBLISHER_ID_SLOT);
        size_t app_len = ad_manifest_identifier_len(manifest + 20u,
                                                    APP_ADMISSION_APP_ID_SLOT);
        memcpy(out->publisher_id, manifest + 4u, pub_len);
        out->publisher_id[pub_len] = '\0';
        memcpy(out->app_id, manifest + 20u, app_len);
        out->app_id[app_len] = '\0';
        out->version_major = mf.version_major;
        out->version_minor = mf.version_minor;
        out->version_patch = mf.version_patch;
        out->required_psram_mib = mf.required_psram_mib;
        out->required_host_abi = mf.required_host_abi;
        out->required_host_caps = caps;
        out->required_exec_region_bytes = mf.required_exec_region_bytes;
        out->native_image_size = mf.native_image_size;
        out->native_entry_offset = mf.native_entry_offset;
        out->native_target_addr = mf.native_target_addr;
        out->run_profile = run_profile;
        out->event_max = event_max;
        out->state_quota = state_quota;
        out->report_max = report_max;
        out->control_max = control_max;
        out->snapshot_max = snapshot_max;
        out->live_publish_max = live_publish_max;
        out->live_track_max = live_track_max;
        out->tbs_len = layout.sig_offset;
        out->signature_len = layout.sig_len;
        memcpy(out->tbs_sha256, tbs_sha, APP_ADMISSION_SHA256_LEN);
        memcpy(out->image_sha256, image_sha, APP_ADMISSION_SHA256_LEN);
        memcpy(out->publisher_key_sha256, mf.publisher_key_sha256,
               APP_ADMISSION_SHA256_LEN);
    }
    return APP_ADMISSION_OK;
}

app_admission_result_t app_admission_verify_package(const uint8_t *data,
                                                    size_t len,
                                                    const app_admission_policy_t *policy,
                                                    const app_admission_trust_view_t *trust,
                                                    app_admission_verdict_t *out)
{
    if (data == NULL || policy == NULL || trust == NULL || out == NULL) {
        return APP_ADMISSION_ERR_PARAM;
    }

    memset(out, 0, sizeof(*out));

    if (len < (size_t)APP_ADMISSION_CONTAINER_HEADER_SIZE) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    {
        app_admission_result_t rc;
        uint32_t major = 0u;
        if (memcmp(data, g_ad_container_magic_v1, APP_ADMISSION_CONTAINER_MAGIC_SIZE) == 0) {
            major = 1u;
            rc = ad_verify_v1(data, len, policy, trust, out);
        } else if (memcmp(data, g_ad_container_magic_v2, APP_ADMISSION_CONTAINER_MAGIC_SIZE) == 0) {
            major = 2u;
            rc = ad_verify_v2(data, len, policy, trust, out);
        } else if (memcmp(data, g_ad_container_magic_v3, APP_ADMISSION_CONTAINER_MAGIC_SIZE) == 0) {
            major = 3u;
            rc = ad_verify_v3(data, len, policy, trust, out);
        } else if (memcmp(data, g_ad_container_magic_v4, APP_ADMISSION_CONTAINER_MAGIC_SIZE) == 0) {
            major = 4u;
            rc = ad_verify_v4(data, len, policy, trust, out);
        } else {
            return APP_ADMISSION_ERR_BAD_PACKAGE;
        }
        if (rc == APP_ADMISSION_OK) {
            out->container_major = major;
        }
        return rc;
    }
}

uint32_t app_admission_container_version(const uint8_t *data, size_t len)
{
    if (data == NULL || len < (size_t)APP_ADMISSION_CONTAINER_HEADER_SIZE) {
        return 0u;
    }

    if (memcmp(data, g_ad_container_magic_v1, APP_ADMISSION_CONTAINER_MAGIC_SIZE) == 0) {
        return 1u;
    }

    if (memcmp(data, g_ad_container_magic_v2, APP_ADMISSION_CONTAINER_MAGIC_SIZE) == 0) {
        return 2u;
    }

    if (memcmp(data, g_ad_container_magic_v3, APP_ADMISSION_CONTAINER_MAGIC_SIZE) == 0) {
        return 3u;
    }

    if (memcmp(data, g_ad_container_magic_v4, APP_ADMISSION_CONTAINER_MAGIC_SIZE) == 0) {
        return 4u;
    }

    return 0u;
}

const char *app_admission_result_str(app_admission_result_t result)
{
    switch (result) {
    case APP_ADMISSION_OK:
        return "trusted_compatible";
    case APP_ADMISSION_ERR_PARAM:
        return "PARAM";
    case APP_ADMISSION_ERR_BAD_PACKAGE:
        return "BAD_PACKAGE";
    case APP_ADMISSION_ERR_SIGNATURE_INVALID:
        return "SIGNATURE_INVALID";
    case APP_ADMISSION_ERR_PUBLISHER_UNTRUSTED:
        return "PUBLISHER_UNTRUSTED";
    case APP_ADMISSION_ERR_TARGET_INCOMPATIBLE:
        return "TARGET_INCOMPATIBLE";
    case APP_ADMISSION_ERR_ABI_INCOMPATIBLE:
        return "ABI_INCOMPATIBLE";
    case APP_ADMISSION_ERR_RESOURCE_LIMIT:
        return "RESOURCE_LIMIT";
    case APP_ADMISSION_ERR_TRUST_UNPROVEN:
        return "TRUST_UNPROVEN";
    default:
        return "UNKNOWN";
    }
}
