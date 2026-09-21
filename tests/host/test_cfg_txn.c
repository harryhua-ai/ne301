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
static uint32_t g_canon_a_at_unlock;
static test_cfg_t *g_canon_ref;

static void ev_unlock(void *user) {
    ev_push(EV_UNLOCK, g_ev_writer);
    if (g_seq_ref) g_seq_at_unlock = *g_seq_ref;
    if (g_canon_ref) g_canon_a_at_unlock = g_canon_ref->a;
    tl_unlock(user);
}

static test_cfg_t g_persisted;
static int g_persist_calls;

static aicam_result_t ev_persist(void *user, const void *candidate, size_t n) {
    (void)user;
    (void)n;
    ev_push(EV_PERSIST, g_ev_writer);
    memcpy(&g_persisted, candidate, sizeof(g_persisted));
    g_persist_calls++;
    return AICAM_OK;
}

static aicam_result_t persist_fail_for_test(void *user, const void *candidate, size_t n) {
    (void)user;
    (void)candidate;
    (void)n;
    return AICAM_ERROR_IO;
}

static void test_commit_persist_inside_lock_segment(void) {
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

    test_cfg_t candidate = { 5, 6, 7, 8 };
    CHECK(cfg_txn_commit(&t, &g_lock, &candidate, sizeof(candidate), 0,
                         ev_persist, NULL) == AICAM_OK);

    CHECK(g_ev.n == 3);
    CHECK(g_ev.kind[0] == EV_LOCK);
    CHECK(g_ev.kind[1] == EV_PERSIST);
    CHECK(g_ev.kind[2] == EV_UNLOCK);
    CHECK(g_seq_at_unlock == 2);
    CHECK(g_canon_a_at_unlock == 5);
    CHECK(seq == 2);
    CHECK(canonical.a == 5 && canonical.d == 8);
    CHECK(g_persisted.a == 5 && g_persisted.d == 8);
}

static void test_persist_failure_within_lock_no_publish(void) {
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

    test_cfg_t candidate = { 9, 9, 9, 9 };
    CHECK(cfg_txn_commit(&t, &g_lock, &candidate, sizeof(candidate), 0,
                         persist_fail_for_test, NULL) == AICAM_ERROR_IO);

    CHECK(seq == 0);
    CHECK(canonical.a == 1 && canonical.d == 4);
    CHECK(g_lk.held == 0);
    CHECK(g_persist_calls == 0);
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

    test_cfg_t a = { 10, 10, 10, 10 };
    test_cfg_t b = { 20, 20, 20, 20 };

    g_lk.held = 1;
    g_ev_writer = 1;
    ev_push(EV_LOCK, 1);
    ev_push(EV_PERSIST, 1);
    memcpy(&g_persisted, &a, sizeof(a));

    g_ev_writer = 2;
    CHECK(cfg_txn_commit(&t, &g_lock, &b, sizeof(b), 0,
                         ev_persist, NULL) == AICAM_ERROR_BUSY);
    CHECK(g_persist_calls == 0);

    g_ev_writer = 1;
    g_seq_ref = &seq;
    cfg_txn_publish(&t, &a, sizeof(a), 0);
    ev_push(EV_UNLOCK, 1);
    g_lk.held = 0;

    CHECK(canonical.a == 10);
    CHECK(g_persisted.a == 10);
    CHECK(seq == 2);

    g_ev_writer = 2;
    CHECK(cfg_txn_commit(&t, &g_lock, &b, sizeof(b), 0,
                         ev_persist, NULL) == AICAM_OK);
    CHECK(canonical.a == 20);
    CHECK(g_persisted.a == 20);
    CHECK(seq == 4);
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
    test_commit_persist_inside_lock_segment();
    test_persist_failure_within_lock_no_publish();
    test_second_writer_cannot_persist_inside_first_window();
    test_read_member_offset_and_stability();
    test_read_member_never_torn_under_publish();

    if (g_failures) {
        printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all cfg txn core tests passed\n");
    return 0;
}
