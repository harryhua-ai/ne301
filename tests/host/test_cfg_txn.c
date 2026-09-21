#include "cfg_txn.h"
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);            \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

typedef struct {
    uint32_t a;
    uint32_t b;
    uint8_t  c;
    uint32_t d;
} test_cfg_t;

#define CANON_BYTES 64u

typedef struct {
    uint32_t head;
    char     name[16];
    char     mac[18];
    uint8_t  tail[CANON_BYTES - 4 - 16 - 18];
} wide_cfg_t;

typedef struct {
    int lock_calls;
    int unlock_calls;
    int held;
    int fail_lock;
} test_lock_t;

static aicam_bool_t tl_lock(void *user) {
    test_lock_t *l = (test_lock_t *)user;
    if (l->fail_lock > 0) {
        l->fail_lock--;
        return AICAM_FALSE;
    }
    if (l->held) return AICAM_FALSE;
    l->lock_calls++;
    l->held = 1;
    return AICAM_TRUE;
}

static void tl_unlock(void *user) {
    test_lock_t *l = (test_lock_t *)user;
    l->unlock_calls++;
    l->held = 0;
}

typedef enum { EV_LOCK, EV_PERSIST, EV_PUBLISH, EV_UNLOCK } ev_kind_t;

typedef struct {
    ev_kind_t kind[64];
    int who[64];
    int n;
} ev_log_t;

static ev_log_t g_ev;

static void ev_reset(void) { g_ev.n = 0; }
static void ev_push(ev_kind_t k, int who) {
    if (g_ev.n < 64) {
        g_ev.kind[g_ev.n] = k;
        g_ev.who[g_ev.n] = who;
        g_ev.n++;
    }
}

static test_lock_t g_lk;
static cfg_txn_lock_t g_lock;
static int g_ev_writer;

static aicam_bool_t ev_lock(void *user) {
    if (!tl_lock(user)) return AICAM_FALSE;
    ev_push(EV_LOCK, g_ev_writer);
    return AICAM_TRUE;
}

static volatile uint32_t *g_seq_ref;
static uint32_t g_seq_at_unlock;
static test_cfg_t *g_canon_ref;
static uint32_t g_canon_a_at_unlock;

static void ev_unlock(void *user) {
    ev_push(EV_UNLOCK, g_ev_writer);
    if (g_seq_ref) g_seq_at_unlock = *g_seq_ref;
    if (g_canon_ref) g_canon_a_at_unlock = g_canon_ref->a;
    tl_unlock(user);
}

static uint8_t g_persisted[256];
static size_t g_persisted_len;
static int g_persist_calls;
static uint32_t g_fake_blob_gen;

static aicam_result_t ev_persist(void *user, const void *candidate, size_t n,
                                 uint32_t *generation_out) {
    (void)user;
    if (n > sizeof(g_persisted)) return AICAM_ERROR_INVALID_PARAM;
    ev_push(EV_PERSIST, g_ev_writer);
    memcpy(g_persisted, candidate, n);
    g_persisted_len = n;
    g_persist_calls++;
    g_fake_blob_gen++;
    if (generation_out) *generation_out = g_fake_blob_gen;
    return AICAM_OK;
}

static aicam_result_t persist_fail_for_test(void *user, const void *candidate, size_t n,
                                            uint32_t *generation_out) {
    (void)user;
    (void)candidate;
    (void)n;
    (void)generation_out;
    return AICAM_ERROR_IO;
}

static void test_commit_replace_persist_publish_order(void) {
    static test_cfg_t canonical;
    static volatile uint32_t seq = 0;
    cfg_txn_t t;
    cfg_txn_init(&t, &canonical, &seq, sizeof(canonical));
    memset(&g_lk, 0, sizeof(g_lk));
    g_lock.ctx = &g_lk;
    g_lock.lock = ev_lock;
    g_lock.unlock = ev_unlock;
    g_seq_ref = &seq;
    g_canon_ref = &canonical;
    g_seq_at_unlock = 0;
    g_canon_a_at_unlock = 0;
    ev_reset();
    g_persist_calls = 0;
    g_fake_blob_gen = 0;

    test_cfg_t input = { 5, 6, 7, 8 };
    test_cfg_t scratch;
    CHECK(cfg_txn_commit_replace(&t, &g_lock, &scratch, sizeof(scratch), 0,
                                 sizeof(input), &input,
                                 ev_persist, NULL, NULL) == AICAM_OK);

    CHECK(g_ev.n == 3);
    CHECK(g_ev.kind[0] == EV_LOCK);
    CHECK(g_ev.kind[1] == EV_PERSIST);
    CHECK(g_ev.kind[2] == EV_UNLOCK);
    CHECK(g_seq_at_unlock == 2);
    CHECK(g_canon_a_at_unlock == 5);
    CHECK(seq == 2);
    test_cfg_t pt;
    memcpy(&pt, g_persisted, sizeof(pt));
    CHECK(canonical.a == 5 && canonical.d == 8);
    CHECK(pt.a == 5 && pt.d == 8);
}

static void mac_rewrite_patch(void *member, size_t size, void *user) {
    const char *mac = (const char *)user;
    memcpy(member, mac, size);
}

static void test_commit_patch_changes_only_target_member(void) {
    static wide_cfg_t canonical;
    static volatile uint32_t seq = 0;
    cfg_txn_t t;
    cfg_txn_init(&t, &canonical, &seq, sizeof(canonical));

    uint8_t *p = (uint8_t *)&canonical;
    for (uint32_t i = 0; i < sizeof(canonical); i++) p[i] = (uint8_t)(i * 37u + 11u);

    wide_cfg_t expected = canonical;
    memcpy(expected.mac, "11:22:33:44:55:66", 18);

    static wide_cfg_t scratch;
    memset(&scratch, 0, sizeof(scratch));

    CHECK(cfg_txn_commit_patch(&t, &g_lock, &scratch, sizeof(canonical),
                               offsetof(wide_cfg_t, mac), sizeof(canonical.mac),
                               NULL, NULL,
                               ev_persist, NULL, NULL) == AICAM_ERROR_INVALID_PARAM);

    CHECK(cfg_txn_commit_replace(&t, &g_lock, &scratch, sizeof(canonical),
                                 offsetof(wide_cfg_t, mac), sizeof(canonical.mac),
                                 NULL, ev_persist, NULL, NULL) == AICAM_ERROR_INVALID_PARAM);

    CHECK(cfg_txn_commit_patch(&t, &g_lock, &scratch, sizeof(canonical),
                               offsetof(wide_cfg_t, mac), sizeof(canonical.mac),
                               mac_rewrite_patch, (void *)"11:22:33:44:55:66",
                               ev_persist, NULL, NULL) == AICAM_OK);

    CHECK(memcmp(&canonical, &expected, sizeof(canonical)) == 0);
    CHECK(memcmp(g_persisted, &expected, sizeof(expected)) == 0);
    CHECK(seq == 2);

    CHECK(cfg_txn_commit_replace(&t, &g_lock, &scratch, sizeof(canonical),
                                 sizeof(canonical), 1,
                                 canonical.mac, ev_persist, NULL, NULL) == AICAM_ERROR_INVALID_PARAM);

    CHECK(cfg_txn_commit_replace(&t, &g_lock, &scratch, sizeof(canonical),
                                 offsetof(wide_cfg_t, mac), 0,
                                 canonical.mac, ev_persist, NULL, NULL) == AICAM_ERROR_INVALID_PARAM);
}

static void test_replace_member_keeps_rest_byte_identical(void) {
    static wide_cfg_t canonical;
    static volatile uint32_t seq = 0;
    cfg_txn_t t;
    cfg_txn_init(&t, &canonical, &seq, sizeof(canonical));

    uint8_t *p = (uint8_t *)&canonical;
    for (uint32_t i = 0; i < sizeof(canonical); i++) p[i] = (uint8_t)(i * 53u + 7u);

    wide_cfg_t expected = canonical;
    memcpy(expected.name, "NE301-CAMERA-01", 16);

    static wide_cfg_t scratch;
    const char *new_name = "NE301-CAMERA-01";
    CHECK(cfg_txn_commit_replace(&t, &g_lock, &scratch, sizeof(canonical),
                                 offsetof(wide_cfg_t, name), sizeof(canonical.name),
                                 new_name, ev_persist, NULL, NULL) == AICAM_OK);

    CHECK(memcmp(&canonical, &expected, sizeof(canonical)) == 0);
    CHECK(memcmp(g_persisted, &expected, sizeof(expected)) == 0);
    CHECK(seq == 2);
}

static void test_persist_failure_keeps_canonical_bytes_identical(void) {
    static wide_cfg_t canonical;
    static volatile uint32_t seq = 0;
    cfg_txn_t t;
    cfg_txn_init(&t, &canonical, &seq, sizeof(canonical));

    uint8_t *p = (uint8_t *)&canonical;
    for (uint32_t i = 0; i < sizeof(canonical); i++) p[i] = (uint8_t)(i * 29u + 3u);

    wide_cfg_t before = canonical;
    static wide_cfg_t scratch;

    const char *new_name = "MIGRATED-NAME-1";
    CHECK(cfg_txn_commit_replace(&t, &g_lock, &scratch, sizeof(canonical),
                                 offsetof(wide_cfg_t, name), sizeof(canonical.name),
                                 new_name, persist_fail_for_test, NULL, NULL) == AICAM_ERROR_IO);
    CHECK(memcmp(&canonical, &before, sizeof(canonical)) == 0);
    CHECK(seq == 0);
    CHECK(g_lk.held == 0);

    CHECK(cfg_txn_commit_replace(&t, &g_lock, &scratch, sizeof(canonical),
                                 offsetof(wide_cfg_t, name), sizeof(canonical.name),
                                 new_name, ev_persist, NULL, NULL) == AICAM_OK);
    wide_cfg_t pp;
    memcpy(&pp, g_persisted, sizeof(pp));
    CHECK(memcmp(canonical.name, new_name, sizeof(canonical.name)) == 0);
    CHECK(memcmp(pp.tail, before.tail, sizeof(before.tail)) == 0);
    CHECK(memcmp(pp.mac, before.mac, sizeof(before.mac)) == 0);
    CHECK(pp.head == before.head);
}

static void qs_generate_name_for_test(char *name, size_t len, const char *mac) {
    unsigned int v[6] = {0};
    if (sscanf(mac, "%02X:%02X:%02X:%02X:%02X:%02X",
               &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) {
        snprintf(name, len, "AICAM-UNKNOWN");
        return;
    }
    snprintf(name, len, "AICAM-%02X%02X%02X", v[3], v[4], v[5]);
}

static void name_migration_patch(void *member, size_t size, void *user) {
    (void)size;
    wide_cfg_t *m = (wide_cfg_t *)member;
    if (memcmp(m->name, "AICAM-000000\0\0\0\0", 12) == 0) {
        qs_generate_name_for_test(m->name, sizeof(m->name), m->mac);
    }
    (void)user;
}

static void test_device_name_migration_candidate_semantics(void) {
    static wide_cfg_t canonical;
    static volatile uint32_t seq = 0;
    cfg_txn_t t;
    cfg_txn_init(&t, &canonical, &seq, sizeof(canonical));

    memset(&canonical, 0, sizeof(canonical));
    canonical.head = 0xC0FFEE00u;
    memcpy(canonical.name, "AICAM-000000", 12);
    memcpy(canonical.mac, "94:E6:F7:AA:BB:CC", 18);
    for (uint32_t i = 0; i < sizeof(canonical.tail); i++) canonical.tail[i] = (uint8_t)(i + 1);

    wide_cfg_t before = canonical;
    static wide_cfg_t scratch;

    CHECK(cfg_txn_commit_patch(&t, &g_lock, &scratch, sizeof(canonical),
                               0, sizeof(canonical),
                               name_migration_patch, NULL,
                               persist_fail_for_test, NULL, NULL) == AICAM_ERROR_IO);
    CHECK(memcmp(&canonical, &before, sizeof(canonical)) == 0);

    CHECK(cfg_txn_commit_patch(&t, &g_lock, &scratch, sizeof(canonical),
                               0, sizeof(canonical),
                               name_migration_patch, NULL,
                               ev_persist, NULL, NULL) == AICAM_OK);

    CHECK(memcmp(canonical.name, "AICAM-AABBCC", 12) == 0);
    CHECK(canonical.head == before.head);
    CHECK(memcmp(canonical.mac, before.mac, sizeof(canonical.mac)) == 0);
    CHECK(memcmp(canonical.tail, before.tail, sizeof(canonical.tail)) == 0);
    CHECK(memcmp(g_persisted, &canonical, sizeof(canonical)) == 0);
}

static void test_second_writer_cannot_persist_inside_first_window(void) {
    static test_cfg_t canonical = { 1, 2, 3, 4 };
    static volatile uint32_t seq = 0;
    cfg_txn_t t;
    cfg_txn_init(&t, &canonical, &seq, sizeof(canonical));
    memset(&g_lk, 0, sizeof(g_lk));
    g_lock.ctx = &g_lk;
    g_lock.lock = ev_lock;
    g_lock.unlock = ev_unlock;
    ev_reset();
    g_persist_calls = 0;

    test_cfg_t a = { 10, 10, 10, 10 };
    test_cfg_t b = { 20, 20, 20, 20 };
    static test_cfg_t scratch;

    g_lk.held = 1;
    g_ev_writer = 1;
    ev_push(EV_LOCK, 1);
    ev_push(EV_PERSIST, 1);
    memcpy(&g_persisted, &a, sizeof(a));

    g_ev_writer = 2;
    CHECK(cfg_txn_commit_replace(&t, &g_lock, &scratch, sizeof(b), 0,
                                 sizeof(b), &b,
                                 ev_persist, NULL, NULL) == AICAM_ERROR_BUSY);
    CHECK(g_persist_calls == 0);

    g_ev_writer = 1;
    g_seq_ref = &seq;
    cfg_txn_publish(&t, &a, sizeof(a), 0);
    ev_push(EV_UNLOCK, 1);
    g_lk.held = 0;

    test_cfg_t pt;
    CHECK(canonical.a == 10);
    memcpy(&pt, g_persisted, sizeof(pt));
    CHECK(pt.a == 10);
    CHECK(seq == 2);

    g_ev_writer = 2;
    CHECK(cfg_txn_commit_replace(&t, &g_lock, &scratch, sizeof(b), 0,
                                 sizeof(b), &b,
                                 ev_persist, NULL, NULL) == AICAM_OK);
    CHECK(canonical.a == 20);
    memcpy(&pt, g_persisted, sizeof(pt));
    CHECK(pt.a == 20);
    CHECK(seq == 4);
}

static void test_capture_pairs_candidate_with_generation(void) {
    static test_cfg_t canonical = { 1, 2, 3, 4 };
    static volatile uint32_t seq = 0;
    cfg_txn_t t;
    cfg_txn_init(&t, &canonical, &seq, sizeof(canonical));
    memset(&g_lk, 0, sizeof(g_lk));
    g_lock.ctx = &g_lk;
    g_lock.lock = ev_lock;
    g_lock.unlock = ev_unlock;
    g_persist_calls = 0;
    g_fake_blob_gen = 9;

    static test_cfg_t scratch;
    static test_cfg_t capture_a;
    static test_cfg_t capture_b;
    cfg_txn_commit_capture_t cap_a = { &capture_a, sizeof(capture_a), 0 };
    cfg_txn_commit_capture_t cap_b = { &capture_b, sizeof(capture_b), 0 };

    test_cfg_t a = { 10, 10, 10, 10 };
    test_cfg_t b = { 20, 20, 20, 20 };

    CHECK(cfg_txn_commit_replace(&t, &g_lock, &scratch, sizeof(a), 0,
                                 sizeof(a), &a, ev_persist, NULL, &cap_a) == AICAM_OK);
    CHECK(cap_a.generation == 10u);
    CHECK(memcmp(&capture_a, &a, sizeof(a)) == 0);

    CHECK(cfg_txn_commit_replace(&t, &g_lock, &scratch, sizeof(b), 0,
                                 sizeof(b), &b, ev_persist, NULL, &cap_b) == AICAM_OK);
    CHECK(cap_b.generation == 11u);
    CHECK(memcmp(&capture_b, &b, sizeof(b)) == 0);

    CHECK(cap_a.generation == 10u);
    CHECK(memcmp(&capture_a, &a, sizeof(a)) == 0);
    CHECK(canonical.a == 20u);
}

static void test_capture_not_filled_on_persist_failure(void) {
    static test_cfg_t canonical = { 1, 2, 3, 4 };
    static volatile uint32_t seq = 0;
    cfg_txn_t t;
    cfg_txn_init(&t, &canonical, &seq, sizeof(canonical));
    memset(&g_lk, 0, sizeof(g_lk));
    g_lock.ctx = &g_lk;
    g_lock.lock = ev_lock;
    g_lock.unlock = ev_unlock;
    g_fake_blob_gen = 100;

    static test_cfg_t scratch;
    static test_cfg_t capture_buf;
    cfg_txn_commit_capture_t cap = { &capture_buf, sizeof(capture_buf), 0 };
    test_cfg_t input = { 99, 99, 99, 99 };

    CHECK(cfg_txn_commit_replace(&t, &g_lock, &scratch, sizeof(input), 0,
                                 sizeof(input), &input,
                                 persist_fail_for_test, NULL, &cap) == AICAM_ERROR_IO);
    CHECK(cap.generation == 0u);
    CHECK(canonical.a == 1u);
}

static void test_read_member_offset_and_stability(void) {
    static test_cfg_t canonical = { 1, 2, 3, 4 };
    static volatile uint32_t seq = 0;
    cfg_txn_t t;
    cfg_txn_init(&t, &canonical, &seq, sizeof(canonical));

    uint32_t b = 0;
    CHECK(cfg_txn_read_member(&t, offsetof(test_cfg_t, b), sizeof(b), &b) == AICAM_TRUE);
    CHECK(b == 2);

    uint8_t c = 0;
    CHECK(cfg_txn_read_member(&t, offsetof(test_cfg_t, c), sizeof(c), &c) == AICAM_TRUE);
    CHECK(c == 3);

    test_cfg_t out;
    CHECK(cfg_txn_read_member(&t, 0, sizeof(out), &out) == AICAM_TRUE);
    CHECK(out.a == 1 && out.b == 2 && out.c == 3 && out.d == 4);

    CHECK(cfg_txn_read_member(&t, sizeof(test_cfg_t), 1, &c) == AICAM_FALSE);
    CHECK(cfg_txn_read_member(&t, 0, sizeof(test_cfg_t) + 1, &out) == AICAM_FALSE);
    CHECK(cfg_txn_read_member(&t, 0, sizeof(out), NULL) == AICAM_FALSE);
}

static void test_read_member_never_torn_under_publish(void) {
    static test_cfg_t canonical;
    static volatile uint32_t seq = 0;
    memset(&canonical, 0, sizeof(canonical));
    cfg_txn_t t;
    cfg_txn_init(&t, &canonical, &seq, sizeof(canonical));

    test_cfg_t a = { 0xAAAAAAAAu, 0xAAAAAAAAu, 0xAA, 0xAAAAAAAAu };
    test_cfg_t b = { 0xBBBBBBBBu, 0xBBBBBBBBu, 0xBB, 0xBBBBBBBBu };
    test_cfg_t full;
    uint32_t fld;
    uint8_t small;
    for (int i = 0; i < 2000; i++) {
        cfg_txn_publish(&t, (i & 1) ? &a : &b, sizeof(a), 0);
        CHECK(cfg_txn_read_member(&t, 0, sizeof(full), &full) == AICAM_TRUE);
        CHECK(full.a == full.b && full.b == full.d);
        CHECK(full.c == (uint8_t)(full.a & 0xFFu));
        CHECK(cfg_txn_read_member(&t, offsetof(test_cfg_t, d), sizeof(fld), &fld) == AICAM_TRUE);
        CHECK(fld == full.a);
        CHECK(cfg_txn_read_member(&t, offsetof(test_cfg_t, c), sizeof(small), &small) == AICAM_TRUE);
        CHECK(small == (uint8_t)(full.a & 0xFFu));
    }
}

int main(void) {
    test_commit_replace_persist_publish_order();
    test_commit_patch_changes_only_target_member();
    test_replace_member_keeps_rest_byte_identical();
    test_persist_failure_keeps_canonical_bytes_identical();
    test_device_name_migration_candidate_semantics();
    test_second_writer_cannot_persist_inside_first_window();
    test_capture_pairs_candidate_with_generation();
    test_capture_not_filled_on_persist_failure();
    test_read_member_offset_and_stability();
    test_read_member_never_torn_under_publish();

    if (g_failures) {
        printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all cfg txn core tests passed\n");
    return 0;
}
