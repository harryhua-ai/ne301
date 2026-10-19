#include <stdio.h>
#include <string.h>

#include "app_admission.h"
#include "app_installer.h"
#include "app_native_vnext_floor.h"
#include "v3_vectors.h"
#include "v4_vectors.h"
#include "mem_io_backend.h"

static int g_total;
static int g_fail;
static app_admission_policy_t g_policy;
static app_uninstall_purge_result_t g_purge_result;

#define CHECK(v) do { \
    g_total++; \
    if (!(v)) { \
        g_fail++; \
        printf("FAIL app_native_vnext:%d %s\n", g_total, #v); \
    } \
} while (0)

static void policy_fill(app_admission_policy_t *policy, uint32_t major,
                        uint32_t caps)
{
    memset(policy, 0, sizeof(*policy));
    policy->target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    policy->psram_mib = 64u;
    policy->abi_version = (major == 4u) ? APP_ADMISSION_ABI_VERSION_V4 :
                                          APP_ADMISSION_ABI_VERSION_V3;
    policy->abi_table_size = (major == 4u) ? APP_ADMISSION_V4_ABI_TABLE_SIZE :
                                             APP_ADMISSION_V3_ABI_TABLE_SIZE;
    policy->supported_caps_mask = caps;
    policy->exec_region_base = 0x93E00000u;
    policy->exec_region_size = 2u * 1024u * 1024u;
    policy->event_capacity = 8192u;
    policy->report_capacity = 8192u;
    policy->state_quota_capacity = 8192u;
    policy->control_capacity = 1024u;
    policy->snapshot_capacity = 4096u;
    policy->live_publish_capacity = 9008u;
    policy->live_track_capacity = 64u;
    policy->max_package_len = (major == 4u) ? APP_ADMISSION_MAX_PACKAGE_LEN_V4 :
                                               APP_ADMISSION_MAX_PACKAGE_LEN_V3;
    policy->v3_business_profile = APP_ADMISSION_V3_BUSINESS_GENERIC_FULL;
}

static app_admission_trust_view_t fixture_trust(void)
{
    app_admission_trust_view_t trust;
    trust.publisher_id = "ne301-exp-pub";
    trust.publisher_id_len = strlen(trust.publisher_id);
    trust.spki_der = V4_TEST_SPKI_DER;
    trust.spki_der_len = V4_TEST_SPKI_DER_LEN;
    return trust;
}

static app_admission_result_t fixture_verify(const uint8_t *data, size_t len,
                                             app_admission_verdict_t *out,
                                             void *ctx)
{
    app_admission_policy_t policy;
    app_admission_trust_view_t trust = fixture_trust();
    uint32_t major = app_admission_container_version(data, len);
    uint32_t caps = (major == 4u) ? APP_ADMISSION_V4_KNOWN_CAPS_MASK :
                                    APP_ADMISSION_V3_KNOWN_CAPS_MASK;
    (void)ctx;
    policy_fill(&policy, major, caps);
    return app_admission_verify_package(data, len, &policy, &trust, out);
}

static app_admission_result_t fixture_verify_alt_owner(
    const uint8_t *data, size_t len, app_admission_verdict_t *out, void *ctx)
{
    app_admission_trust_view_t trust = fixture_trust();
    app_admission_policy_t policy;
    uint32_t major = app_admission_container_version(data, len);
    (void)ctx;
    trust.publisher_id = "other-publisher";
    trust.publisher_id_len = strlen(trust.publisher_id);
    policy_fill(&policy, major, APP_ADMISSION_V4_KNOWN_CAPS_MASK);
    return app_admission_verify_package(data, len, &policy, &trust, out);
}

static app_admission_result_t verify(const uint8_t *package, size_t len,
                                     app_admission_policy_t *policy)
{
    app_admission_trust_view_t trust = fixture_trust();
    app_admission_verdict_t verdict;
    return app_admission_verify_package(package, len, policy, &trust, &verdict);
}

static app_uninstall_purge_result_t purge_cb(void *ctx,
                                             const app_install_record_t *record)
{
    (void)ctx;
    (void)record;
    return g_purge_result;
}

static void test_signed_admission(void)
{
    app_admission_verdict_t verdict;
    app_admission_trust_view_t trust = fixture_trust();
    app_admission_policy_t policy;
    policy_fill(&policy, 4u, APP_ADMISSION_V4_KNOWN_CAPS_MASK);
    CHECK(app_admission_verify_package(V4_GENERIC_PACKAGE, V4_GENERIC_PACKAGE_LEN,
                                       &policy, &trust, &verdict) == APP_ADMISSION_OK);
    CHECK(verdict.container_major == 4u && verdict.required_host_abi == 0x00040000u);
    CHECK(verdict.required_host_caps == 0x63u && verdict.run_profile == 3u);
    CHECK(app_admission_container_version(V4_GENERIC_PACKAGE,
                                         V4_GENERIC_PACKAGE_LEN) == 4u);
    CHECK(verify(V4_ENABLE_PACKAGE, V4_ENABLE_PACKAGE_LEN, &policy) == APP_ADMISSION_OK);
    CHECK(verify(V4_LIVE_PACKAGE, V4_LIVE_PACKAGE_LEN, &policy) == APP_ADMISSION_OK);
    CHECK(verify(V4_LIVE_64_PACKAGE, V4_LIVE_64_PACKAGE_LEN, &policy) == APP_ADMISSION_OK);
    CHECK(verify(V4_LIVE_SMALL_CLOSURE_PACKAGE,
                 V4_LIVE_SMALL_CLOSURE_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_RESOURCE_LIMIT);
    CHECK(verify(V4_LIVE_ZERO_TRACK_PACKAGE,
                 V4_LIVE_ZERO_TRACK_PACKAGE_LEN, &policy) == APP_ADMISSION_ERR_BAD_PACKAGE);
    CHECK(verify(V4_ENABLE_MISSING_DEPS_PACKAGE,
                 V4_ENABLE_MISSING_DEPS_PACKAGE_LEN, &policy) == APP_ADMISSION_ERR_BAD_PACKAGE);
    CHECK(verify(V4_ENABLE_SMALL_CONTROL_PACKAGE,
                 V4_ENABLE_SMALL_CONTROL_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_RESOURCE_LIMIT);
    CHECK(verify(V4_UNKNOWN_CAP_PACKAGE, V4_UNKNOWN_CAP_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_BAD_PACKAGE);
    CHECK(verify(V4_BAD_MAGIC_PACKAGE, V4_BAD_MAGIC_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_BAD_PACKAGE);
    CHECK(verify(V4_BAD_MANIFEST_LENGTH_PACKAGE,
                 V4_BAD_MANIFEST_LENGTH_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_BAD_PACKAGE);
    CHECK(verify(V4_BAD_RESERVED_PACKAGE, V4_BAD_RESERVED_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_BAD_PACKAGE);
    CHECK(verify(V4_TRAILING_BYTE_PACKAGE, V4_TRAILING_BYTE_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_BAD_PACKAGE);
    CHECK(verify(V4_BAD_MANIFEST_ABI_PACKAGE,
                 V4_BAD_MANIFEST_ABI_PACKAGE_LEN, &policy) == APP_ADMISSION_ERR_BAD_PACKAGE);
    CHECK(verify(V4_BAD_NATIVE_ABI_PACKAGE,
                 V4_BAD_NATIVE_ABI_PACKAGE_LEN, &policy) == APP_ADMISSION_ERR_BAD_PACKAGE);
    CHECK(verify(V4_WRONG_PUBLISHER_PACKAGE,
                 V4_WRONG_PUBLISHER_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_PUBLISHER_UNTRUSTED);
    CHECK(verify(V4_WRONG_TARGET_PACKAGE, V4_WRONG_TARGET_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_TARGET_INCOMPATIBLE);
    CHECK(verify(V4_FOREIGN_SIGNATURE_PACKAGE,
                 V4_FOREIGN_SIGNATURE_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_SIGNATURE_INVALID);
    CHECK(verify(V4_ROTATED_KEY_PACKAGE,
                 V4_ROTATED_KEY_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_PUBLISHER_UNTRUSTED);
    CHECK(verify(V4_TAMPERED_TBS_PACKAGE,
                 V4_TAMPERED_TBS_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_SIGNATURE_INVALID);
    CHECK(verify(V4_TAMPERED_SIGNATURE_PACKAGE,
                 V4_TAMPERED_SIGNATURE_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_SIGNATURE_INVALID);
    policy.supported_caps_mask = APP_ADMISSION_V3_KNOWN_CAPS_MASK;
    CHECK(verify(V4_ENABLE_PACKAGE, V4_ENABLE_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_ABI_INCOMPATIBLE);
    CHECK(verify(V4_LIVE_PACKAGE, V4_LIVE_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_ABI_INCOMPATIBLE);
    policy_fill(&policy, 3u, APP_ADMISSION_V3_KNOWN_CAPS_MASK);
    CHECK(verify(V3_GENERIC_MINIMAL_PACKAGE,
                 V3_GENERIC_MINIMAL_PACKAGE_LEN, &policy) == APP_ADMISSION_OK);
}

static void test_install_floor_and_confirmed_retirement(void)
{
    app_install_io_t io;
    app_install_record_t record;
    app_install_record_t second;
    app_install_record_t retired;
    app_uninstall_cleanup_ops_t ops;
    app_uninstall_state_t uninstall_state;
    app_admission_policy_t policy;
    mem_kv_t *floor_kv;

    mem_reset();
    mem_io_init(&io);
    policy_fill(&policy, 4u, APP_ADMISSION_V4_KNOWN_CAPS_MASK);
    g_policy = policy;
    CHECK(app_install(&io, fixture_verify, &g_policy,
                      V4_GENERIC_PACKAGE, V4_GENERIC_PACKAGE_LEN, &record) == APP_INSTALL_OK);
    CHECK(count_files() == 1);
    CHECK(app_native_vnext_floor_validate(&io) == APP_INSTALL_OK);
    app_install_set_app_running(AICAM_TRUE);
    CHECK(app_native_vnext_floor_raise(&io, &record, 1u, 17u, 9u, 1u, 0u) ==
          APP_INSTALL_OK);
    app_install_set_app_running(AICAM_FALSE);
    CHECK(app_native_vnext_floor_check(&io, &record, 4u) == APP_INSTALL_OK);
    CHECK(app_install(&io, fixture_verify, &g_policy,
                      V4_ROTATED_KEY_PACKAGE, V4_ROTATED_KEY_PACKAGE_LEN,
                      &second) == APP_INSTALL_ERR_PUBLISHER_UNTRUSTED);
    CHECK(app_native_vnext_floor_validate(&io) == APP_INSTALL_OK);
    CHECK(app_install(&io, fixture_verify_alt_owner, NULL,
                      V4_WRONG_PUBLISHER_PACKAGE,
                      V4_WRONG_PUBLISHER_PACKAGE_LEN, &second) ==
          APP_INSTALL_ERR_IDENTITY_CONFLICT);
    CHECK(app_install(&io, fixture_verify, &g_policy,
                      V3_GENERIC_MINIMAL_PACKAGE, V3_GENERIC_MINIMAL_PACKAGE_LEN,
                      &second) == APP_INSTALL_ERR_DOWNGRADE_REFUSED);
    CHECK(app_install(&io, fixture_verify, &g_policy,
                      V4_NEXT_GENERATION_PACKAGE, V4_NEXT_GENERATION_PACKAGE_LEN,
                      &second) == APP_INSTALL_OK);
    CHECK(memcmp(record.tbs_sha256, second.tbs_sha256, APP_INSTALL_SHA256_LEN) != 0);
    CHECK(app_native_vnext_floor_check(&io, &second, 4u) == APP_INSTALL_OK);
    ops.ctx = NULL;
    ops.purge = purge_cb;
    g_purge_result = APP_UNINSTALL_PURGE_UNKNOWN;
    CHECK(app_install_uninstall_confirmed(&io, AICAM_TRUE, &ops, &record,
                                          &uninstall_state) ==
          APP_UNINSTALL_CLEANUP_PENDING);
    CHECK(uninstall_state == APP_UNINSTALL_STATE_CLEANUP_PENDING);
    CHECK(app_native_vnext_floor_validate(&io) == APP_INSTALL_OK);
    CHECK(app_install_boot_resolve(&io) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    g_purge_result = APP_UNINSTALL_PURGE_CONFIRMED;
    CHECK(app_install_uninstall_confirmed(&io, AICAM_TRUE, &ops, &record,
                                          &uninstall_state) == APP_UNINSTALL_OK);
    memcpy(&retired, &record, sizeof(retired));
    CHECK(uninstall_state == APP_UNINSTALL_STATE_NONE);
    CHECK(app_native_vnext_floor_validate(&io) == APP_INSTALL_OK);
    CHECK(mem_find_key(APP_NATIVE_VNEXT_FLOOR_KEY) == NULL);
    CHECK(app_install(&io, fixture_verify, &g_policy,
                      V3_GENERIC_MINIMAL_PACKAGE, V3_GENERIC_MINIMAL_PACKAGE_LEN,
                      &record) == APP_INSTALL_OK);
    CHECK(app_install(&io, fixture_verify, &g_policy,
                      V4_GENERIC_PACKAGE, V4_GENERIC_PACKAGE_LEN,
                      &second) == APP_INSTALL_OK);
    app_install_set_app_running(AICAM_TRUE);
    CHECK(app_native_vnext_floor_raise(&io, &second, 3u, 19u, 11u, 1u, 0u) ==
          APP_INSTALL_OK);
    app_install_set_app_running(AICAM_FALSE);
    CHECK(app_native_vnext_floor_retire_finish(&io, &retired) ==
          APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(app_native_vnext_floor_validate(&io) == APP_INSTALL_OK);
    CHECK(app_native_vnext_floor_check(&io, &second, 4u) == APP_INSTALL_OK);
    CHECK(app_install(&io, fixture_verify, &g_policy,
                      V3_GENERIC_MINIMAL_PACKAGE, V3_GENERIC_MINIMAL_PACKAGE_LEN,
                      &record) == APP_INSTALL_ERR_DOWNGRADE_REFUSED);

    mem_reset();
    mem_io_init(&io);
    CHECK(app_install(&io, fixture_verify, &g_policy,
                      V4_GENERIC_PACKAGE, V4_GENERIC_PACKAGE_LEN, &record) == APP_INSTALL_OK);
    app_install_set_app_running(AICAM_TRUE);
    CHECK(app_native_vnext_floor_raise(&io, &record, 2u, 18u, 10u, 1u, 0u) ==
          APP_INSTALL_OK);
    app_install_set_app_running(AICAM_FALSE);
    floor_kv = mem_find_key(APP_NATIVE_VNEXT_FLOOR_KEY);
    CHECK(floor_kv != NULL);
    if (floor_kv != NULL) {
        floor_kv->data[floor_kv->len - 1u] ^= 1u;
    }
    CHECK(app_install(&io, fixture_verify, &g_policy,
                      V3_GENERIC_MINIMAL_PACKAGE, V3_GENERIC_MINIMAL_PACKAGE_LEN,
                      &second) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
}

int main(void)
{
    test_signed_admission();
    test_install_floor_and_confirmed_retirement();
    printf("app_native_vnext: %d checks, %d failures\n", g_total, g_fail);
    return g_fail == 0 ? 0 : 1;
}
