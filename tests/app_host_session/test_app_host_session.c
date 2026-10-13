#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>

#include "app_host_session.h"
#include "app_host_session_device.h"
#include "app_admission.h"
#include "app_host.h"
#include "app_trust_anchor.h"
#include "installer_vectors.h"
#include "anchor_vectors.h"
#include "session_vectors.h"
#include "mem_io_backend.h"
#include "mem_map.h"
#include "devshim/dev_seam.h"

#define STAGE_BUF_CAP (256u * 1024u)
#define TARGET_BASE 0x93E00000u

static int g_total;
static int g_fail;

static void check_impl(int cond, int line)
{
    g_total++;
    if (!cond) {
        g_fail++;
        printf("FAIL session:%d line=%d\n", g_total, line);
    }
}

static void check_msg_impl(int cond, int line, const char *what)
{
    g_total++;
    if (!cond) {
        g_fail++;
        printf("FAIL session:%d line=%d: %s\n", g_total, line, what);
    }
}

#define CHECK(cond) check_impl((cond), __LINE__)
#define CHECK_MSG(cond, what) check_msg_impl((cond), __LINE__, (what))

typedef struct {
    pthread_mutex_t state_mtx;
    pthread_cond_t cond;
    int signalled;
    aicam_bool_t attached;
    int attach_count;
    int detach_count;
    aicam_bool_t fail_attach;
    int fail_spawn;
    int scrub_count;
    int scrub_hold;
    aicam_bool_t model_available;
    app_session_model_view_t model;
    void (*body)(const app_host_session_api_table_t *api);
    volatile int park_release;
    volatile int terminal_hold;
    volatile int thread_exited;
    int thread_started;
    pthread_t thread;
} test_backend_t;

static test_backend_t tb;
static pthread_mutex_t g_backend_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t tb_scrub_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t tb_scrub_cond = PTHREAD_COND_INITIALIZER;
static app_install_io_t g_session_io;
static uint8_t g_stage[STAGE_BUF_CAP];
static aicam_bool_t g_trust_mode_session;
static uint8_t g_ebuf[APP_SESSION_EVENT_WIRE_MAX];
static int g_entry_kinds[16];
static int g_entry_kind_count;

static uint32_t mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

static void sleep_ms(uint32_t ms)
{
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    nanosleep(&ts, NULL);
}

static const uint8_t *trust_spki(void)
{
    return (g_trust_mode_session != AICAM_FALSE) ? SESS_D1_SPKI_DER
                                                 : ANCHOR_VECTOR_SPKI_DER;
}

static size_t trust_spki_len(void)
{
    return (g_trust_mode_session != AICAM_FALSE) ? (size_t)SESS_D1_SPKI_DER_LEN
                                                 : (size_t)ANCHOR_VECTOR_SPKI_DER_LEN;
}

static const char *trust_publisher(void)
{
    return (g_trust_mode_session != AICAM_FALSE) ? "test-publisher" : "ne301-exp-pub";
}

static const uint8_t *trust_sha(void)
{
    return (g_trust_mode_session != AICAM_FALSE) ? SESS_D1_SPKI_SHA256
                                                 : ANCHOR_VECTOR_SPKI_SHA256;
}

const app_trust_anchor_t *app_trust_anchor_get(void)
{
    static app_trust_anchor_t anchor;

    memset(&anchor, 0, sizeof(anchor));
    anchor.format_version = APP_TRUST_ANCHOR_FORMAT_VERSION;
    anchor.publisher_id = trust_publisher();
    anchor.publisher_id_len = strlen(trust_publisher());
    anchor.spki_der = trust_spki();
    anchor.spki_der_len = trust_spki_len();
    anchor.spki_sha256 = trust_sha();
    return &anchor;
}

app_trust_anchor_status_t app_trust_anchor_status(void)
{
    return APP_TRUST_ANCHOR_PROVISIONED;
}

const char *app_trust_anchor_status_str(app_trust_anchor_status_t status)
{
    (void)status;
    return "provisioned";
}

aicam_bool_t app_host_exec_region(uint32_t *base, uint32_t *size)
{
    *base = TARGET_BASE;
    *size = 2u * 1024u * 1024u;
    return AICAM_TRUE;
}

static void tb_state_lock(void)
{
    pthread_mutex_lock(&tb.state_mtx);
}

static void tb_state_unlock(void)
{
    pthread_mutex_unlock(&tb.state_mtx);
}

static void *tb_wait(uint32_t timeout_ms)
{
    struct timespec ts;
    void *signalled = NULL;

    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += (time_t)(timeout_ms / 1000u);
    ts.tv_nsec += (long)(timeout_ms % 1000u) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    pthread_mutex_lock(&tb.state_mtx);
    if (tb.signalled) {
        tb.signalled = 0;
        signalled = (void *)1;
    } else {
        int rc = pthread_cond_timedwait(&tb.cond, &tb.state_mtx, &ts);
        if (rc == 0 && tb.signalled) {
            tb.signalled = 0;
            signalled = (void *)1;
        }
    }
    pthread_mutex_unlock(&tb.state_mtx);
    return signalled;
}

static void *tb_body_park(const app_host_session_api_table_t *api)
{
    (void)api;
    for (;;) {
        if (tb.park_release) {
            tb.park_release = 0;
            return NULL;
        }
        sleep_ms(2);
    }
}

static uint32_t rd32_stub(const uint8_t *p, uint32_t off)
{
    return (uint32_t)p[off] | ((uint32_t)p[off + 1u] << 8) |
           ((uint32_t)p[off + 2u] << 16) | ((uint32_t)p[off + 3u] << 24);
}

static void tb_body_consumer(const app_host_session_api_table_t *api)
{
    uint32_t len = 0;

    for (;;) {
        int32_t rc = api->event_next(g_ebuf, sizeof(g_ebuf), &len, 40u);
        if (rc == 0) {
            if (g_entry_kind_count < 16) {
                g_entry_kinds[g_entry_kind_count] = (int)((rd32_stub(g_ebuf, 4u)));
                g_entry_kind_count++;
            }
        } else if (rc == -10) {
            break;
        } else if (rc == 1) {
            if (api->should_stop() == 1) {
                break;
            }
            continue;
        } else {
            break;
        }
    }
}

static void *tb_entry_trampoline(void *arg)
{
    void (*fn)(void *) = (void (*)(void *))arg;

    fn(NULL);
    tb_state_lock();
    tb.thread_exited++;
    tb_state_unlock();
    return NULL;
}

static uint8_t *tb_stage_base(void *ctx, size_t *cap)
{
    (void)ctx;
    *cap = STAGE_BUF_CAP;
    return g_stage;
}

static int tb_stage_commit(void *ctx, size_t stage_cap, const app_host_image_info_t *info)
{
    (void)ctx;
    (void)stage_cap;
    (void)info;
    return 0;
}

static int tb_invoke_entry(void *ctx, const app_host_session_api_table_t *api,
                           const app_host_image_info_t *info, int *raw)
{
    (void)ctx;
    (void)info;
    if (tb.body != NULL) {
        tb.body(api);
    } else {
        tb_body_park(api);
    }
    *raw = 0;
    return 0;
}

static void tb_scrub(void *ctx)
{
    (void)ctx;
    pthread_mutex_lock(&tb_scrub_mtx);
    tb.scrub_count++;
    while (tb.scrub_hold != 0) {
        pthread_cond_wait(&tb_scrub_cond, &tb_scrub_mtx);
    }
    pthread_mutex_unlock(&tb_scrub_mtx);
    memset(g_stage, 0, sizeof(g_stage));
}

static void tb_scrub_hold_set(int hold)
{
    pthread_mutex_lock(&tb_scrub_mtx);
    tb.scrub_hold = hold;
    if (hold == 0) {
        pthread_cond_broadcast(&tb_scrub_cond);
    }
    pthread_mutex_unlock(&tb_scrub_mtx);
}

static int tb_scrub_count_get(void)
{
    int n;

    pthread_mutex_lock(&tb_scrub_mtx);
    n = tb.scrub_count;
    pthread_mutex_unlock(&tb_scrub_mtx);
    return n;
}

static void tb_log_sink(void *ctx, const char *text)
{
    (void)ctx;
    (void)text;
}

static uint32_t tb_tick_ms(void *ctx)
{
    (void)ctx;
    return mono_ms();
}

static void tb_sleep_ms(void *ctx, uint32_t ms)
{
    (void)ctx;
    sleep_ms(ms);
}

static int tb_entry_spawn(void *ctx, void (*fn)(void *arg), void *arg)
{
    (void)ctx;
    (void)arg;
    tb.thread_started++;
    if (tb.fail_spawn != 0) {
        return -1;
    }
    if (pthread_create(&tb.thread, NULL, tb_entry_trampoline, (void *)fn) != 0) {
        return -1;
    }
    pthread_detach(tb.thread);
    return 0;
}

static aicam_bool_t tb_entry_exited(void *ctx)
{
    aicam_bool_t exited;

    (void)ctx;
    tb_state_lock();
    exited = (tb.thread_exited != 0 && tb.terminal_hold == 0) ? AICAM_TRUE
                                                              : AICAM_FALSE;
    tb_state_unlock();
    return exited;
}

static void *tb_mutex_create(void *ctx)
{
    (void)ctx;
    return (void *)&g_backend_mutex;
}

static void tb_mutex_destroy(void *ctx, void *m)
{
    (void)ctx;
    (void)m;
}

static int tb_mutex_lock(void *ctx, void *m)
{
    (void)ctx;
    if (pthread_mutex_lock((pthread_mutex_t *)m) != 0) {
        return -1;
    }
    return 0;
}

static void tb_mutex_unlock2(void *ctx, void *m)
{
    (void)ctx;
    pthread_mutex_unlock((pthread_mutex_t *)m);
}

static void *tb_event_create(void *ctx)
{
    (void)ctx;
    return (void *)&tb.signalled;
}

static void tb_event_destroy(void *ctx, void *e)
{
    (void)ctx;
    (void)e;
}

static uint32_t tb_event_wait(void *ctx, void *e, uint32_t timeout_ms)
{
    (void)ctx;
    (void)e;
    return (tb_wait(timeout_ms) != NULL) ? 1u : 0u;
}

static void tb_event_set(void *ctx, void *e)
{
    (void)ctx;
    (void)e;
    pthread_mutex_lock(&tb.state_mtx);
    tb.signalled = 1;
    pthread_cond_signal(&tb.cond);
    pthread_mutex_unlock(&tb.state_mtx);
}

static aicam_bool_t tb_ai_attach(void *ctx)
{
    (void)ctx;
    tb_state_lock();
    tb.attach_count++;
    if (tb.fail_attach == AICAM_TRUE) {
        tb_state_unlock();
        return AICAM_FALSE;
    }
    tb.attached = AICAM_TRUE;
    tb_state_unlock();
    return AICAM_TRUE;
}

static void tb_ai_detach(void *ctx)
{
    (void)ctx;
    tb_state_lock();
    tb.attached = AICAM_FALSE;
    tb.detach_count++;
    tb_state_unlock();
}

static aicam_bool_t tb_ai_model_view(void *ctx, app_session_model_view_t *out)
{
    (void)ctx;
    tb_state_lock();
    if (tb.model_available != AICAM_TRUE) {
        tb_state_unlock();
        return AICAM_FALSE;
    }
    *out = tb.model;
    tb_state_unlock();
    return AICAM_TRUE;
}

static const app_host_session_backend_t g_test_backend = {
    NULL,
    tb_stage_base,
    tb_stage_commit,
    tb_invoke_entry,
    tb_scrub,
    tb_log_sink,
    tb_tick_ms,
    tb_sleep_ms,
    tb_entry_spawn,
    tb_entry_exited,
    tb_mutex_create,
    tb_mutex_destroy,
    tb_mutex_lock,
    tb_mutex_unlock2,
    tb_event_create,
    tb_event_destroy,
    tb_event_wait,
    tb_event_set,
    tb_ai_attach,
    tb_ai_detach,
    tb_ai_model_view
};

static uint32_t tb_model_digest(uintptr_t identity, const char *const *names,
                                uint32_t count, const char *name,
                                const char *version)
{
    const char *s;
    uint32_t h = 2166136261u;
    uint32_t i;

    h ^= (uint32_t)(identity & 0xFFFFFFFFu);
    h *= 16777619u;
    h ^= 4096u;
    h *= 16777619u;
    for (s = name; *s != '\0'; s++) {
        h ^= (uint32_t)(uint8_t)*s;
        h *= 16777619u;
    }
    for (s = version; *s != '\0'; s++) {
        h ^= (uint32_t)(uint8_t)*s;
        h *= 16777619u;
    }
    for (i = 0u; i < count && i < APP_SESSION_MAX_CLASSES; i++) {
        for (s = names[i]; *s != '\0'; s++) {
            h ^= (uint32_t)(uint8_t)*s;
            h *= 16777619u;
        }
    }
    return h;
}

static void set_model(uintptr_t identity, const char *const *names, uint32_t count,
                      const char *name, const char *version)
{
    uint32_t i;
    uint32_t fill;

    tb_state_lock();
    memset(&tb.model, 0, sizeof(tb.model));
    snprintf(tb.model.model_name, APP_SESSION_MODEL_NAME_SLOT, "%s", name);
    snprintf(tb.model.model_version, APP_SESSION_MODEL_VERSION_SLOT, "%s", version);
    tb.model.identity_addr = identity;
    tb.model.identity_size = 4096u;
    tb.model.content_digest = tb_model_digest(identity, names, count, name, version);
    tb.model.result_type_od = AICAM_TRUE;
    tb.model.class_count = count;
    fill = (count > APP_SESSION_MAX_CLASSES) ? APP_SESSION_MAX_CLASSES : count;
    for (i = 0u; i < fill; i++) {
        snprintf(tb.model.class_names[i], APP_SESSION_CLASS_SLOT, "%s", names[i]);
    }
    tb_state_unlock();
}

static void set_class_slot_unterminated(uint32_t index)
{
    tb_state_lock();
    tb.model.class_names[index][APP_SESSION_CLASS_SLOT - 1u] = 'x';
    tb_state_unlock();
}

static void set_model_available(aicam_bool_t available)
{
    tb_state_lock();
    tb.model_available = available;
    tb_state_unlock();
}

static const char *const g_two_classes[] = { "person", "car" };
static const char *const g_three_classes[] = { "person", "car", "dog" };

static od_detect_t g_det[96];

static void mk_det(od_detect_t *d, float x, float y, float w, float h,
                   float conf, const char *name)
{
    d->x = x;
    d->y = y;
    d->width = w;
    d->height = h;
    d->conf = conf;
    d->class_name = (char *)name;
}

static void produce(uint32_t count, uint32_t tick)
{
    app_host_session_on_ai_result(count, g_det, tick);
}

static uint32_t rd32(const uint8_t *p, uint32_t off)
{
    return rd32_stub(p, off);
}

static uint32_t fbits(float f)
{
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

static app_admission_policy_t s_v1_policy;
static app_admission_policy_t s_v2_policy;
static app_admission_trust_view_t s_trust;

static void fill_policies(void)
{
    memset(&s_v1_policy, 0, sizeof(s_v1_policy));
    s_v1_policy.target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    s_v1_policy.psram_mib = 64u;
    s_v1_policy.abi_version = APP_HOST_ABI_VERSION;
    s_v1_policy.supported_caps_mask = APP_ADMISSION_KNOWN_CAPS_MASK;
    s_v1_policy.max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN_V2;
    s_v1_policy.exec_region_base = TARGET_BASE;
    s_v1_policy.exec_region_size = 2u * 1024u * 1024u;

    memset(&s_v2_policy, 0, sizeof(s_v2_policy));
    s_v2_policy.target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    s_v2_policy.psram_mib = 64u;
    s_v2_policy.abi_version = APP_SESSION_ABI_VERSION;
    s_v2_policy.supported_caps_mask = APP_SESSION_GRANTED_CAPS;
    s_v2_policy.max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN_V2;
    s_v2_policy.event_capacity = APP_SESSION_EVENT_CAPACITY_MAX;
    s_v2_policy.exec_region_base = TARGET_BASE;
    s_v2_policy.exec_region_size = 2u * 1024u * 1024u;
}

static app_admission_result_t install_verify(const uint8_t *data, size_t len,
                                             app_admission_verdict_t *out, void *ctx)
{
    const app_trust_anchor_t *anchor = app_trust_anchor_get();

    (void)ctx;
    s_trust.publisher_id = anchor->publisher_id;
    s_trust.publisher_id_len = anchor->publisher_id_len;
    s_trust.spki_der = anchor->spki_der;
    s_trust.spki_der_len = anchor->spki_der_len;
    if (app_admission_container_version(data, len) == 2u) {
        return app_admission_verify_package(data, len, &s_v2_policy, &s_trust, out);
    }
    return app_admission_verify_package(data, len, &s_v1_policy, &s_trust, out);
}

static app_install_result_t install_fresh(const uint8_t *pkg, size_t len,
                                          app_install_record_t *rec)
{
    app_install_record_t removed;

    app_install_uninstall(&g_session_io, &removed);
    return app_install(&g_session_io, install_verify, NULL, pkg, len, rec);
}

static void reset_all(void)
{
    memset(&tb, 0, sizeof(tb));
    pthread_mutex_init(&tb.state_mtx, NULL);
    pthread_cond_init(&tb.cond, NULL);
    tb.model_available = AICAM_TRUE;
    set_model(0x1000u, g_two_classes, 2u, "sess-model", "1.0.0");
    app_host_session_init(&g_test_backend);
    mem_reset();
    mem_io_init(&g_session_io);
    app_install_boot_resolve_reset();
    app_install_set_app_running(AICAM_FALSE);
    fill_policies();
    g_trust_mode_session = AICAM_TRUE;
}

static app_session_result_t start_ok(app_host_session_start_out_t *out)
{
    return app_host_session_start(&g_session_io, out);
}

static const app_host_session_api_table_t *api_tbl(void)
{
    return app_host_session_api_table();
}

static int32_t pop(uint8_t *buf, uint32_t cap, uint32_t *len, uint32_t wait)
{
    return api_tbl()->event_next(buf, cap, len, wait);
}

static app_install_result_t install_expected(const uint8_t *pkg, size_t len,
                                             app_install_record_t *rec)
{
    app_install_result_t rc = install_fresh(pkg, len, rec);
    return (rc == APP_INSTALL_OK || rc == APP_INSTALL_OK_ALREADY) ? APP_INSTALL_OK : rc;
}

typedef struct {
    app_session_result_t rc;
    char reason[APP_SESSION_REASON_SLOT];
} start_call_t;

static void *start_thread_fn(void *arg)
{
    start_call_t *call = (start_call_t *)arg;
    app_host_session_start_out_t out;

    memset(&out, 0, sizeof(out));
    call->rc = app_host_session_start(&g_session_io, &out);
    snprintf(call->reason, sizeof(call->reason), "%s", out.reason);
    return NULL;
}

static void wait_idle(void)
{
    app_host_session_status_t st;
    int spins = 0;

    for (;;) {
        app_host_session_status(&st);
        if (st.state == APP_SESSION_STATE_IDLE) {
            break;
        }
        sleep_ms(2);
        spins++;
        if (spins > 5000) {
            break;
        }
    }
}

static void test_container_version(void)
{
    uint8_t buf[16];

    CHECK(app_admission_container_version(NULL, 0u) == 0u);
    CHECK(app_admission_container_version(buf, 4u) == 0u);
    memcpy(buf, SESS_PKG, 8);
    CHECK(app_admission_container_version(buf, sizeof(buf)) == 2u);
    memcpy(buf, HELLO_V1_PACKAGE, 8);
    CHECK(app_admission_container_version(buf, sizeof(buf)) == 1u);
    memset(buf, 0xAA, sizeof(buf));
    CHECK(app_admission_container_version(buf, sizeof(buf)) == 0u);
}

static void test_unauthorized_before_start(void)
{
    uint8_t buf[512];
    uint32_t len = 0;

    CHECK(api_tbl()->event_next(buf, sizeof(buf), &len, 1u) == -3);
    CHECK(api_tbl()->model_meta(buf, sizeof(buf), &len) == -3);
    CHECK(api_tbl()->class_name(1u, 1u, 0u, buf, sizeof(buf), &len) == -3);
    CHECK(api_tbl()->should_stop() == -3);
    CHECK(api_tbl()->report_submit(buf, 1u, 1u, NULL) == -3);
    CHECK(api_tbl()->report_status(1u, 1u, NULL, NULL) == -3);
    CHECK(api_tbl()->state_read(buf, sizeof(buf), &len, NULL) == -3);
    CHECK(api_tbl()->state_commit(buf, 1u, 0u, NULL) == -3);
}

static void test_start_refusals(void)
{
    app_host_session_start_out_t out;
    app_install_record_t rec;
    uint32_t i;

    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_ERR_NOT_INSTALLED);
    CHECK(strcmp(out.reason, "NOT_INSTALLED") == 0);
    CHECK(app_install_app_running() == AICAM_FALSE);

    g_trust_mode_session = AICAM_FALSE;
    CHECK(install_expected(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_ERR_PROFILE_UNSUPPORTED);
    CHECK_MSG(strcmp(out.reason, "profile_unsupported") == 0, "v1_refused");
    CHECK(app_install_app_running() == AICAM_FALSE);
    g_trust_mode_session = AICAM_TRUE;

    CHECK(install_fresh(SESS_PKG_NOSESSION, SESS_PKG_NOSESSION_LEN, &rec) ==
          APP_INSTALL_ERR_BAD_PACKAGE);
    CHECK(install_fresh(SESS_PKG_FULLCAPS, SESS_PKG_FULLCAPS_LEN, &rec) ==
          APP_INSTALL_ERR_ABI_INCOMPATIBLE);
    CHECK(install_fresh(SESS_PKG_SMALL_EM, SESS_PKG_SMALL_EM_LEN, &rec) ==
          APP_INSTALL_ERR_RESOURCE_LIMIT);
    CHECK(install_fresh(SESS_PKG_D2, SESS_PKG_D2_LEN, &rec) ==
          APP_INSTALL_ERR_PUBLISHER_UNTRUSTED);

    set_model_available(AICAM_FALSE);
    CHECK(install_expected(SESS_PKG, SESS_PKG_LEN, &rec) == APP_INSTALL_OK);
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_ERR_UNAVAILABLE);
    CHECK_MSG(strcmp(out.reason, "model_incompatible") == 0, "no_model_refused");
    set_model_available(AICAM_TRUE);

    {
        static const char *dup_names[2];
        dup_names[0] = "person";
        dup_names[1] = "person";
        set_model(0x2000u, dup_names, 2u, "dup", "1.0.0");
        memset(&out, 0, sizeof(out));
        CHECK(start_ok(&out) == APP_SESSION_ERR_UNAVAILABLE);
        CHECK_MSG(strcmp(out.reason, "model_incompatible") == 0, "dup_labels_refused");
    }
    {
        static const char *long_names[2];
        char long_label[64];
        memset(long_label, 'a', sizeof(long_label) - 1u);
        long_label[sizeof(long_label) - 1u] = '\0';
        long_names[0] = long_label;
        long_names[1] = "car";
        set_model(0x2100u, long_names, 2u, "long", "1.0.0");
        set_class_slot_unterminated(0u);
        memset(&out, 0, sizeof(out));
        CHECK(start_ok(&out) == APP_SESSION_ERR_UNAVAILABLE);
        CHECK_MSG(strcmp(out.reason, "model_incompatible") == 0, "long_label_refused");
    }
    {
        static char labels[33][8];
        const char *many_names[33];
        for (i = 0u; i < 33u; i++) {
            snprintf(labels[i], sizeof(labels[i]), "c%u", (unsigned)i);
            many_names[i] = labels[i];
        }
        set_model(0x2200u, many_names, 33u, "many", "1.0.0");
        memset(&out, 0, sizeof(out));
        CHECK(start_ok(&out) == APP_SESSION_ERR_UNAVAILABLE);
        CHECK_MSG(strcmp(out.reason, "model_incompatible") == 0, "too_many_classes");
    }
    {
        static const char *pose_names[2];
        pose_names[0] = "kp0";
        pose_names[1] = "kp1";
        set_model(0x2300u, pose_names, 2u, "pose", "1.0.0");
        tb_state_lock();
        tb.model.result_type_od = AICAM_FALSE;
        tb_state_unlock();
        memset(&out, 0, sizeof(out));
        CHECK(start_ok(&out) == APP_SESSION_ERR_UNAVAILABLE);
        CHECK_MSG(strcmp(out.reason, "model_incompatible") == 0, "non_od_refused");
    }
    set_model(0x1000u, g_two_classes, 2u, "sess-model", "1.0.0");
}

static void test_start_success_and_double(void)
{
    app_host_session_start_out_t out;
    app_install_record_t rec;
    app_host_session_status_t st;

    CHECK(install_expected(SESS_PKG, SESS_PKG_LEN, &rec) == APP_INSTALL_OK);
    CHECK(count_files() == 1);
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);
    CHECK_MSG(strcmp(out.record.app_id, "hello-app") == 0, "record_app");
    CHECK(out.abi_version == APP_SESSION_ABI_VERSION);
    CHECK(out.granted_caps == APP_SESSION_GRANTED_CAPS);
    CHECK(out.event_max == 1576u);

    app_host_session_status(&st);
    CHECK(st.state == APP_SESSION_STATE_RUNNING);
    CHECK(st.app_running == AICAM_TRUE);
    CHECK(st.event_max == 1576u);
    CHECK(st.model_generation == 1u);
    CHECK(st.class_generation == 1u);
    CHECK(st.class_count == 2u);
    CHECK(strcmp(st.model_name, "sess-model") == 0);
    CHECK(strcmp(st.publisher_id, "test-publisher") == 0);
    CHECK(strcmp(st.app_id, "hello-app") == 0);
    CHECK(tb.attach_count == 1);
    CHECK(st.abi_version == 0x00020000u);

    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_ERR_BUSY);
    CHECK(strcmp(out.reason, "session_active") == 0);
}

static void test_frame_layout(void)
{
    uint8_t buf[APP_SESSION_EVENT_WIRE_MAX];
    uint32_t len = 0;

    mk_det(&g_det[0], 1.0f, 2.0f, 3.0f, 4.0f, 0.5f, "person");
    mk_det(&g_det[1], 5.0f, 6.0f, 7.0f, 8.0f, 0.25f, "car");
    produce(2u, 1234u);

    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(len == APP_SESSION_EVENT_HEADER_SIZE + 2u * APP_SESSION_EVENT_RECORD_SIZE);
    CHECK(rd32(buf, 0) == len);
    CHECK(rd32(buf, 4) == 1u);
    CHECK(rd32(buf, 8) == 0u);
    CHECK(rd32(buf, 12) != 0u);
    CHECK(rd32(buf, 16) == 1u);
    CHECK(rd32(buf, 20) == 1u);
    CHECK(rd32(buf, 24) == 0u);
    CHECK(rd32(buf, 28) == 2u);
    CHECK(rd32(buf, 32) == 0u);
    CHECK(rd32(buf, 36) == 0u);
    CHECK(rd32(buf, 40) == fbits(1.0f));
    CHECK(rd32(buf, 44) == fbits(2.0f));
    CHECK(rd32(buf, 48) == fbits(3.0f));
    CHECK(rd32(buf, 52) == fbits(4.0f));
    CHECK(rd32(buf, 56) == fbits(0.5f));
    CHECK(rd32(buf, 60) == 0u);
    CHECK(rd32(buf, 64) == fbits(5.0f));
    CHECK(rd32(buf, 76) == fbits(8.0f));
    CHECK(rd32(buf, 80) == fbits(0.25f));
    CHECK(rd32(buf, 84) == 1u);
}

static void test_empty_frame_and_no_event(void)
{
    uint8_t buf[APP_SESSION_EVENT_WIRE_MAX];
    uint32_t len = 0;
    uint32_t t0;

    produce(0u, 77u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(len == APP_SESSION_EVENT_HEADER_SIZE);
    CHECK(rd32(buf, 4) == 1u);
    CHECK(rd32(buf, 28) == 0u);

    t0 = mono_ms();
    CHECK(pop(buf, sizeof(buf), &len, 40u) == 1);
    CHECK((uint32_t)(mono_ms() - t0) < 2000u);
}

static void test_buffer_too_small(void)
{
    uint8_t buf[APP_SESSION_EVENT_WIRE_MAX];
    uint32_t len = 0;

    mk_det(&g_det[0], 1.0f, 2.0f, 3.0f, 4.0f, 0.5f, "person");
    produce(1u, 55u);
    CHECK(pop(buf, APP_SESSION_EVENT_HEADER_SIZE - 1u, &len, 0u) == -2);
    CHECK(pop(buf, APP_SESSION_EVENT_HEADER_SIZE + APP_SESSION_EVENT_RECORD_SIZE, &len, 0u) == 0);
    CHECK(len == APP_SESSION_EVENT_HEADER_SIZE + APP_SESSION_EVENT_RECORD_SIZE);

    CHECK(api_tbl()->event_next(NULL, 512u, &len, 0u) == -1);
    CHECK(api_tbl()->event_next(buf, 0u, &len, 0u) == -1);
    CHECK(api_tbl()->event_next(buf, 512u, NULL, 0u) == -1);
}

static void test_sixty_four_and_over(void)
{
    uint8_t buf[APP_SESSION_EVENT_WIRE_MAX];
    uint32_t len = 0;
    uint32_t i;

    for (i = 0u; i < 64u; i++) {
        mk_det(&g_det[i], (float)i, 1.0f, 2.0f, 3.0f, 0.9f, "person");
    }
    produce(64u, 99u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(len == APP_SESSION_EVENT_WIRE_MAX);
    CHECK(rd32(buf, 28) == 64u);

    for (i = 0u; i < 65u; i++) {
        mk_det(&g_det[i], 1.0f, 1.0f, 1.0f, 1.0f, 0.9f, "person");
    }
    produce(65u, 100u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(len == APP_SESSION_EVENT_HEADER_SIZE);
    CHECK(rd32(buf, 4) == APP_SESSION_EVT_KIND_GAP);
    CHECK(rd32(buf, 24) == APP_SESSION_EVT_FLAG_LOST_KNOWN);
    CHECK(rd32(buf, 28) == 0u);
    CHECK(rd32(buf, 32) == 1u);
    CHECK(len == APP_SESSION_EVENT_HEADER_SIZE);
    CHECK(rd32(buf, 4) == APP_SESSION_EVT_KIND_GAP);
    CHECK(rd32(buf, 24) == APP_SESSION_EVT_FLAG_LOST_KNOWN);
    CHECK(rd32(buf, 28) == 0u);
    CHECK(rd32(buf, 32) == 1u);
}

static void test_nonfinite_and_unknown_class(void)
{
    uint8_t buf[APP_SESSION_EVENT_WIRE_MAX];
    uint32_t len = 0;

    mk_det(&g_det[0], 1.0f, 2.0f, 3.0f, 4.0f, 0.5f, "person");
    g_det[0].conf = 1.0f / 0.0f;
    produce(1u, 101u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(rd32(buf, 4) == APP_SESSION_EVT_KIND_GAP);
    CHECK(rd32(buf, 32) == 1u);

    mk_det(&g_det[0], 1.0f, 2.0f, 3.0f, 4.0f, 0.5f, "bird");
    produce(1u, 102u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(rd32(buf, 4) == APP_SESSION_EVT_KIND_GAP);
    CHECK(rd32(buf, 32) == 1u);

    mk_det(&g_det[0], 1.0f, 2.0f, 3.0f, 4.0f, 0.5f, NULL);
    produce(1u, 103u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(rd32(buf, 4) == APP_SESSION_EVT_KIND_GAP);
}

static void test_slow_consumer_loss(void)
{
    uint8_t buf[APP_SESSION_EVENT_WIRE_MAX];
    uint32_t len = 0;
    uint32_t i;
    uint32_t first_seq;
    uint32_t base_dropped;
    app_host_session_status_t st;

    for (i = 0u; i < 8u; i++) {
        mk_det(&g_det[0], (float)i, 1.0f, 1.0f, 1.0f, 0.5f, "person");
        produce(1u, 200u + i);
    }
    app_host_session_status(&st);
    CHECK(st.state == APP_SESSION_STATE_RUNNING);
    base_dropped = st.frames_dropped;

    mk_det(&g_det[0], 9.0f, 1.0f, 1.0f, 1.0f, 0.5f, "person");
    produce(1u, 208u);
    app_host_session_status(&st);
    CHECK(st.frames_dropped == base_dropped + 1u);

    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    first_seq = rd32(buf, 8);
    CHECK(rd32(buf, 24) == 0u);
    for (i = 1u; i < 8u; i++) {
        CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
        CHECK(rd32(buf, 8) == first_seq + i);
    }
    CHECK(rd32(buf, 24) == APP_SESSION_EVT_FLAG_LOST_KNOWN);
    CHECK(rd32(buf, 32) == 1u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 1);

    for (i = 0u; i < 8u; i++) {
        mk_det(&g_det[0], (float)i, 1.0f, 1.0f, 1.0f, 0.5f, "person");
        produce(1u, 300u + i);
    }
    produce(1u, 308u);
    produce(1u, 309u);
    app_host_session_status(&st);
    CHECK(st.frames_dropped == base_dropped + 3u);
    for (i = 0u; i < 8u; i++) {
        CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    }
    CHECK(rd32(buf, 24) == APP_SESSION_EVT_FLAG_LOST_KNOWN);
    CHECK(rd32(buf, 32) == 1u);
}

static void test_model_changed(void)
{
    uint8_t buf[APP_SESSION_EVENT_WIRE_MAX];
    uint32_t len = 0;

    mk_det(&g_det[0], 1.0f, 2.0f, 3.0f, 4.0f, 0.5f, "person");
    produce(1u, 400u);

    set_model(0x5000u, g_three_classes, 3u, "sess-model-v2", "2.0.0");
    produce(1u, 401u);

    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(rd32(buf, 4) == APP_SESSION_EVT_KIND_MODEL_CHANGED);
    CHECK(rd32(buf, 16) == 2u);
    CHECK(rd32(buf, 20) == 2u);
    CHECK(rd32(buf, 24) == APP_SESSION_EVT_FLAG_LOST_KNOWN);
    CHECK(rd32(buf, 32) == 1u);
    CHECK(rd32(buf, 28) == 0u);

    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(rd32(buf, 4) == 1u);
    CHECK(rd32(buf, 16) == 2u);
    CHECK(rd32(buf, 20) == 2u);
    CHECK(rd32(buf, 28) == 1u);
    CHECK(rd32(buf, 60) == 0u);

    CHECK(api_tbl()->class_name(1u, 1u, 0u, buf, sizeof(buf), &len) == -4);
    len = 0;
    CHECK(api_tbl()->class_name(2u, 2u, 0u, buf, sizeof(buf), &len) == 0);
    CHECK(len == 6u);
    CHECK(memcmp(buf, "person", 6u) == 0);
    len = 0;
    CHECK(api_tbl()->class_name(2u, 2u, 2u, buf, sizeof(buf), &len) == 0);
    CHECK(memcmp(buf, "dog", 3u) == 0);
    CHECK(api_tbl()->class_name(2u, 2u, 3u, buf, sizeof(buf), &len) == -1);

    len = 0;
    CHECK(api_tbl()->model_meta(buf, sizeof(buf), &len) == 0);
    CHECK(len == 128u);
    CHECK(rd32(buf, 8) == 2u);
    CHECK(rd32(buf, 12) == 2u);
    CHECK(rd32(buf, 16) == 3u);
    CHECK(rd32(buf, 20) == 0u);
    CHECK(memcmp(buf + 24, "sess-model-v2", 13u) == 0);
    CHECK(buf[24 + 13u] == 0);
    CHECK(memcmp(buf + 24 + 64u, "2.0.0", 5u) == 0);
    len = 0;
    CHECK(api_tbl()->model_meta(buf, 127u, &len) == -2);
}

static void test_model_incompatible_runtime(void)
{
    uint8_t buf[APP_SESSION_EVENT_WIRE_MAX];
    uint32_t len = 0;

    set_model_available(AICAM_FALSE);
    produce(1u, 500u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == -4);
    CHECK(api_tbl()->model_meta(buf, sizeof(buf), &len) == -4);
    CHECK(api_tbl()->class_name(1u, 1u, 0u, buf, sizeof(buf), &len) == -4);

    set_model_available(AICAM_TRUE);
    set_model(0x6000u, g_two_classes, 2u, "back", "3.0.0");
    produce(1u, 501u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(rd32(buf, 4) == APP_SESSION_EVT_KIND_MODEL_CHANGED);
    CHECK(rd32(buf, 16) == 3u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(rd32(buf, 4) == 1u);
}

static void test_model_changed_same_addr(void)
{
    uint8_t buf[APP_SESSION_EVENT_WIRE_MAX];
    uint32_t len = 0;
    uint32_t gen0;
    uint32_t mc0;
    app_host_session_status_t st;

    set_model(0x1000u, g_two_classes, 2u, "stable", "4.0.0");
    produce(1u, 799u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(rd32(buf, 4) == APP_SESSION_EVT_KIND_MODEL_CHANGED);
    produce(1u, 800u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(rd32(buf, 4) == 1u);
    app_host_session_status(&st);
    gen0 = st.model_generation;
    mc0 = st.model_changes;

    set_model(0x1000u, g_three_classes, 3u, "stable", "4.1.0");
    mk_det(&g_det[0], 1.0f, 2.0f, 3.0f, 4.0f, 0.5f, "person");
    produce(1u, 801u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK_MSG(rd32(buf, 4) == APP_SESSION_EVT_KIND_MODEL_CHANGED,
              "same_addr_version_class_change");
    CHECK(rd32(buf, 16) == gen0 + 1u);
    CHECK(rd32(buf, 20) == gen0 + 1u);
    app_host_session_status(&st);
    CHECK(st.model_generation == gen0 + 1u);
    CHECK(st.class_generation == gen0 + 1u);
    CHECK(st.class_count == 3u);
    CHECK(strcmp(st.model_version, "4.1.0") == 0);
    CHECK(st.model_changes == mc0 + 1u);

    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(rd32(buf, 4) == 1u);
    CHECK(rd32(buf, 12) == 801u);
    CHECK(rd32(buf, 16) == gen0 + 1u);
    CHECK(rd32(buf, 20) == gen0 + 1u);
    CHECK(rd32(buf, 60) == 0u);

    len = 0;
    CHECK(api_tbl()->class_name(gen0, gen0, 0u, buf, sizeof(buf), &len) == -4);
    len = 0;
    CHECK(api_tbl()->class_name(gen0 + 1u, gen0 + 1u, 2u, buf, sizeof(buf), &len) == 0);
    CHECK(len == 3u);
    CHECK(memcmp(buf, "dog", 3u) == 0);

    mk_det(&g_det[0], 1.0f, 2.0f, 3.0f, 4.0f, 0.5f, "dog");
    produce(1u, 802u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(rd32(buf, 4) == 1u);
    CHECK(rd32(buf, 12) == 802u);
    CHECK(rd32(buf, 16) == gen0 + 1u);
    CHECK(rd32(buf, 20) == gen0 + 1u);
    CHECK(rd32(buf, 60) == 2u);

    set_model(0x1000u, g_three_classes, 3u, "stable", "4.1.0");
    produce(1u, 803u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK_MSG(rd32(buf, 4) == 1u, "identical_model_no_event");
    CHECK(rd32(buf, 12) == 803u);
    app_host_session_status(&st);
    CHECK(st.model_generation == gen0 + 1u);
    CHECK(st.class_generation == gen0 + 1u);
}

static void test_meta_and_class_validation(void)
{
    uint8_t buf[256];
    uint32_t len = 0;
    uint32_t gen;
    app_host_session_status_t st;

    app_host_session_status(&st);
    gen = st.model_generation;
    CHECK(gen == st.class_generation);

    CHECK(api_tbl()->model_meta(NULL, 128u, &len) == -1);
    CHECK(api_tbl()->model_meta(buf, 128u, NULL) == -1);
    len = 99u;
    CHECK(api_tbl()->class_name(gen, gen, 0u, NULL, 32u, &len) == -1);
    CHECK(api_tbl()->class_name(gen, gen, 0u, buf, 32u, NULL) == -1);
    CHECK(api_tbl()->class_name(gen, gen, 99u, buf, 32u, &len) == -1);
    len = 0;
    CHECK(api_tbl()->class_name(gen, gen, 1u, buf, 3u, &len) == -2);
    len = 0;
    CHECK(api_tbl()->class_name(gen, gen, 1u, buf, 4u, &len) == 0);
    CHECK(len == 3u);
    CHECK(memcmp(buf, "car", 3u) == 0);
    len = 0;
    CHECK(api_tbl()->class_name(gen - 1u, gen - 1u, 0u, buf, 32u, &len) == -4);
}

static void test_log(void)
{
    char log_buf[APP_SESSION_LOG_CAP + 1u];
    size_t n;
    char big[1024];

    memset(big, 'x', sizeof(big));
    big[sizeof(big) - 1u] = '\0';

    CHECK(api_tbl()->log("hello-log") == 0);
    n = app_host_session_take_log(log_buf, sizeof(log_buf));
    CHECK(n == strlen("hello-log"));
    CHECK(strcmp(log_buf, "hello-log") == 0);
    n = app_host_session_take_log(log_buf, sizeof(log_buf));
    CHECK(n == 0u);
    CHECK(api_tbl()->log(big) == 0);
    CHECK(app_host_session_log_truncated() == AICAM_TRUE);
    (void)app_host_session_take_log(log_buf, sizeof(log_buf));
    CHECK(app_host_session_log_truncated() == AICAM_FALSE);
}

static void test_tick_wrap_semantics(void)
{
    uint8_t buf[APP_SESSION_EVENT_WIRE_MAX];
    uint32_t len = 0;

    mk_det(&g_det[0], 1.0f, 2.0f, 3.0f, 4.0f, 0.5f, "person");
    produce(1u, 0xFFFFFF00u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(rd32(buf, 12) == 0xFFFFFF00u);
    CHECK(pop(buf, sizeof(buf), &len, 5u) == 1);
}

static void test_stop_busy_then_settles(void)
{
    app_host_session_stop_out_t stop_out;
    app_host_session_status_t st;
    app_install_record_t removed;
    uint8_t buf[APP_SESSION_EVENT_WIRE_MAX];
    uint32_t len = 0;

    CHECK(api_tbl()->should_stop() == 0);

    memset(&stop_out, 0, sizeof(stop_out));
    CHECK(app_host_session_stop(120u, &stop_out) == APP_SESSION_ERR_BUSY);
    CHECK(stop_out.entry_returned == AICAM_FALSE);
    CHECK(strcmp(stop_out.reason, "entry_not_returned") == 0);
    CHECK(api_tbl()->should_stop() == 1);
    CHECK(api_tbl()->event_next(buf, sizeof(buf), &len, 0u) == -10);

    app_host_session_status(&st);
    CHECK(st.state == APP_SESSION_STATE_STOPPING);
    CHECK(st.app_running == AICAM_TRUE);
    CHECK(app_install_uninstall(&g_session_io, &removed) == APP_INSTALL_ERR_BUSY);

    tb.park_release = 1;
    wait_idle();
    CHECK(app_install_app_running() == AICAM_FALSE);
    CHECK(tb.detach_count == 1);
    CHECK(api_tbl()->event_next(buf, sizeof(buf), &len, 0u) == -3);
    CHECK(app_host_session_stop_requested() == AICAM_FALSE);

    memset(&stop_out, 0, sizeof(stop_out));
    CHECK(app_host_session_stop(10u, &stop_out) == APP_SESSION_ERR_NOT_RUNNING);
}

static void test_cooperative_stop_and_restart(void)
{
    app_host_session_stop_out_t stop_out;
    app_host_session_start_out_t out;
    app_install_record_t rec;
    uint8_t buf[APP_SESSION_EVENT_WIRE_MAX];
    uint32_t len = 0;

    CHECK(install_expected(SESS_PKG, SESS_PKG_LEN, &rec) == APP_INSTALL_OK);
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);

    mk_det(&g_det[0], 1.0f, 2.0f, 3.0f, 4.0f, 0.5f, "person");
    produce(1u, 600u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(rd32(buf, 8) == 0u);
    CHECK(rd32(buf, 16) == 1u);

    tb.park_release = 1;

    memset(&stop_out, 0, sizeof(stop_out));
    CHECK(app_host_session_stop(2000u, &stop_out) == APP_SESSION_OK);
    CHECK(stop_out.entry_returned == AICAM_TRUE);
    CHECK(app_install_app_running() == AICAM_FALSE);

    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);
    mk_det(&g_det[0], 1.0f, 2.0f, 3.0f, 4.0f, 0.5f, "person");
    produce(1u, 601u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(rd32(buf, 8) == 0u);
    CHECK(rd32(buf, 16) == 1u);
    CHECK(rd32(buf, 20) == 1u);

    tb.park_release = 1;
    memset(&stop_out, 0, sizeof(stop_out));
    CHECK(app_host_session_stop(2000u, &stop_out) == APP_SESSION_OK);
}

static void test_entry_consumes_until_stopping(void)
{
    app_host_session_stop_out_t stop_out;
    app_host_session_start_out_t out;
    app_install_record_t rec;

    g_entry_kind_count = 0;
    memset(g_entry_kinds, 0, sizeof(g_entry_kinds));
    tb.body = tb_body_consumer;

    CHECK(install_expected(SESS_PKG, SESS_PKG_LEN, &rec) == APP_INSTALL_OK);
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);

    mk_det(&g_det[0], 1.0f, 2.0f, 3.0f, 4.0f, 0.5f, "person");
    produce(1u, 700u);
    produce(0u, 701u);
    sleep_ms(150);
    CHECK(g_entry_kind_count == 2);
    CHECK(g_entry_kinds[0] == 1);
    CHECK(g_entry_kinds[1] == 1);

    memset(&stop_out, 0, sizeof(stop_out));
    CHECK(app_host_session_stop(2000u, &stop_out) == APP_SESSION_OK);
    CHECK(stop_out.entry_returned == AICAM_TRUE);
    CHECK(g_entry_kind_count == 2);
    tb.body = NULL;
}

static void test_attach_failure_refused(void)
{
    app_host_session_start_out_t out;
    app_host_session_stop_out_t stop_out;
    app_host_session_status_t st;
    app_install_record_t rec;
    int d0;

    CHECK(install_expected(SESS_PKG, SESS_PKG_LEN, &rec) == APP_INSTALL_OK);
    tb.fail_attach = AICAM_TRUE;
    d0 = tb.detach_count;
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_ERR_RUNTIME_UNAVAILABLE);
    CHECK_MSG(strcmp(out.reason, "ai_attach_failed") == 0, "attach_fail_refused");
    CHECK(tb.detach_count == d0);
    CHECK(app_install_app_running() == AICAM_FALSE);
    app_host_session_status(&st);
    CHECK(st.state == APP_SESSION_STATE_IDLE);
    CHECK(strcmp(st.last_reason, "ai_attach_failed") == 0);

    tb.fail_attach = AICAM_FALSE;
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);
    tb.park_release = 1;
    memset(&stop_out, 0, sizeof(stop_out));
    CHECK(app_host_session_stop(2000u, &stop_out) == APP_SESSION_OK);
}

static void test_spawn_failure_refused(void)
{
    app_host_session_start_out_t out;
    app_host_session_stop_out_t stop_out;
    app_host_session_status_t st;
    app_install_record_t rec;
    int scrub0;

    CHECK(install_expected(SESS_PKG, SESS_PKG_LEN, &rec) == APP_INSTALL_OK);
    tb.fail_spawn = 1;
    scrub0 = tb_scrub_count_get();
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_ERR_RUNTIME_UNAVAILABLE);
    CHECK_MSG(strcmp(out.reason, "entry_spawn_failed") == 0, "spawn_fail_refused");
    CHECK(app_install_app_running() == AICAM_FALSE);
    app_host_session_status(&st);
    CHECK(st.state == APP_SESSION_STATE_IDLE);
    CHECK(st.app_running == AICAM_FALSE);
    CHECK(strcmp(st.last_reason, "entry_spawn_failed") == 0);
    CHECK(tb_scrub_count_get() == scrub0 + 1);
    tb.fail_spawn = 0;

    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);
    tb.park_release = 1;
    memset(&stop_out, 0, sizeof(stop_out));
    CHECK(app_host_session_stop(2000u, &stop_out) == APP_SESSION_OK);
}

static void test_start_blocked_until_teardown_done(void)
{
    app_host_session_start_out_t out;
    app_host_session_stop_out_t stop_out;
    app_host_session_status_t st;
    app_install_record_t rec;
    app_install_record_t removed;
    start_call_t calls[4];
    pthread_t tids[4];
    int i;
    int spins;
    int scrub_before;

    CHECK(install_expected(SESS_PKG, SESS_PKG_LEN, &rec) == APP_INSTALL_OK);
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);

    scrub_before = tb_scrub_count_get();
    tb_scrub_hold_set(1);
    tb.park_release = 1;
    spins = 0;
    while (tb_scrub_count_get() < scrub_before + 1) {
        sleep_ms(2);
        spins++;
        if (spins > 5000) {
            break;
        }
    }
    CHECK(tb_scrub_count_get() >= scrub_before + 1);

    app_host_session_status(&st);
    CHECK(st.state == APP_SESSION_STATE_STOPPING);
    CHECK(st.app_running == AICAM_TRUE);
    CHECK(app_install_uninstall(&g_session_io, &removed) == APP_INSTALL_ERR_BUSY);

    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_ERR_BUSY);
    CHECK_MSG(strcmp(out.reason, "session_active") == 0, "start_busy_during_cleanup");

    for (i = 0; i < 4; i++) {
        calls[i].rc = APP_SESSION_OK;
        calls[i].reason[0] = '\0';
        CHECK(pthread_create(&tids[i], NULL, start_thread_fn, &calls[i]) == 0);
    }
    for (i = 0; i < 4; i++) {
        pthread_join(tids[i], NULL);
        CHECK(calls[i].rc == APP_SESSION_ERR_BUSY);
    }
    app_host_session_status(&st);
    CHECK(st.state == APP_SESSION_STATE_STOPPING);
    CHECK(app_install_app_running() == AICAM_TRUE);

    tb_scrub_hold_set(0);
    wait_idle();
    CHECK(app_install_app_running() == AICAM_FALSE);
    CHECK(tb_scrub_count_get() == scrub_before + 1);

    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);
    tb.park_release = 1;
    memset(&stop_out, 0, sizeof(stop_out));
    CHECK(app_host_session_stop(2000u, &stop_out) == APP_SESSION_OK);
    CHECK(stop_out.entry_returned == AICAM_TRUE);
}

static int wait_thread_exit_count(int target)
{
    int spins = 0;

    while (tb.thread_exited < target) {
        sleep_ms(2);
        spins++;
        if (spins > 5000) {
            break;
        }
    }
    return tb.thread_exited;
}

static void test_start_blocked_until_worker_terminal(void)
{
    app_host_session_start_out_t out;
    app_host_session_stop_out_t stop_out;
    app_host_session_status_t st;
    app_install_record_t rec;
    app_install_record_t removed;
    start_call_t calls[4];
    pthread_t tids[4];
    int spawns0;
    int exited0;
    int i;

    tb.park_release = 0;
    tb.terminal_hold = 0;
    CHECK(install_expected(SESS_PKG, SESS_PKG_LEN, &rec) == APP_INSTALL_OK);
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);
    spawns0 = tb.thread_started;
    exited0 = tb.thread_exited;

    tb.terminal_hold = 1;
    tb.park_release = 1;
    CHECK(wait_thread_exit_count(exited0 + 1) == exited0 + 1);

    app_host_session_status(&st);
    CHECK_MSG(st.state == APP_SESSION_STATE_STOPPING, "latch_keeps_stopping");
    CHECK_MSG(st.app_running == AICAM_TRUE, "latch_keeps_app_running");
    CHECK(app_install_app_running() == AICAM_TRUE);
    CHECK_MSG(app_install_uninstall(&g_session_io, &removed) == APP_INSTALL_ERR_BUSY,
              "uninstall_blocked_until_terminal");
    memset(&stop_out, 0, sizeof(stop_out));
    CHECK_MSG(app_host_session_stop(20u, &stop_out) == APP_SESSION_ERR_BUSY,
              "stop_busy_until_terminal");

    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_ERR_BUSY);
    CHECK_MSG(strcmp(out.reason, "session_active") == 0, "start_busy_until_terminal");
    CHECK_MSG(tb.thread_started == spawns0, "no_respawn_until_terminal");

    for (i = 0; i < 4; i++) {
        calls[i].rc = APP_SESSION_OK;
        calls[i].reason[0] = '\0';
        CHECK(pthread_create(&tids[i], NULL, start_thread_fn, &calls[i]) == 0);
    }
    for (i = 0; i < 4; i++) {
        pthread_join(tids[i], NULL);
        CHECK_MSG(calls[i].rc == APP_SESSION_ERR_BUSY,
                  "concurrent_start_busy_until_terminal");
    }
    app_host_session_status(&st);
    CHECK(st.state == APP_SESSION_STATE_STOPPING);
    CHECK(app_install_app_running() == AICAM_TRUE);
    CHECK_MSG(tb.thread_started == spawns0, "stack_not_reused_until_terminal");

    tb.terminal_hold = 0;
    wait_idle();
    app_host_session_status(&st);
    CHECK_MSG(st.state == APP_SESSION_STATE_IDLE, "status_poll_reaps_terminal");
    CHECK(app_install_app_running() == AICAM_FALSE);

    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);
    CHECK_MSG(tb.thread_started == spawns0 + 1, "respawn_after_terminal");
    app_host_session_status(&st);
    CHECK(st.state == APP_SESSION_STATE_RUNNING);

    tb.terminal_hold = 1;
    tb.park_release = 1;
    CHECK(wait_thread_exit_count(exited0 + 2) == exited0 + 2);
    app_host_session_status(&st);
    CHECK_MSG(st.state == APP_SESSION_STATE_STOPPING, "second_cycle_latched");

    tb.terminal_hold = 0;
    memset(&stop_out, 0, sizeof(stop_out));
    CHECK(app_host_session_stop(2000u, &stop_out) == APP_SESSION_OK);
    CHECK(stop_out.entry_returned == AICAM_TRUE);
    app_host_session_status(&st);
    CHECK(st.state == APP_SESSION_STATE_IDLE);
}

static app_host_session_backend_t g_dev_session_backend;

static void dev_backend_prepare(void)
{
    const app_host_session_backend_t *dev = app_host_session_device_backend();

    CHECK(dev != NULL);
    g_dev_session_backend = *dev;
    g_dev_session_backend.stage_base = tb_stage_base;
    g_dev_session_backend.stage_commit = tb_stage_commit;
    g_dev_session_backend.invoke_entry = tb_invoke_entry;
    g_dev_session_backend.scrub = tb_scrub;
    g_dev_session_backend.entry_spawn = tb_entry_spawn;
    g_dev_session_backend.entry_exited = tb_entry_exited;
    CHECK(g_dev_session_backend.ai_model_view == dev->ai_model_view);
    CHECK(g_dev_session_backend.ai_attach == dev->ai_attach);
    CHECK(g_dev_session_backend.ai_detach == dev->ai_detach);
}

static void dev_seam_fill_model(uintptr_t config_addr,
                                uintptr_t model_addr, uint32_t model_size,
                                const char *name, const char *version)
{
    nn_model_info_t *info = dev_seam_model_info();

    memset((void *)info, 0, sizeof(*info));
    snprintf(info->name, sizeof(info->name), "%s", name);
    snprintf(info->version, sizeof(info->version), "%s", version);
    snprintf(info->postprocess_type, sizeof(info->postprocess_type), "od_yolov8");
    info->config_ptr = config_addr;
    info->model_ptr = model_addr;
    info->model_size = model_size;
}

static void test_dev_model_digest_bounds(void)
{
    const app_host_session_backend_t *dev = app_host_session_device_backend();
    app_session_model_view_t view;
    app_session_model_view_t cached;
    nn_model_info_t *info;
    long page;
    uint8_t *region;
    const size_t region_bytes = 2u * 1024u * 1024u;
    const size_t strip_bytes = 128u * 1024u;
    uint32_t t0;
    uint32_t t1;
    uint32_t gen0;
    int i;

    CHECK(dev != NULL);
    dev_seam_reset();
    info = dev_seam_model_info();
    dev_seam_fill_model(0u, 0x91000000u, 4096u, "bound-model", "1.0.0");
    dev_seam_set_state(NN_STATE_READY);

    page = sysconf(_SC_PAGESIZE);
    region = mmap(NULL, region_bytes + (size_t)page, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(region != MAP_FAILED);
    CHECK(mprotect(region + region_bytes, (size_t)page, PROT_NONE) == 0);

    CHECK_MSG(dev->ai_model_view(NULL, &view) == AICAM_FALSE, "null_config_refused");

    memset(region + region_bytes - strip_bytes, 'x', strip_bytes);
    dev_seam_fill_model((uintptr_t)(region + region_bytes - strip_bytes),
                        0x91000000u, 4096u, "bound-model", "1.0.0");
    CHECK_MSG(dev->ai_model_view(NULL, &view) == AICAM_FALSE, "unterminated_config_refused");

    t0 = mono_ms();
    for (i = 0; i < 1000; i++) {
        if (dev->ai_model_view(NULL, &view) != AICAM_FALSE) {
            break;
        }
    }
    t1 = mono_ms();
    CHECK_MSG(i == 1000, "unterminated_config_stable_fail");
    CHECK_MSG((t1 - t0) < 5000u, "unterminated_config_bounded_time");

    dev_seam_set_info_rc(-1);
    CHECK_MSG(dev->ai_model_view(NULL, &view) == AICAM_FALSE, "info_failure_refused");
    dev_seam_set_info_rc(0);

    dev_seam_set_state(NN_STATE_INIT);
    CHECK_MSG(dev->ai_model_view(NULL, &view) == AICAM_FALSE, "not_ready_refused");
    dev_seam_set_state(NN_STATE_READY);

    memset((void *)info->name, 'n', sizeof(info->name));
    CHECK_MSG(dev->ai_model_view(NULL, &view) == AICAM_FALSE, "unterminated_name_refused");
    snprintf((char *)info->name, sizeof(info->name), "bound-model");

    region[0] = '{';
    memcpy(region + 1, "\"postprocess_params\":{\"class_names\":[\"person\",\"car\"]}}", 54);
    memset(region + 55, ' ', 59943);
    region[59998] = '\0';
    dev_seam_fill_model((uintptr_t)region, 0x91000000u, 4096u, "bound-model", "1.0.0");
    t0 = mono_ms();
    CHECK_MSG(dev->ai_model_view(NULL, &view) == AICAM_TRUE, "large_config_accepted");
    t1 = mono_ms();
    CHECK_MSG((t1 - t0) < 2000u, "large_config_bounded_time");
    CHECK(view.content_digest != 0u);
    CHECK(view.class_count == 2u);
    CHECK(strcmp(view.class_names[0], "person") == 0);
    CHECK(strcmp(view.class_names[1], "car") == 0);
    CHECK(strcmp(view.model_name, "bound-model") == 0);
    cached = view;

    CHECK(mprotect(region, region_bytes + (size_t)page, PROT_NONE) == 0);
    CHECK_MSG(dev->ai_model_view(NULL, &view) == AICAM_TRUE, "hot_path_uses_cache_key");
    CHECK(view.content_digest == cached.content_digest);
    CHECK(mprotect(region, region_bytes + (size_t)page, PROT_READ | PROT_WRITE) == 0);

    region[8] = 'x';
    CHECK_MSG(dev->ai_model_view(NULL, &view) == AICAM_TRUE, "content_mutation_key_hit");
    CHECK(view.content_digest == cached.content_digest);

    gen0 = video_ai_node_model_load_generation();
    CHECK(video_ai_node_load_model(dev_seam_ai_node(), 0u) == AICAM_OK);
    CHECK(video_ai_node_model_load_generation() == gen0 + 1u);
    CHECK_MSG(dev->ai_model_view(NULL, &view) == AICAM_TRUE, "reload_recomputes");
    CHECK_MSG(view.content_digest != cached.content_digest, "generation_changes_digest");

    munmap(region, region_bytes + (size_t)page);
}

static void test_dev_load_generation_calibration(void)
{
    video_node_t *node;
    uint32_t gen;

    dev_seam_reset();
    node = dev_seam_ai_node();
    CHECK(node != NULL);
    gen = video_ai_node_model_load_generation();

    CHECK(video_ai_node_load_model(node, 0u) == AICAM_OK);
    CHECK_MSG(video_ai_node_model_load_generation() == gen + 1u, "active_load_counts_once");
    CHECK(dev_seam_load_calls() == 1u);
    CHECK(dev_seam_last_load_ptr() == (uintptr_t)AI_1_BASE + 1024u);

    CHECK(video_ai_node_load_model(node, 0u) == AICAM_OK);
    CHECK_MSG(video_ai_node_model_load_generation() == gen + 2u, "second_active_load_counts_once");
    CHECK(dev_seam_load_calls() == 2u);

    dev_seam_set_load_rc(-1);
    CHECK(video_ai_node_load_model(node, 0u) == AICAM_ERROR);
    CHECK_MSG(video_ai_node_model_load_generation() == gen + 2u, "failed_load_no_count");
    CHECK(dev_seam_load_calls() == 3u);

    dev_seam_set_load_rc(0);
    CHECK(video_ai_node_load_model(node, (uintptr_t)0x92000000u) == AICAM_OK);
    CHECK_MSG(video_ai_node_model_load_generation() == gen + 3u, "explicit_load_counts_once");
    CHECK(dev_seam_last_load_ptr() == (uintptr_t)0x92000000u);
    CHECK(dev_seam_active_switch_calls() >= 1u);
}

static void test_dev_model_changed_via_load(void)
{
    static char dev_cfg[256];
    app_host_session_start_out_t out;
    app_host_session_stop_out_t stop_out;
    app_install_record_t rec;
    uint8_t buf[APP_SESSION_EVENT_WIRE_MAX];
    uint32_t len = 0;
    app_host_session_status_t st;
    uint32_t gen_before;

    dev_seam_reset();
    dev_seam_fill_model(0u, 0x91000000u, 0x200000u, "load-model", "6.0.0");
    snprintf(dev_cfg, sizeof(dev_cfg),
             "{\"postprocess_params\":{\"class_names\":[\"person\",\"car\"]}}");
    dev_seam_model_info()->config_ptr = (uintptr_t)dev_cfg;
    dev_seam_set_state(NN_STATE_READY);
    CHECK(video_ai_node_load_model(dev_seam_ai_node(), 0u) == AICAM_OK);

    dev_backend_prepare();
    app_host_session_init(&g_dev_session_backend);
    mem_reset();
    mem_io_init(&g_session_io);
    app_install_boot_resolve_reset();
    app_install_set_app_running(AICAM_FALSE);
    fill_policies();
    g_trust_mode_session = AICAM_TRUE;
    CHECK(install_expected(SESS_PKG, SESS_PKG_LEN, &rec) == APP_INSTALL_OK);
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);

    mk_det(&g_det[0], 1.0f, 2.0f, 3.0f, 4.0f, 0.5f, "person");
    produce(1u, 900u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK_MSG(rd32(buf, 4) == 1u, "dev_view_frame_after_start");
    app_host_session_status(&st);
    CHECK(st.model_changes == 1u);
    CHECK(st.class_count == 2u);

    CHECK(video_ai_node_load_model(dev_seam_ai_node(), 0u) == AICAM_OK);
    produce(1u, 901u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK_MSG(rd32(buf, 4) == APP_SESSION_EVT_KIND_MODEL_CHANGED,
              "same_metadata_reload_model_changed");
    app_host_session_status(&st);
    CHECK(st.model_changes == 2u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(rd32(buf, 4) == 1u);

    produce(1u, 902u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK_MSG(rd32(buf, 4) == 1u, "no_reload_no_model_changed");

    dev_seam_set_load_rc(-1);
    CHECK(video_ai_node_load_model(dev_seam_ai_node(), 0u) == AICAM_ERROR);
    dev_seam_set_load_rc(0);
    app_host_session_status(&st);
    gen_before = st.model_generation;
    produce(1u, 903u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK_MSG(rd32(buf, 4) == 1u, "failed_load_no_model_changed");
    app_host_session_status(&st);
    CHECK(st.model_generation == gen_before);

    tb.park_release = 1;
    memset(&stop_out, 0, sizeof(stop_out));
    CHECK(app_host_session_stop(2000u, &stop_out) == APP_SESSION_OK);
    CHECK(stop_out.entry_returned == AICAM_TRUE);
}

int main(void)
{
    reset_all();
    test_container_version();
    test_unauthorized_before_start();
    test_start_refusals();
    reset_all();
    test_start_success_and_double();
    test_frame_layout();
    test_empty_frame_and_no_event();
    test_buffer_too_small();
    test_sixty_four_and_over();
    test_nonfinite_and_unknown_class();
    test_slow_consumer_loss();
    test_model_changed();
    test_model_incompatible_runtime();
    test_model_changed_same_addr();
    test_meta_and_class_validation();
    test_log();
    test_tick_wrap_semantics();
    test_stop_busy_then_settles();
    test_cooperative_stop_and_restart();
    test_entry_consumes_until_stopping();
    test_attach_failure_refused();
    test_spawn_failure_refused();
    test_start_blocked_until_teardown_done();
    test_start_blocked_until_worker_terminal();

    reset_all();
    test_dev_model_digest_bounds();
    test_dev_load_generation_calibration();
    reset_all();
    test_dev_model_changed_via_load();

    printf("app_host_session: total=%d fail=%d\n", g_total, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
