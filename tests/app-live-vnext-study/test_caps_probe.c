#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_admission.h"
#include "mbedtls/sha256.h"
#include "generic_math.h"

#define LV_BOARD_ID 0x00003010u
#define LV_EXEC_BASE 0x60000000u
#define LV_PUB "acme"
#define LV_APP "counting"

static int g_checks;
static int g_fails;

#define LV_CHECK(cond) \
    do { \
        g_checks++; \
        if (!(cond)) { \
            g_fails++; \
            printf("STUDY FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

#define LV_EXPECT(pkg, len, pol, trust, want) \
    do { \
        app_admission_verdict_t v; \
        app_admission_result_t r = app_admission_verify_package((pkg), (len), &(pol), &(trust), &v); \
        g_checks++; \
        if (r != (want)) { \
            g_fails++; \
            printf("STUDY FAIL expect %s got %s at %d\n", app_admission_result_str(want), \
                   app_admission_result_str(r), __LINE__); \
        } \
    } while (0)

static uint8_t g_spki[82];

static uint32_t entry_offset_ok(uint32_t file_len);
static const uint8_t *lv_spki_fp(void);

static void lv_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void lv_put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void lv_put_id(uint8_t *p, size_t slot, const char *id)
{
    size_t n = strlen(id);
    memset(p, 0, slot);
    memcpy(p, id, n);
}

static size_t lv_build_sig(uint8_t *sig)
{
    size_t i;
    sig[0] = 0x30u;
    sig[1] = 0x44u;
    sig[2] = 0x02u;
    sig[3] = 0x20u;
    for (i = 0; i < 32u; i++) {
        sig[4u + i] = (uint8_t)(0x11u + i);
    }
    sig[36u] = 0x02u;
    sig[37u] = 0x20u;
    for (i = 0; i < 32u; i++) {
        sig[38u + i] = (uint8_t)(0x21u + i);
    }
    return 70u;
}

static size_t lv_build_image(uint8_t *img, uint32_t abi, uint32_t image_payload,
                             uint32_t entry_offset)
{
    memset(img, 0xA5, 32u + image_payload);
    lv_put_u32(img + 0, 0x3141454Eu);
    lv_put_u16(img + 4, 32u);
    lv_put_u16(img + 6, 1u);
    lv_put_u32(img + 8, abi);
    lv_put_u32(img + 12, LV_EXEC_BASE);
    lv_put_u32(img + 16, image_payload);
    lv_put_u32(img + 20, entry_offset);
    lv_put_u32(img + 24, 0u);
    lv_put_u32(img + 28, generic_crc32(img + 32, image_payload));
    return 32u + image_payload;
}

static size_t lv_build_v3(uint8_t *pkg, uint32_t caps, uint32_t abi, uint32_t event_max,
                          uint32_t state_quota, uint32_t report_max, uint32_t control_max,
                          uint32_t snapshot_max, int reserved_byte_184)
{
    uint8_t *m = pkg + APP_ADMISSION_CONTAINER_HEADER_SIZE;
    uint8_t *img = pkg + APP_ADMISSION_IMAGE_OFFSET_V3;
    uint32_t file_len = (uint32_t)lv_build_image(img, abi, 128u, 16u);
    uint8_t image_sha[32];

    memset(pkg, 0, APP_ADMISSION_CONTAINER_HEADER_SIZE);
    pkg[0] = 0x4Eu;
    pkg[1] = 0x45u;
    pkg[2] = 0x41u;
    pkg[3] = 0x50u;
    pkg[4] = 0x50u;
    pkg[5] = 0x03u;
    lv_put_u32(pkg + 8, APP_ADMISSION_MANIFEST_SIZE_V3);
    lv_put_u32(pkg + 12, file_len);

    memset(m, 0, APP_ADMISSION_MANIFEST_SIZE_V3);
    memcpy(m + 0, "NMF3", 4);
    lv_put_id(m + 4, APP_ADMISSION_PUBLISHER_ID_SLOT, LV_PUB);
    lv_put_id(m + 20, APP_ADMISSION_APP_ID_SLOT, LV_APP);
    lv_put_u16(m + 52, 1u);
    lv_put_u16(m + 54, 2u);
    lv_put_u16(m + 56, 3u);
    lv_put_u32(m + 60, LV_BOARD_ID);
    lv_put_u32(m + 64, 32u);
    lv_put_u32(m + 68, abi);
    lv_put_u32(m + 72, caps);
    lv_put_u32(m + 76, 8192u);
    lv_put_u32(m + 80, file_len);
    lv_put_u32(m + 84, 128u);
    lv_put_u32(m + 88, entry_offset_ok(file_len));
    lv_put_u32(m + 92, LV_EXEC_BASE);
    mbedtls_sha256(img, file_len, image_sha, 0);
    memcpy(m + 96, image_sha, 32);
    memcpy(m + 128, lv_spki_fp(), 32);
    lv_put_u32(m + 160, APP_ADMISSION_V3_RUN_PROFILE_HOT_COMMAND_SESSION);
    lv_put_u32(m + 164, event_max);
    lv_put_u32(m + 168, state_quota);
    lv_put_u32(m + 172, report_max);
    lv_put_u32(m + 176, control_max);
    lv_put_u32(m + 180, snapshot_max);
    if (reserved_byte_184) {
        m[184] = 0x01u;
    }
    return APP_ADMISSION_IMAGE_OFFSET_V3 + file_len +
           lv_build_sig(pkg + APP_ADMISSION_IMAGE_OFFSET_V3 + file_len);
}

static uint32_t entry_offset_ok(uint32_t file_len)
{
    (void)file_len;
    return 16u;
}

static const uint8_t *lv_spki_fp(void)
{
    static uint8_t fp[32];
    static int done;
    if (!done) {
        memset(g_spki, 0, sizeof(g_spki));
        g_spki[2] = 0x2Au;
        g_spki[3] = 0x86u;
        g_spki[4] = 0x48u;
        g_spki[5] = 0xCEu;
        g_spki[6] = 0x3Du;
        g_spki[7] = 0x02u;
        g_spki[8] = 0x01u;
        g_spki[9] = 0x2Au;
        g_spki[10] = 0x86u;
        g_spki[11] = 0x48u;
        g_spki[12] = 0xCEu;
        g_spki[13] = 0x3Du;
        g_spki[14] = 0x03u;
        g_spki[15] = 0x01u;
        g_spki[16] = 0x07u;
        g_spki[17] = 0x04u;
        mbedtls_sha256(g_spki, sizeof(g_spki), fp, 0);
        done = 1;
    }
    return fp;
}

static void lv_policy_default(app_admission_policy_t *p)
{
    memset(p, 0, sizeof(*p));
    p->target_board_id = LV_BOARD_ID;
    p->psram_mib = 32u;
    p->abi_version = APP_ADMISSION_ABI_VERSION_V3;
    p->supported_caps_mask = APP_ADMISSION_V3_KNOWN_CAPS_MASK;
    p->exec_region_base = LV_EXEC_BASE;
    p->exec_region_size = 2097152u;
    p->event_capacity = 4096u;
    p->report_capacity = 8192u;
    p->state_quota_capacity = 8192u;
    p->max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN_V3;
    p->abi_table_size = APP_ADMISSION_V3_ABI_TABLE_SIZE;
    p->control_capacity = 512u;
    p->snapshot_capacity = 2048u;
    p->v3_business_profile = APP_ADMISSION_V3_BUSINESS_LINE_CROSSING_64;
}

static void lv_trust_default(app_admission_trust_view_t *t)
{
    t->publisher_id = LV_PUB;
    t->publisher_id_len = strlen(LV_PUB);
    t->spki_der = g_spki;
    t->spki_der_len = sizeof(g_spki);
}

static void lv_caps_enumeration(void)
{
    uint32_t bits = APP_ADMISSION_CAP_LOG | APP_ADMISSION_CAP_TICK_MS |
                    APP_ADMISSION_V2_CAP_AI_EVENTS | APP_ADMISSION_V2_CAP_REPORT_SUBMIT |
                    APP_ADMISSION_V2_CAP_APP_STATE | APP_ADMISSION_V2_CAP_SESSION_LIFECYCLE |
                    APP_ADMISSION_V3_CAP_CONTROL_RECEIVE | APP_ADMISSION_V3_CAP_BUSINESS_SNAPSHOT;
    LV_CHECK(bits == APP_ADMISSION_V3_KNOWN_CAPS_MASK);
    LV_CHECK(APP_ADMISSION_V3_KNOWN_CAPS_MASK == 0x000000FFu);
    LV_CHECK((APP_ADMISSION_V3_KNOWN_CAPS_MASK & 0xFFFFFF00u) == 0u);
    LV_CHECK(APP_ADMISSION_ABI_VERSION_V3 == 0x00030000u);
    LV_CHECK(APP_ADMISSION_V3_ABI_TABLE_SIZE == 60u);
    LV_CHECK(APP_ADMISSION_MANIFEST_SIZE_V3 == 192u);
    LV_CHECK(APP_ADMISSION_V3_EVENT_MAX == 1576u);
    LV_CHECK(APP_ADMISSION_V3_REPORT_MAX == 6144u);
    printf("STUDY ac1 v3_known_caps_mask=0x%08X abi=0x%08X table=%u manifest=%u event_max=%u report_max=%u\n",
           APP_ADMISSION_V3_KNOWN_CAPS_MASK, APP_ADMISSION_ABI_VERSION_V3,
           APP_ADMISSION_V3_ABI_TABLE_SIZE, APP_ADMISSION_MANIFEST_SIZE_V3,
           APP_ADMISSION_V3_EVENT_MAX, APP_ADMISSION_V3_REPORT_MAX);
}

static void lv_v3_matrix(void)
{
    static uint8_t pkg[APP_ADMISSION_MAX_PACKAGE_LEN_V3];
    app_admission_policy_t pol;
    app_admission_trust_view_t trust;
    app_admission_verdict_t v;
    size_t len;
    uint32_t caps_all = APP_ADMISSION_V3_KNOWN_CAPS_MASK;

    lv_policy_default(&pol);
    lv_trust_default(&trust);

    len = lv_build_v3(pkg, caps_all, APP_ADMISSION_ABI_VERSION_V3, 1576u, 8192u, 6144u,
                      512u, 2048u, 0);
    LV_EXPECT(pkg, len, pol, trust, APP_ADMISSION_OK);
    {
        app_admission_result_t r = app_admission_verify_package(pkg, len, &pol, &trust, &v);
        LV_CHECK(r == APP_ADMISSION_OK);
        LV_CHECK(v.run_profile == APP_ADMISSION_V3_RUN_PROFILE_HOT_COMMAND_SESSION);
        LV_CHECK(v.required_host_abi == APP_ADMISSION_ABI_VERSION_V3);
        LV_CHECK(v.required_host_caps == caps_all);
        LV_CHECK(v.control_max == 512u);
        LV_CHECK(v.snapshot_max == 2048u);
        LV_CHECK(v.event_max == 1576u);
        LV_CHECK(strcmp(v.publisher_id, LV_PUB) == 0);
        printf("STUDY ac1 v3_baseline_ok caps=0x%08X event_max=%u control=%u snapshot=%u\n",
               v.required_host_caps, v.event_max, v.control_max, v.snapshot_max);
    }

    lv_policy_default(&pol);
    len = lv_build_v3(pkg, caps_all | 0x00000100u, APP_ADMISSION_ABI_VERSION_V3, 1576u, 8192u,
                      6144u, 512u, 2048u, 0);
    LV_EXPECT(pkg, len, pol, trust, APP_ADMISSION_ERR_BAD_PACKAGE);

    lv_policy_default(&pol);
    len = lv_build_v3(pkg, caps_all | 0x80000000u, APP_ADMISSION_ABI_VERSION_V3, 1576u, 8192u,
                      6144u, 512u, 2048u, 0);
    LV_EXPECT(pkg, len, pol, trust, APP_ADMISSION_ERR_BAD_PACKAGE);

    lv_policy_default(&pol);
    len = lv_build_v3(pkg, caps_all, 0x00040000u, 1576u, 8192u, 6144u, 512u, 2048u, 0);
    LV_EXPECT(pkg, len, pol, trust, APP_ADMISSION_ERR_BAD_PACKAGE);

    lv_policy_default(&pol);
    len = lv_build_v3(pkg, caps_all, APP_ADMISSION_ABI_VERSION_V2, 1576u, 8192u, 6144u, 512u,
                      2048u, 0);
    LV_EXPECT(pkg, len, pol, trust, APP_ADMISSION_ERR_BAD_PACKAGE);

    lv_policy_default(&pol);
    len = lv_build_v3(pkg, caps_all, APP_ADMISSION_ABI_VERSION_V3, 1576u, 8192u, 6144u, 512u,
                      2048u, 1);
    LV_EXPECT(pkg, len, pol, trust, APP_ADMISSION_ERR_BAD_PACKAGE);

    lv_policy_default(&pol);
    len = lv_build_v3(pkg, caps_all, APP_ADMISSION_ABI_VERSION_V3, 1576u, 8192u, 6144u, 0u,
                      2048u, 0);
    LV_EXPECT(pkg, len, pol, trust, APP_ADMISSION_ERR_BAD_PACKAGE);

    lv_policy_default(&pol);
    len = lv_build_v3(pkg, caps_all, APP_ADMISSION_ABI_VERSION_V3, 1576u, 8192u, 6144u, 512u,
                      0u, 0);
    LV_EXPECT(pkg, len, pol, trust, APP_ADMISSION_ERR_BAD_PACKAGE);

    lv_policy_default(&pol);
    pol.abi_version = APP_ADMISSION_ABI_VERSION_V2;
    len = lv_build_v3(pkg, caps_all, APP_ADMISSION_ABI_VERSION_V3, 1576u, 8192u, 6144u, 512u,
                      2048u, 0);
    LV_EXPECT(pkg, len, pol, trust, APP_ADMISSION_ERR_ABI_INCOMPATIBLE);

    lv_policy_default(&pol);
    pol.abi_table_size = 61u;
    len = lv_build_v3(pkg, caps_all, APP_ADMISSION_ABI_VERSION_V3, 1576u, 8192u, 6144u, 512u,
                      2048u, 0);
    LV_EXPECT(pkg, len, pol, trust, APP_ADMISSION_ERR_ABI_INCOMPATIBLE);

    lv_policy_default(&pol);
    pol.supported_caps_mask = APP_ADMISSION_V3_KNOWN_CAPS_MASK & ~APP_ADMISSION_V3_CAP_BUSINESS_SNAPSHOT;
    len = lv_build_v3(pkg, caps_all, APP_ADMISSION_ABI_VERSION_V3, 1576u, 8192u, 6144u, 512u,
                      2048u, 0);
    LV_EXPECT(pkg, len, pol, trust, APP_ADMISSION_ERR_ABI_INCOMPATIBLE);

    lv_policy_default(&pol);
    pol.supported_caps_mask = 0x00000000u;
    len = lv_build_v3(pkg, caps_all, APP_ADMISSION_ABI_VERSION_V3, 1576u, 8192u, 6144u, 512u,
                      2048u, 0);
    LV_EXPECT(pkg, len, pol, trust, APP_ADMISSION_ERR_ABI_INCOMPATIBLE);

    lv_policy_default(&pol);
    len = lv_build_v3(pkg, caps_all, APP_ADMISSION_ABI_VERSION_V3, 1575u, 8192u, 6144u, 512u,
                      2048u, 0);
    LV_EXPECT(pkg, len, pol, trust, APP_ADMISSION_ERR_RESOURCE_LIMIT);

    lv_policy_default(&pol);
    len = lv_build_v3(pkg, caps_all, APP_ADMISSION_ABI_VERSION_V3, 1576u, 8192u, 6144u, 31u,
                      2048u, 0);
    LV_EXPECT(pkg, len, pol, trust, APP_ADMISSION_ERR_RESOURCE_LIMIT);

    lv_policy_default(&pol);
    pol.v3_business_profile = APP_ADMISSION_V3_BUSINESS_NONE;
    len = lv_build_v3(pkg, caps_all, APP_ADMISSION_ABI_VERSION_V3, 1576u, 8192u, 6144u, 512u,
                      2048u, 0);
    LV_EXPECT(pkg, len, pol, trust, APP_ADMISSION_ERR_ABI_INCOMPATIBLE);

    lv_policy_default(&pol);
    pol.v3_business_profile = (app_admission_v3_business_profile_t)7u;
    len = lv_build_v3(pkg, caps_all, APP_ADMISSION_ABI_VERSION_V3, 1576u, 8192u, 6144u, 512u,
                      2048u, 0);
    LV_EXPECT(pkg, len, pol, trust, APP_ADMISSION_ERR_PARAM);

    lv_policy_default(&pol);
    pol.event_capacity = 1575u;
    len = lv_build_v3(pkg, caps_all, APP_ADMISSION_ABI_VERSION_V3, 1576u, 8192u, 6144u, 512u,
                      2048u, 0);
    LV_EXPECT(pkg, len, pol, trust, APP_ADMISSION_ERR_RESOURCE_LIMIT);

    lv_policy_default(&pol);
    pol.snapshot_capacity = 1023u;
    len = lv_build_v3(pkg, caps_all, APP_ADMISSION_ABI_VERSION_V3, 1576u, 8192u, 6144u, 512u,
                      2048u, 0);
    LV_EXPECT(pkg, len, pol, trust, APP_ADMISSION_ERR_RESOURCE_LIMIT);
    printf("STUDY ac1 v3_reject_matrix unknown_bit=BAD unknown_abi=BAD reserved184=BAD policy_caps=ABI_INCOMPATIBLE limits=RESOURCE\n");
}

static size_t lv_build_v2(uint8_t *pkg, uint32_t caps, uint32_t event_max, uint32_t state_quota,
                          uint32_t report_max)
{
    uint8_t *m = pkg + APP_ADMISSION_CONTAINER_HEADER_SIZE;
    uint8_t *img = pkg + APP_ADMISSION_IMAGE_OFFSET_V2;
    uint32_t file_len = (uint32_t)lv_build_image(img, APP_ADMISSION_ABI_VERSION_V2, 128u, 16u);
    uint8_t image_sha[32];

    memset(pkg, 0, APP_ADMISSION_CONTAINER_HEADER_SIZE);
    pkg[0] = 0x4Eu;
    pkg[1] = 0x45u;
    pkg[2] = 0x41u;
    pkg[3] = 0x50u;
    pkg[4] = 0x50u;
    pkg[5] = 0x02u;
    lv_put_u32(pkg + 8, APP_ADMISSION_MANIFEST_SIZE_V2);
    lv_put_u32(pkg + 12, file_len);

    memset(m, 0, APP_ADMISSION_MANIFEST_SIZE_V2);
    memcpy(m + 0, "NMF2", 4);
    lv_put_id(m + 4, APP_ADMISSION_PUBLISHER_ID_SLOT, LV_PUB);
    lv_put_id(m + 20, APP_ADMISSION_APP_ID_SLOT, LV_APP);
    lv_put_u16(m + 52, 1u);
    lv_put_u16(m + 54, 0u);
    lv_put_u16(m + 56, 0u);
    lv_put_u32(m + 60, LV_BOARD_ID);
    lv_put_u32(m + 64, 32u);
    lv_put_u32(m + 68, APP_ADMISSION_ABI_VERSION_V2);
    lv_put_u32(m + 72, caps);
    lv_put_u32(m + 76, 8192u);
    lv_put_u32(m + 80, file_len);
    lv_put_u32(m + 84, 128u);
    lv_put_u32(m + 88, 16u);
    lv_put_u32(m + 92, LV_EXEC_BASE);
    mbedtls_sha256(img, file_len, image_sha, 0);
    memcpy(m + 96, image_sha, 32);
    memcpy(m + 128, lv_spki_fp(), 32);
    lv_put_u32(m + 160, APP_ADMISSION_V2_RUN_PROFILE_SESSION);
    lv_put_u32(m + 164, event_max);
    lv_put_u32(m + 168, state_quota);
    lv_put_u32(m + 172, report_max);
    return APP_ADMISSION_IMAGE_OFFSET_V2 + file_len +
           lv_build_sig(pkg + APP_ADMISSION_IMAGE_OFFSET_V2 + file_len);
}

static void lv_v2_matrix(void)
{
    static uint8_t pkg[APP_ADMISSION_MAX_PACKAGE_LEN_V2];
    app_admission_policy_t pol;
    app_admission_trust_view_t trust;
    size_t len;
    uint32_t caps = APP_ADMISSION_KNOWN_CAPS_MASK | APP_ADMISSION_V2_CAP_AI_EVENTS |
                    APP_ADMISSION_V2_CAP_REPORT_SUBMIT | APP_ADMISSION_V2_CAP_APP_STATE |
                    APP_ADMISSION_V2_CAP_SESSION_LIFECYCLE;

    memset(&pol, 0, sizeof(pol));
    pol.target_board_id = LV_BOARD_ID;
    pol.psram_mib = 32u;
    pol.abi_version = APP_ADMISSION_ABI_VERSION_V2;
    pol.supported_caps_mask = APP_ADMISSION_V2_KNOWN_CAPS_MASK;
    pol.exec_region_base = LV_EXEC_BASE;
    pol.exec_region_size = 2097152u;
    pol.event_capacity = 4096u;
    pol.report_capacity = 8192u;
    pol.state_quota_capacity = 8192u;
    pol.max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN_V2;

    lv_trust_default(&trust);

    len = lv_build_v2(pkg, caps, 1576u, 4096u, 6144u);
    LV_EXPECT(pkg, len, pol, trust, APP_ADMISSION_OK);

    len = lv_build_v2(pkg, caps | APP_ADMISSION_V3_CAP_CONTROL_RECEIVE, 1576u, 4096u, 6144u);
    LV_EXPECT(pkg, len, pol, trust, APP_ADMISSION_ERR_BAD_PACKAGE);

    len = lv_build_v2(pkg, caps | APP_ADMISSION_V3_CAP_BUSINESS_SNAPSHOT, 1576u, 4096u, 6144u);
    LV_EXPECT(pkg, len, pol, trust, APP_ADMISSION_ERR_BAD_PACKAGE);
    printf("STUDY ac1 v2_caps_mask=0x%08X v3_bits_unknown_to_v2=BAD\n",
           APP_ADMISSION_V2_KNOWN_CAPS_MASK);
}

static size_t lv_build_v1(uint8_t *pkg, uint32_t caps)
{
    uint8_t *m = pkg + APP_ADMISSION_CONTAINER_HEADER_SIZE;
    uint8_t *img = pkg + APP_ADMISSION_IMAGE_OFFSET;
    uint32_t file_len = (uint32_t)lv_build_image(img, 0x00010000u, 128u, 16u);
    uint8_t image_sha[32];

    memset(pkg, 0, APP_ADMISSION_CONTAINER_HEADER_SIZE);
    pkg[0] = 0x4Eu;
    pkg[1] = 0x45u;
    pkg[2] = 0x41u;
    pkg[3] = 0x50u;
    pkg[4] = 0x50u;
    pkg[5] = 0x01u;
    lv_put_u32(pkg + 8, APP_ADMISSION_MANIFEST_SIZE);
    lv_put_u32(pkg + 12, file_len);

    memset(m, 0, APP_ADMISSION_MANIFEST_SIZE);
    memcpy(m + 0, "NMF1", 4);
    lv_put_id(m + 4, APP_ADMISSION_PUBLISHER_ID_SLOT, LV_PUB);
    lv_put_id(m + 20, APP_ADMISSION_APP_ID_SLOT, LV_APP);
    lv_put_u16(m + 52, 1u);
    lv_put_u16(m + 54, 0u);
    lv_put_u16(m + 56, 0u);
    lv_put_u32(m + 60, LV_BOARD_ID);
    lv_put_u32(m + 64, 32u);
    lv_put_u32(m + 68, 0x00010000u);
    lv_put_u32(m + 72, caps);
    lv_put_u32(m + 76, 8192u);
    lv_put_u32(m + 80, file_len);
    lv_put_u32(m + 84, 128u);
    lv_put_u32(m + 88, 16u);
    lv_put_u32(m + 92, LV_EXEC_BASE);
    mbedtls_sha256(img, file_len, image_sha, 0);
    memcpy(m + 96, image_sha, 32);
    memcpy(m + 128, lv_spki_fp(), 32);
    return APP_ADMISSION_IMAGE_OFFSET + file_len +
           lv_build_sig(pkg + APP_ADMISSION_IMAGE_OFFSET + file_len);
}

static void lv_v1_matrix(void)
{
    static uint8_t pkg[APP_ADMISSION_MAX_PACKAGE_LEN];
    app_admission_policy_t pol;
    app_admission_trust_view_t trust;
    size_t len;

    memset(&pol, 0, sizeof(pol));
    pol.target_board_id = LV_BOARD_ID;
    pol.psram_mib = 32u;
    pol.abi_version = 0x00010000u;
    pol.supported_caps_mask = APP_ADMISSION_KNOWN_CAPS_MASK;
    pol.exec_region_base = LV_EXEC_BASE;
    pol.exec_region_size = 2097152u;
    pol.max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN;

    lv_trust_default(&trust);

    len = lv_build_v1(pkg, APP_ADMISSION_KNOWN_CAPS_MASK);
    LV_EXPECT(pkg, len, pol, trust, APP_ADMISSION_OK);

    len = lv_build_v1(pkg, APP_ADMISSION_KNOWN_CAPS_MASK | APP_ADMISSION_V2_CAP_AI_EVENTS);
    LV_EXPECT(pkg, len, pol, trust, APP_ADMISSION_ERR_BAD_PACKAGE);
    printf("STUDY ac1 v1_caps_mask=0x%08X v2_bit_unknown_to_v1=BAD\n",
           APP_ADMISSION_KNOWN_CAPS_MASK);
}

int main(void)
{
    lv_caps_enumeration();
    lv_v3_matrix();
    lv_v2_matrix();
    lv_v1_matrix();
    printf("STUDY caps_probe total=%d fail=%d\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
