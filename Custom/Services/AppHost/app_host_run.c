#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "app_host_run.h"

#include "app_admission.h"
#include "app_host.h"
#include "app_trust_anchor.h"
#include "mbedtls/sha256.h"

static const app_host_run_backend_t *g_run_backend;
static char g_run_log[APP_RUN_LOG_CAP];
static size_t g_run_log_len;
static aicam_bool_t g_run_log_truncated;

void app_host_run_init(const app_host_run_backend_t *backend)
{
    g_run_backend = backend;
    g_run_log_len = 0;
    g_run_log_truncated = AICAM_FALSE;
}

static void run_log_reset(void)
{
    g_run_log_len = 0;
    g_run_log_truncated = AICAM_FALSE;
}

static void run_log_append(const char *text)
{
    size_t len;
    size_t room;

    if (text == NULL) {
        return;
    }
    len = strlen(text);
    if (len >= APP_RUN_LOG_CAP - g_run_log_len) {
        room = APP_RUN_LOG_CAP - g_run_log_len - 1u;
        if (g_run_log_len < APP_RUN_LOG_CAP) {
            memcpy(g_run_log + g_run_log_len, text, room);
            g_run_log_len = APP_RUN_LOG_CAP - 1u;
        }
        g_run_log_truncated = AICAM_TRUE;
        return;
    }
    memcpy(g_run_log + g_run_log_len, text, len);
    g_run_log_len += len;
}

size_t app_host_run_take_log(char *buf, size_t cap)
{
    size_t copied;

    if (buf == NULL || cap == 0u) {
        return 0u;
    }
    copied = g_run_log_len;
    if (copied > cap - 1u) {
        copied = cap - 1u;
    }
    memcpy(buf, g_run_log, copied);
    buf[copied] = '\0';
    g_run_log_len = 0;
    g_run_log_truncated = AICAM_FALSE;
    return copied;
}

aicam_bool_t app_host_run_log_truncated(void)
{
    return g_run_log_truncated;
}

static int run_api_log(const char *text)
{
    run_log_append(text);
    if (g_run_backend != NULL && g_run_backend->log_sink != NULL) {
        g_run_backend->log_sink(g_run_backend->ctx, text);
    }
    return 0;
}

static int run_api_tick_ms(void)
{
    if (g_run_backend != NULL && g_run_backend->tick_ms != NULL) {
        return g_run_backend->tick_ms(g_run_backend->ctx);
    }
    return 0;
}

static void run_scrub(void)
{
    if (g_run_backend != NULL && g_run_backend->scrub != NULL) {
        g_run_backend->scrub(g_run_backend->ctx);
    }
}

static app_run_result_t run_refuse_owned(app_run_outcome_t *out,
                                         app_run_result_t result,
                                         const char *reason)
{
    run_scrub();
    app_install_set_app_running(AICAM_FALSE);
    out->result = result;
    out->entry_returned = AICAM_FALSE;
    out->entry_result = 0;
    snprintf(out->reason, sizeof(out->reason), "%s", (reason != NULL) ? reason : "");
    return result;
}

static aicam_bool_t run_fill_ctx(app_run_admission_ctx_t *actx)
{
    const app_trust_anchor_t *anchor;
    uint32_t base = 0;
    uint32_t size = 0;

    memset(actx, 0, sizeof(*actx));
    if (app_trust_anchor_status() != APP_TRUST_ANCHOR_PROVISIONED) {
        return AICAM_FALSE;
    }
    anchor = app_trust_anchor_get();
    if (anchor == NULL) {
        return AICAM_FALSE;
    }
    actx->trust.publisher_id = anchor->publisher_id;
    actx->trust.publisher_id_len = anchor->publisher_id_len;
    actx->trust.spki_der = anchor->spki_der;
    actx->trust.spki_der_len = anchor->spki_der_len;

    actx->policy.target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    actx->policy.psram_mib = 64u;
    actx->policy.abi_version = APP_HOST_ABI_VERSION;
    actx->policy.supported_caps_mask = APP_ADMISSION_KNOWN_CAPS_MASK;
    actx->policy.max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN;
    if (app_host_exec_region(&base, &size) == AICAM_TRUE) {
        actx->policy.exec_region_base = base;
        actx->policy.exec_region_size = size;
    }
    return AICAM_TRUE;
}

app_run_result_t app_host_run_installed(const app_install_io_t *io,
                                        app_run_outcome_t *out)
{
    app_run_admission_ctx_t actx;
    app_install_record_t rec;
    aicam_bool_t present = AICAM_FALSE;
    app_install_result_t qrc;
    uint8_t *stage;
    size_t stage_cap = 0;
    size_t staged_len = 0;
    app_install_io_result_t src;
    app_install_io_result_t rrc;
    size_t stat_len = 0;
    uint8_t sha[APP_INSTALL_SHA256_LEN];
    app_admission_verdict_t verdict;
    app_admission_result_t arc;
    app_host_load_policy_t load_policy;
    app_host_image_info_t info;
    app_host_api_table_t api;
    app_host_check_t check;
    int raw = 0;

    if (out == NULL) {
        return APP_RUN_ERR_PARAM;
    }
    memset(out, 0, sizeof(*out));
    out->result = APP_RUN_ERR_PARAM;
    if (io == NULL || io->tx_begin == NULL || io->tx_end == NULL ||
        io->file_stat == NULL || io->file_read == NULL) {
        out->result = APP_RUN_ERR_PARAM;
        return APP_RUN_ERR_PARAM;
    }

    if (g_run_backend == NULL || g_run_backend->stage_base == NULL ||
        g_run_backend->stage_commit == NULL || g_run_backend->invoke_entry == NULL) {
        out->result = APP_RUN_ERR_RUNTIME_UNAVAILABLE;
        snprintf(out->reason, sizeof(out->reason), "%s", "RUNTIME_UNAVAILABLE");
        return APP_RUN_ERR_RUNTIME_UNAVAILABLE;
    }

    if (io->tx_begin(io->ctx) != AICAM_TRUE) {
        out->result = APP_RUN_ERR_BUSY;
        snprintf(out->reason, sizeof(out->reason), "%s", "BUSY");
        return APP_RUN_ERR_BUSY;
    }
    if (app_install_app_running() == AICAM_TRUE) {
        io->tx_end(io->ctx);
        out->result = APP_RUN_ERR_BUSY;
        snprintf(out->reason, sizeof(out->reason), "%s", "BUSY");
        return APP_RUN_ERR_BUSY;
    }
    app_install_set_app_running(AICAM_TRUE);
    io->tx_end(io->ctx);
    run_log_reset();

    qrc = app_install_query_committed(io, &rec, &present);
    if (qrc != APP_INSTALL_OK) {
        return run_refuse_owned(out, APP_RUN_ERR_STORAGE_STATE_UNKNOWN,
                                app_install_result_str(qrc));
    }
    if (present != AICAM_TRUE) {
        return run_refuse_owned(out, APP_RUN_ERR_NOT_INSTALLED, "NOT_INSTALLED");
    }
    out->record = rec;

    if (run_fill_ctx(&actx) != AICAM_TRUE) {
        return run_refuse_owned(out, APP_RUN_ERR_UNAVAILABLE, "TRUST_UNPROVEN");
    }

    stage = g_run_backend->stage_base(g_run_backend->ctx, &stage_cap);
    if (stage == NULL || stage_cap == 0u ||
        (size_t)rec.package_len > stage_cap ||
        (uint64_t)rec.package_len > (uint64_t)APP_ADMISSION_MAX_PACKAGE_LEN) {
        return run_refuse_owned(out, APP_RUN_ERR_UNAVAILABLE, "RESOURCE_LIMIT");
    }

    src = io->file_stat(io->ctx, rec.package_path, &stat_len);
    if (src == APP_INSTALL_IO_ERROR) {
        return run_refuse_owned(out, APP_RUN_ERR_STORAGE_STATE_UNKNOWN,
                                "STORAGE_STATE_UNKNOWN");
    }
    if (src == APP_INSTALL_IO_NOT_FOUND) {
        return run_refuse_owned(out, APP_RUN_ERR_UNAVAILABLE, "file_missing");
    }
    if (stat_len != (size_t)rec.package_len) {
        return run_refuse_owned(out, APP_RUN_ERR_UNAVAILABLE, "content_mismatch");
    }

    rrc = io->file_read(io->ctx, rec.package_path, stage, (size_t)rec.package_len,
                        &staged_len);
    if (rrc == APP_INSTALL_IO_ERROR) {
        return run_refuse_owned(out, APP_RUN_ERR_STORAGE_STATE_UNKNOWN,
                                "STORAGE_STATE_UNKNOWN");
    }
    if (rrc != APP_INSTALL_IO_OK || staged_len != (size_t)rec.package_len) {
        return run_refuse_owned(out, APP_RUN_ERR_UNAVAILABLE, "content_unreadable");
    }

    mbedtls_sha256(stage, staged_len, sha, 0);
    if (memcmp(sha, rec.package_sha256, APP_INSTALL_SHA256_LEN) != 0) {
        return run_refuse_owned(out, APP_RUN_ERR_UNAVAILABLE, "content_mismatch");
    }

    arc = app_admission_verify_package(stage, staged_len, &actx.policy,
                                       &actx.trust, &verdict);
    if (arc != APP_ADMISSION_OK) {
        return run_refuse_owned(out, APP_RUN_ERR_UNAVAILABLE,
                                app_admission_result_str(arc));
    }

    if (strcmp(rec.publisher_id, verdict.publisher_id) != 0 ||
        strcmp(rec.app_id, verdict.app_id) != 0 ||
        rec.version_major != verdict.version_major ||
        rec.version_minor != verdict.version_minor ||
        rec.version_patch != verdict.version_patch ||
        memcmp(rec.tbs_sha256, verdict.tbs_sha256, APP_INSTALL_SHA256_LEN) != 0 ||
        memcmp(rec.publisher_key_sha256, verdict.publisher_key_sha256,
               APP_INSTALL_SHA256_LEN) != 0) {
        return run_refuse_owned(out, APP_RUN_ERR_UNAVAILABLE, "identity_mismatch");
    }

    load_policy.expected_target_addr = verdict.native_target_addr;
    load_policy.region_size = stage_cap;
    load_policy.abi_version = APP_HOST_ABI_VERSION;
    check = app_host_validate(stage + APP_ADMISSION_IMAGE_OFFSET,
                              (size_t)APP_ADMISSION_NATIVE_HEADER_SIZE + verdict.native_image_size,
                              &load_policy, &info);
    if (check != APP_HOST_CHECK_OK) {
        return run_refuse_owned(out, APP_RUN_ERR_UNAVAILABLE,
                                app_host_check_str(check));
    }

    memmove(stage, stage + APP_ADMISSION_IMAGE_OFFSET,
            (size_t)APP_ADMISSION_NATIVE_HEADER_SIZE + info.image_size);
    if ((size_t)APP_ADMISSION_NATIVE_HEADER_SIZE + info.image_size < stage_cap) {
        memset(stage + APP_ADMISSION_NATIVE_HEADER_SIZE + info.image_size, 0,
               stage_cap - ((size_t)APP_ADMISSION_NATIVE_HEADER_SIZE + info.image_size));
    }

    if (g_run_backend->stage_commit(g_run_backend->ctx, stage_cap, &info) != 0) {
        return run_refuse_owned(out, APP_RUN_ERR_LOAD_FAILED, "stage_commit_failed");
    }

    api.table_size = (uint32_t)sizeof(api);
    api.abi_version = APP_HOST_ABI_VERSION;
    api.log = run_api_log;
    api.tick_ms = run_api_tick_ms;

    if (g_run_backend->invoke_entry(g_run_backend->ctx, &api, &info, &raw) != 0) {
        app_install_set_app_running(AICAM_FALSE);
        run_scrub();
        out->result = APP_RUN_ERR_RUNTIME_UNAVAILABLE;
        out->entry_returned = AICAM_FALSE;
        out->entry_result = 0;
        snprintf(out->reason, sizeof(out->reason), "%s", "RUNTIME_UNAVAILABLE");
        return APP_RUN_ERR_RUNTIME_UNAVAILABLE;
    }

    app_install_set_app_running(AICAM_FALSE);
    run_scrub();

    out->result = APP_RUN_OK;
    out->entry_returned = AICAM_TRUE;
    out->entry_result = raw;
    out->reason[0] = '\0';
    return APP_RUN_OK;
}

const char *app_host_run_result_str(app_run_result_t result)
{
    switch (result) {
    case APP_RUN_OK:
        return "OK";
    case APP_RUN_ERR_PARAM:
        return "PARAM";
    case APP_RUN_ERR_NOT_INSTALLED:
        return "NOT_INSTALLED";
    case APP_RUN_ERR_UNAVAILABLE:
        return "UNAVAILABLE";
    case APP_RUN_ERR_STORAGE_STATE_UNKNOWN:
        return "STORAGE_STATE_UNKNOWN";
    case APP_RUN_ERR_BUSY:
        return "BUSY";
    case APP_RUN_ERR_LOAD_FAILED:
        return "LOAD_FAILED";
    case APP_RUN_ERR_RUNTIME_UNAVAILABLE:
        return "RUNTIME_UNAVAILABLE";
    default:
        return "UNKNOWN";
    }
}
