#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "app_admission.h"
#include "app_trust_anchor.h"
#include "app_installer.h"
#include "mem_io_backend.h"

#define PACKAGES_DIR "packages"
#define ANCHOR_SHA_HEX "06e732b1b94daeeef433297f7a8e797bf72b2fd3b882a4ec6a5490352931ebda"

static int g_total;
static int g_fail;

#define CHECK(cond) do { \
    g_total++; \
    if (!(cond)) { \
        g_fail++; \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

static const char *g_result_names[] = {
    "OK", "BAD_PACKAGE", "SIGNATURE_INVALID", "PUBLISHER_UNTRUSTED",
    "TARGET_INCOMPATIBLE", "ABI_INCOMPATIBLE", "RESOURCE_LIMIT", "TRUST_UNPROVEN"
};

static app_admission_result_t device_verify(const uint8_t *data, size_t len,
                                            app_admission_verdict_t *out, void *ctx)
{
    app_admission_policy_t *policy = (app_admission_policy_t *)ctx;
    const app_trust_anchor_t *anchor = app_trust_anchor_get();
    app_admission_trust_view_t trust;

    (void)g_result_names;
    trust.publisher_id = anchor->publisher_id;
    trust.publisher_id_len = anchor->publisher_id_len;
    trust.spki_der = anchor->spki_der;
    trust.spki_der_len = anchor->spki_der_len;
    return app_admission_verify_package(data, len, policy, &trust, out);
}

static void fill_policy(app_admission_policy_t *policy)
{
    memset(policy, 0, sizeof(*policy));
    policy->target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    policy->psram_mib = 64u;
    policy->abi_version = 0x00010000u;
    policy->supported_caps_mask = APP_ADMISSION_KNOWN_CAPS_MASK;
    policy->max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN;
    policy->exec_region_base = 0x93E00000u;
    policy->exec_region_size = 2u * 1024u * 1024u;
}

static int load_package(const char *name, uint8_t *buf, size_t cap, size_t *len)
{
    char path[256];
    FILE *handle;
    size_t got;

    if (snprintf(path, sizeof(path), "%s/%s", PACKAGES_DIR, name) >= (int)sizeof(path)) {
        return -1;
    }
    handle = fopen(path, "rb");
    if (handle == NULL) {
        printf("FAIL cannot open %s\n", path);
        return -1;
    }
    got = fread(buf, 1u, cap, handle);
    fclose(handle);
    *len = got;
    return 0;
}

static void hex_encode(const uint8_t *data, size_t len, char *out)
{
    static const char digits[] = "0123456789abcdef";
    size_t i;

    for (i = 0; i < len; i++) {
        out[2u * i] = digits[data[i] >> 4];
        out[2u * i + 1u] = digits[data[i] & 0x0Fu];
    }
    out[2u * len] = '\0';
}


static void test_anchor_provisioned(void)
{
    const app_trust_anchor_t *anchor = app_trust_anchor_get();
    char hex[65];

    CHECK(app_trust_anchor_status() == APP_TRUST_ANCHOR_PROVISIONED);
    CHECK(anchor->publisher_id_len == strlen("ne301-exp-pub"));
    CHECK(memcmp(anchor->publisher_id, "ne301-exp-pub", anchor->publisher_id_len) == 0);
    CHECK(anchor->spki_der_len == 91u);
    hex_encode(anchor->spki_sha256, 32u, hex);
    CHECK(strcmp(hex, ANCHOR_SHA_HEX) == 0);
    printf("anchor spki_sha256=%s\n", hex);
}

static void expect_class(const char *name, app_admission_result_t got,
                         app_admission_result_t want)
{
    g_total++;
    if (got != want) {
        g_fail++;
        printf("FAIL %s: got %d want %d\n", name, (int)got, (int)want);
    }
}

static void test_admission_real_packages(void)
{
    static uint8_t buf[4096];
    app_admission_policy_t policy;
    app_admission_verdict_t verdict;
    size_t len;

    fill_policy(&policy);

    CHECK(load_package("hello-app_v1.0.0.neapp", buf, sizeof(buf), &len) == 0);
    expect_class("v1", device_verify(buf, len, &verdict, &policy), APP_ADMISSION_OK);
    CHECK(strcmp(verdict.publisher_id, "ne301-exp-pub") == 0);
    CHECK(strcmp(verdict.app_id, "hello-app") == 0);
    CHECK(verdict.version_major == 1u && verdict.version_minor == 0u &&
          verdict.version_patch == 0u);
    CHECK(verdict.native_target_addr == 0x93E00000u);
    CHECK(verdict.native_image_size == 576u);

    CHECK(load_package("hello-app_v2.0.0.neapp", buf, sizeof(buf), &len) == 0);
    expect_class("v2", device_verify(buf, len, &verdict, &policy), APP_ADMISSION_OK);
    CHECK(strcmp(verdict.app_id, "hello-app") == 0);
    CHECK(verdict.version_major == 2u && verdict.version_minor == 0u &&
          verdict.version_patch == 0u);
    CHECK(verdict.native_image_size == 576u);

    CHECK(load_package("hello-app_v1.0.0_badsig.neapp", buf, sizeof(buf), &len) == 0);
    expect_class("badsig", device_verify(buf, len, &verdict, &policy),
                 APP_ADMISSION_ERR_SIGNATURE_INVALID);

    CHECK(load_package("hello-app_v9.9.9_foreign.neapp", buf, sizeof(buf), &len) == 0);
    expect_class("foreign", device_verify(buf, len, &verdict, &policy),
                 APP_ADMISSION_ERR_PUBLISHER_UNTRUSTED);

    CHECK(load_package("hello-app_v1.1.0_badboard.neapp", buf, sizeof(buf), &len) == 0);
    expect_class("badboard", device_verify(buf, len, &verdict, &policy),
                 APP_ADMISSION_ERR_TARGET_INCOMPATIBLE);

    CHECK(load_package("hello-app_v1.2.0_badabi.neapp", buf, sizeof(buf), &len) == 0);
    expect_class("badabi", device_verify(buf, len, &verdict, &policy),
                 APP_ADMISSION_ERR_ABI_INCOMPATIBLE);
}

static void test_installer_real_packages(void)
{
    static uint8_t v1[4096];
    static uint8_t v2[4096];
    static uint8_t alt[4096];
    static uint8_t foreign[4096];
    size_t v1_len;
    size_t v2_len;
    size_t alt_len;
    size_t foreign_len;
    app_install_io_t io;
    app_admission_policy_t policy;
    app_install_record_t rec;
    app_install_record_t removed;
    app_install_query_t query;

    CHECK(load_package("hello-app_v1.0.0.neapp", v1, sizeof(v1), &v1_len) == 0);
    CHECK(load_package("hello-app_v2.0.0.neapp", v2, sizeof(v2), &v2_len) == 0);
    CHECK(load_package("hello-app_v2.0.0_altcontent.neapp", alt, sizeof(alt), &alt_len) == 0);
    CHECK(load_package("hello-app_v9.9.9_foreign.neapp", foreign, sizeof(foreign),
                       &foreign_len) == 0);

    fill_policy(&policy);
    mem_reset();
    mem_io_init(&io);
    app_install_boot_resolve_reset();

    CHECK(app_install_query(&io, device_verify, &policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_NONE);

    CHECK(app_install(&io, device_verify, &policy, v1, v1_len, &rec) == APP_INSTALL_OK);
    CHECK(strcmp(rec.publisher_id, "ne301-exp-pub") == 0);
    CHECK(strcmp(rec.app_id, "hello-app") == 0);
    CHECK(rec.version_major == 1u);
    CHECK(app_install_query(&io, device_verify, &policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_INSTALLED);
    CHECK(query.record.version_major == 1u);

    CHECK(app_install(&io, device_verify, &policy, foreign, foreign_len,
                      &rec) == APP_INSTALL_ERR_PUBLISHER_UNTRUSTED);
    CHECK(app_install_query(&io, device_verify, &policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_INSTALLED);
    CHECK(query.record.version_major == 1u);

    CHECK(app_install(&io, device_verify, &policy, v2, v2_len, &rec) == APP_INSTALL_OK);
    CHECK(rec.version_major == 2u);
    CHECK(app_install_query(&io, device_verify, &policy, &query) == APP_INSTALL_OK);
    CHECK(query.record.version_major == 2u);

    CHECK(app_install(&io, device_verify, &policy, alt, alt_len,
                      &rec) == APP_INSTALL_ERR_VERSION_CONFLICT);
    CHECK(app_install(&io, device_verify, &policy, v1, v1_len,
                      &rec) == APP_INSTALL_ERR_DOWNGRADE_REFUSED);

    app_install_set_app_running(AICAM_TRUE);
    CHECK(app_install(&io, device_verify, &policy, v1, v1_len,
                      &rec) == APP_INSTALL_ERR_BUSY);
    CHECK(app_install_uninstall(&io, &removed) == APP_INSTALL_ERR_BUSY);
    app_install_set_app_running(AICAM_FALSE);

    CHECK(app_install_uninstall(&io, &removed) == APP_INSTALL_OK);
    CHECK(removed.version_major == 2u);
    CHECK(app_install_query(&io, device_verify, &policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_NONE);
    CHECK(app_install_uninstall(&io, &removed) == APP_INSTALL_ERR_NOT_FOUND);
}

int main(void)
{
    test_anchor_provisioned();
    test_admission_real_packages();
    test_installer_real_packages();
    printf("test_hello_e2e_40: %d checks, %d failures\n", g_total, g_fail);
    return g_fail == 0 ? 0 : 1;
}
