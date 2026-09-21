#include "cfg_txn.h"
#include <pthread.h>
#include <sched.h>
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

static int g_ev_writer;

static volatile uint32_t *g_seq_ref;
static uint32_t g_seq_at_unlock;
static test_cfg_t *g_canon_ref;
static uint32_t g_canon_a_at_unlock;

static void ev_unlock_record(void) {
    ev_push(EV_UNLOCK, g_ev_writer);
    if (g_seq_ref) g_seq_at_unlock = *g_seq_ref;
    if (g_canon_ref) g_canon_a_at_unlock = g_canon_ref->a;
}

static int g_writer_acquires;
static int g_writer_releases;
static int g_writer_held;

static void writer_reset(void) {
    g_writer_acquires = 0;
    g_writer_releases = 0;
    g_writer_held = 0;
}

static aicam_bool_t writer_lock(void) {
    if (g_writer_held) return AICAM_FALSE;
    g_writer_held = 1;
    g_writer_acquires++;
    return AICAM_TRUE;
}

static void writer_unlock(void) {
    g_writer_held = 0;
    g_writer_releases++;
}

static aicam_result_t writer_commit_replace(cfg_txn_t *t, void *scratch, size_t struct_size,
                                            size_t offset, size_t member_size, const void *input,
                                            cfg_txn_persist_fn persist, void *persist_user,
                                            cfg_txn_post_commit_fn post_commit, void *post_user) {
    if (!writer_lock()) return AICAM_ERROR_BUSY;
    ev_push(EV_LOCK, g_ev_writer);
    aicam_result_t r = cfg_txn_commit_replace_locked(t, scratch, struct_size, offset,
                                                     member_size, input, persist, persist_user,
                                                     post_commit, post_user);
    ev_unlock_record();
    writer_unlock();
    return r;
}

static aicam_result_t writer_commit_patch(cfg_txn_t *t, void *scratch, size_t struct_size,
                                          size_t offset, size_t member_size,
                                          cfg_txn_patch_fn patch, void *user,
                                          cfg_txn_persist_fn persist, void *persist_user,
                                          cfg_txn_post_commit_fn post_commit, void *post_user) {
    if (!writer_lock()) return AICAM_ERROR_BUSY;
    ev_push(EV_LOCK, g_ev_writer);
    aicam_result_t r = cfg_txn_commit_patch_locked(t, scratch, struct_size, offset,
                                                   member_size, patch, user, persist,
                                                   persist_user, post_commit, post_user);
    ev_unlock_record();
    writer_unlock();
    return r;
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
    writer_reset();
    g_seq_ref = &seq;
    g_canon_ref = &canonical;
    g_seq_at_unlock = 0;
    g_canon_a_at_unlock = 0;
    ev_reset();
    g_persist_calls = 0;
    g_fake_blob_gen = 0;

    test_cfg_t input = { 5, 6, 7, 8 };
    test_cfg_t scratch;
    CHECK(writer_commit_replace(&t, &scratch, sizeof(scratch), 0,
                                 sizeof(input), &input,
                                 ev_persist, NULL, NULL, NULL) == AICAM_OK);

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

    CHECK(writer_commit_patch(&t, &scratch, sizeof(canonical),
                               offsetof(wide_cfg_t, mac), sizeof(canonical.mac),
                               NULL, NULL,
                               ev_persist, NULL, NULL, NULL) == AICAM_ERROR_INVALID_PARAM);

    CHECK(writer_commit_replace(&t, &scratch, sizeof(canonical),
                                 offsetof(wide_cfg_t, mac), sizeof(canonical.mac),
                                 NULL, ev_persist, NULL, NULL, NULL) == AICAM_ERROR_INVALID_PARAM);

    CHECK(writer_commit_patch(&t, &scratch, sizeof(canonical),
                               offsetof(wide_cfg_t, mac), sizeof(canonical.mac),
                               mac_rewrite_patch, (void *)"11:22:33:44:55:66",
                               ev_persist, NULL, NULL, NULL) == AICAM_OK);

    CHECK(memcmp(&canonical, &expected, sizeof(canonical)) == 0);
    CHECK(memcmp(g_persisted, &expected, sizeof(expected)) == 0);
    CHECK(seq == 2);

    CHECK(writer_commit_replace(&t, &scratch, sizeof(canonical),
                                 sizeof(canonical), 1,
                                 canonical.mac, ev_persist, NULL, NULL, NULL) == AICAM_ERROR_INVALID_PARAM);

    CHECK(writer_commit_replace(&t, &scratch, sizeof(canonical),
                                 offsetof(wide_cfg_t, mac), 0,
                                 canonical.mac, ev_persist, NULL, NULL, NULL) == AICAM_ERROR_INVALID_PARAM);
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
    CHECK(writer_commit_replace(&t, &scratch, sizeof(canonical),
                                 offsetof(wide_cfg_t, name), sizeof(canonical.name),
                                 new_name, ev_persist, NULL, NULL, NULL) == AICAM_OK);

    CHECK(memcmp(&canonical, &expected, sizeof(canonical)) == 0);
    CHECK(memcmp(g_persisted, &expected, sizeof(expected)) == 0);
    CHECK(seq == 2);
}

static void test_persist_failure_keeps_canonical_bytes_identical(void) {
    static wide_cfg_t canonical;
    static volatile uint32_t seq = 0;
    cfg_txn_t t;
    cfg_txn_init(&t, &canonical, &seq, sizeof(canonical));
    writer_reset();

    uint8_t *p = (uint8_t *)&canonical;
    for (uint32_t i = 0; i < sizeof(canonical); i++) p[i] = (uint8_t)(i * 29u + 3u);

    wide_cfg_t before = canonical;
    static wide_cfg_t scratch;

    const char *new_name = "MIGRATED-NAME-1";
    CHECK(writer_commit_replace(&t, &scratch, sizeof(canonical),
                                 offsetof(wide_cfg_t, name), sizeof(canonical.name),
                                 new_name, persist_fail_for_test, NULL, NULL, NULL) == AICAM_ERROR_IO);
    CHECK(memcmp(&canonical, &before, sizeof(canonical)) == 0);
    CHECK(seq == 0);
    CHECK(g_writer_held == 0);
    CHECK(g_writer_acquires == 1);
    CHECK(g_writer_releases == 1);

    CHECK(writer_commit_replace(&t, &scratch, sizeof(canonical),
                                 offsetof(wide_cfg_t, name), sizeof(canonical.name),
                                 new_name, ev_persist, NULL, NULL, NULL) == AICAM_OK);
    CHECK(g_writer_acquires == 2);
    CHECK(g_writer_releases == 2);
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

    CHECK(writer_commit_patch(&t, &scratch, sizeof(canonical),
                               0, sizeof(canonical),
                               name_migration_patch, NULL,
                               persist_fail_for_test, NULL, NULL, NULL) == AICAM_ERROR_IO);
    CHECK(memcmp(&canonical, &before, sizeof(canonical)) == 0);

    CHECK(writer_commit_patch(&t, &scratch, sizeof(canonical),
                               0, sizeof(canonical),
                               name_migration_patch, NULL,
                               ev_persist, NULL, NULL, NULL) == AICAM_OK);

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
    writer_reset();
    ev_reset();
    g_persist_calls = 0;

    test_cfg_t a = { 10, 10, 10, 10 };
    test_cfg_t b = { 20, 20, 20, 20 };
    static test_cfg_t scratch;

    writer_reset();
    g_writer_held = 1;
    g_ev_writer = 1;
    ev_push(EV_LOCK, 1);
    ev_push(EV_PERSIST, 1);
    memcpy(&g_persisted, &a, sizeof(a));

    g_ev_writer = 2;
    CHECK(writer_commit_replace(&t, &scratch, sizeof(b), 0,
                                 sizeof(b), &b,
                                 ev_persist, NULL, NULL, NULL) == AICAM_ERROR_BUSY);
    CHECK(g_persist_calls == 0);
    CHECK(g_writer_acquires == 0);
    CHECK(g_writer_releases == 0);

    g_ev_writer = 1;
    g_seq_ref = &seq;
    cfg_txn_publish(&t, &a, sizeof(a), 0);
    ev_push(EV_UNLOCK, 1);
    g_writer_held = 0;

    test_cfg_t pt;
    CHECK(canonical.a == 10);
    memcpy(&pt, g_persisted, sizeof(pt));
    CHECK(pt.a == 10);
    CHECK(seq == 2);

    g_ev_writer = 2;
    CHECK(writer_commit_replace(&t, &scratch, sizeof(b), 0,
                                 sizeof(b), &b,
                                 ev_persist, NULL, NULL, NULL) == AICAM_OK);
    CHECK(canonical.a == 20);
    memcpy(&pt, g_persisted, sizeof(pt));
    CHECK(pt.a == 20);
    CHECK(seq == 4);
}

typedef struct {
    uint32_t generation;
    test_cfg_t snapshot;
    uint32_t seq_at_post;
    int called;
} post_record_t;

static post_record_t g_post_record;

static void record_post_commit(void *user, const void *committed, size_t size, uint32_t generation) {
    (void)user;
    post_record_t *r = (post_record_t *)&g_post_record;
    if (size != sizeof(test_cfg_t)) return;
    r->generation = generation;
    memcpy(&r->snapshot, committed, sizeof(r->snapshot));
    r->seq_at_post = *g_seq_ref;
    r->called = 1;
}

static void test_post_commit_pairs_candidate_with_generation(void) {
    static test_cfg_t canonical = { 1, 2, 3, 4 };
    static volatile uint32_t seq = 0;
    cfg_txn_t t;
    cfg_txn_init(&t, &canonical, &seq, sizeof(canonical));
    writer_reset();
    g_seq_ref = &seq;
    g_persist_calls = 0;
    g_fake_blob_gen = 9;
    memset(&g_post_record, 0, sizeof(g_post_record));

    static test_cfg_t scratch;
    test_cfg_t a = { 10, 10, 10, 10 };
    test_cfg_t b = { 20, 20, 20, 20 };

    CHECK(writer_commit_replace(&t, &scratch, sizeof(a), 0,
                                 sizeof(a), &a, ev_persist, NULL,
                                 record_post_commit, NULL) == AICAM_OK);
    CHECK(g_post_record.called == 1);
    CHECK(g_post_record.generation == 10u);
    CHECK(memcmp(&g_post_record.snapshot, &a, sizeof(a)) == 0);
    CHECK(g_post_record.seq_at_post == 2u);
    post_record_t record_a = g_post_record;

    memset(&g_post_record, 0, sizeof(g_post_record));
    CHECK(writer_commit_replace(&t, &scratch, sizeof(b), 0,
                                 sizeof(b), &b, ev_persist, NULL,
                                 record_post_commit, NULL) == AICAM_OK);
    CHECK(g_post_record.called == 1);
    CHECK(g_post_record.generation == 11u);
    CHECK(memcmp(&g_post_record.snapshot, &b, sizeof(b)) == 0);

    CHECK(record_a.generation == 10u);
    CHECK(memcmp(&record_a.snapshot, &a, sizeof(a)) == 0);
    CHECK(canonical.a == 20u);
}

static void test_post_commit_skipped_on_persist_failure(void) {
    static test_cfg_t canonical = { 1, 2, 3, 4 };
    static volatile uint32_t seq = 0;
    cfg_txn_t t;
    cfg_txn_init(&t, &canonical, &seq, sizeof(canonical));
    writer_reset();
    g_fake_blob_gen = 100;
    memset(&g_post_record, 0, sizeof(g_post_record));

    static test_cfg_t scratch;
    test_cfg_t input = { 99, 99, 99, 99 };

    CHECK(writer_commit_replace(&t, &scratch, sizeof(input), 0,
                                 sizeof(input), &input,
                                 persist_fail_for_test, NULL, NULL, NULL) == AICAM_ERROR_IO);
    CHECK(g_post_record.called == 0);
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

static test_cfg_t blank_cfg(void) {
    test_cfg_t b;
    memset(&b, 0, sizeof(b));
    return b;
}

static void test_reader_sees_only_complete_states_during_staged_init(void) {
    static test_cfg_t canonical;
    static volatile uint32_t seq = 0;
    memset(&canonical, 0, sizeof(canonical));
    canonical.a = 0xAAAAAAAAu;
    canonical.b = 0xAAAAAAAAu;
    canonical.c = 0xAAu;
    canonical.d = 0xAAAAAAAAu;
    cfg_txn_t t;
    cfg_txn_init(&t, &canonical, &seq, sizeof(canonical));

    static test_cfg_t staged;
    memset(&staged, 0, sizeof(staged));
    staged.a = 0xBBBBBBBBu;
    staged.b = 0xBBBBBBBBu;
    staged.c = 0xBBu;
    staged.d = 0xBBBBBBBBu;

    test_cfg_t out;
    for (int i = 0; i < 1000; i++) {
        CHECK(cfg_txn_read(&t, &out, sizeof(out)) == AICAM_TRUE);
        CHECK(out.a == 0xAAAAAAAAu && out.b == out.a && out.d == out.a);
        CHECK(out.c == 0xAAu);
    }

    cfg_txn_publish(&t, &staged, sizeof(staged), 0);

    for (int i = 0; i < 1000; i++) {
        CHECK(cfg_txn_read(&t, &out, sizeof(out)) == AICAM_TRUE);
        CHECK(out.a == 0xBBBBBBBBu && out.b == out.a && out.d == out.a);
        CHECK(out.c == 0xBBu);
    }
}

static void test_reader_paused_across_deinit_publication(void) {
    static test_cfg_t canonical;
    static volatile uint32_t seq = 0;
    memset(&canonical, 0, sizeof(canonical));
    canonical.a = 0x11111111u;
    canonical.b = 0x11111111u;
    canonical.c = 0x11u;
    canonical.d = 0x11111111u;
    cfg_txn_t t = { &canonical, &seq, sizeof(canonical) };
    const cfg_txn_t binding_before = t;

    uint32_t s1 = __atomic_load_n(t.seq, __ATOMIC_ACQUIRE);
    test_cfg_t paused_snapshot;
    memcpy(&paused_snapshot, t.canonical, sizeof(paused_snapshot));

    test_cfg_t blank;
    memset(&blank, 0, sizeof(blank));
    cfg_txn_publish(&t, &blank, sizeof(blank), 0);

    CHECK(t.canonical == binding_before.canonical);
    CHECK(t.seq == binding_before.seq);
    CHECK(t.size == binding_before.size);

    uint32_t s2 = __atomic_load_n(t.seq, __ATOMIC_ACQUIRE);
    CHECK((s2 & 1u) == 0u);
    CHECK(s2 != s1);
    CHECK(paused_snapshot.a == 0x11111111u);
    CHECK(paused_snapshot.b == paused_snapshot.a);
    CHECK(canonical.a == 0u);

    test_cfg_t out;
    CHECK(cfg_txn_read(&t, &out, sizeof(out)) == AICAM_TRUE);
    CHECK(out.a == 0u && out.d == 0u);
}

static volatile int g_reader_stop;
static volatile int g_reader_iters;
static volatile int g_reader_torn;

static void *reader_loop_thread(void *arg) {
    (void)arg;
    cfg_txn_t *t = (cfg_txn_t *)arg;
    test_cfg_t out;
    while (!g_reader_stop) {
        if (cfg_txn_read(t, &out, sizeof(out)) == AICAM_TRUE) {
            g_reader_iters++;
            if (!(out.a == out.b && out.b == out.d && out.c == (uint8_t)(out.a & 0xFFu))) {
                g_reader_torn = 1;
            }
        }
        sched_yield();
    }
    return NULL;
}

static void test_reader_loop_across_publish_cycles(void) {
    static test_cfg_t canonical;
    static volatile uint32_t seq = 0;
    memset(&canonical, 0, sizeof(canonical));
    cfg_txn_t t = { &canonical, &seq, sizeof(canonical) };
    g_reader_stop = 0;
    g_reader_iters = 0;
    g_reader_torn = 0;

    pthread_t th;
    CHECK(pthread_create(&th, NULL, reader_loop_thread, &t) == 0);

    for (uint32_t cycle = 0; cycle < 200u; cycle++) {
        test_cfg_t v;
        memset(&v, 0, sizeof(v));
        uint32_t payload = (cycle & 1u) ? 0xAAAAAAAAu : 0xBBBBBBBBu;
        v.a = payload;
        v.b = payload;
        v.c = (uint8_t)(payload & 0xFFu);
        v.d = payload;
        cfg_txn_publish(&t, &v, sizeof(v), 0);
        sched_yield();
    }
    g_reader_stop = 1;
    pthread_join(th, NULL);

    CHECK(g_reader_torn == 0);
    CHECK(g_reader_iters > 0);
    CHECK(t.canonical == &canonical);
    CHECK(t.seq == &seq);
    CHECK((seq & 1u) == 0u);
}

static void test_repeated_lifecycle_keeps_seq_even_and_reader_coherent(void) {
    static test_cfg_t canonical;
    static volatile uint32_t seq = 0;
    memset(&canonical, 0, sizeof(canonical));
    cfg_txn_t t;
    cfg_txn_init(&t, &canonical, &seq, sizeof(canonical));

    test_cfg_t out;
    for (uint32_t round = 1u; round <= 50u; round++) {
        test_cfg_t v;
        memset(&v, 0, sizeof(v));
        v.a = round;
        v.b = round;
        v.c = (uint8_t)round;
        v.d = round;
        cfg_txn_publish(&t, &v, sizeof(v), 0);
        CHECK((seq & 1u) == 0u);
        CHECK(cfg_txn_read(&t, &out, sizeof(out)) == AICAM_TRUE);
        CHECK(out.a == round);

        test_cfg_t blank = blank_cfg();
        cfg_txn_publish(&t, &blank, sizeof(blank), 0);
        CHECK((seq & 1u) == 0u);
        CHECK(cfg_txn_read(&t, &out, sizeof(out)) == AICAM_TRUE);
        CHECK(out.a == 0u);
    }
}

int main(void) {
    test_commit_replace_persist_publish_order();
    test_commit_patch_changes_only_target_member();
    test_replace_member_keeps_rest_byte_identical();
    test_persist_failure_keeps_canonical_bytes_identical();
    test_device_name_migration_candidate_semantics();
    test_second_writer_cannot_persist_inside_first_window();
    test_post_commit_pairs_candidate_with_generation();
    test_post_commit_skipped_on_persist_failure();
    test_read_member_offset_and_stability();
    test_read_member_never_torn_under_publish();

    test_reader_sees_only_complete_states_during_staged_init();
    test_reader_paused_across_deinit_publication();
    test_reader_loop_across_publish_cycles();
    test_repeated_lifecycle_keeps_seq_even_and_reader_coherent();

    if (g_failures) {
        printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all cfg txn core tests passed\n");
    return 0;
}
