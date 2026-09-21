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
    int locked;
    int fail_lock;
} test_lock_t;

static aicam_bool_t tl_lock(void *user) {
    test_lock_t *l = (test_lock_t *)user;
    if (l->fail_lock > 0) {
        l->fail_lock--;
        return AICAM_FALSE;
    }
    l->lock_calls++;
    l->locked++;
    return AICAM_TRUE;
}

static void tl_unlock(void *user) {
    test_lock_t *l = (test_lock_t *)user;
    l->unlock_calls++;
    l->locked--;
}

static aicam_result_t persist_ok(void *user, const void *candidate, size_t n) {
    (void)user;
    (void)candidate;
    (void)n;
    return AICAM_OK;
}

static aicam_result_t persist_fail(void *user, const void *candidate, size_t n) {
    (void)user;
    (void)candidate;
    (void)n;
    return AICAM_ERROR_IO;
}

static void test_read_write_roundtrip(void) {
    static test_cfg_t canonical = { 1, 2, 3, 4 };
    static volatile uint32_t seq = 0;
    cfg_txn_t t;
    cfg_txn_init(&t, &canonical, &seq, sizeof(canonical));

    test_cfg_t out;
    CHECK(cfg_txn_read(&t, &out, sizeof(out)) == AICAM_TRUE);
    CHECK(out.a == 1 && out.b == 2 && out.c == 3 && out.d == 4);
    CHECK(seq == 0);

    test_cfg_t candidate = { 10, 20, 30, 40 };
    cfg_txn_publish(&t, &candidate, sizeof(candidate), 0);
    CHECK(seq == 2);
    CHECK(cfg_txn_read(&t, &out, sizeof(out)) == AICAM_TRUE);
    CHECK(out.a == 10 && out.b == 20 && out.c == 30 && out.d == 40);

    const void *p = cfg_txn_read_ptr(&t);
    CHECK(p != NULL && ((const test_cfg_t *)p)->a == 10);
}

static void test_member_publish_offsets(void) {
    static test_cfg_t canonical = { 1, 2, 3, 4 };
    static volatile uint32_t seq = 0;
    cfg_txn_t t;
    cfg_txn_init(&t, &canonical, &seq, sizeof(canonical));

    uint32_t nb = 99;
    cfg_txn_publish(&t, &nb, sizeof(nb), offsetof(test_cfg_t, b));
    CHECK(canonical.a == 1 && canonical.b == 99 && canonical.c == 3 && canonical.d == 4);
    CHECK(seq == 2);

    uint8_t nc = 7;
    cfg_txn_publish(&t, &nc, sizeof(nc), offsetof(test_cfg_t, c));
    CHECK(canonical.a == 1 && canonical.b == 99 && canonical.c == 7 && canonical.d == 4);

    test_cfg_t out;
    CHECK(cfg_txn_read(&t, &out, sizeof(out)) == AICAM_TRUE);
    CHECK(out.a == 1 && out.b == 99 && out.c == 7 && out.d == 4);
}

static void test_read_never_torn_under_alternating_publish(void) {
    static test_cfg_t canonical;
    static volatile uint32_t seq = 0;
    memset((void *)&canonical, 0, sizeof(canonical));
    cfg_txn_t t;
    cfg_txn_init(&t, &canonical, &seq, sizeof(canonical));

    test_cfg_t a = { 0xAAAAAAAAu, 0xAAAAAAAAu, 0xAA, 0xAAAAAAAAu };
    test_cfg_t b = { 0xBBBBBBBBu, 0xBBBBBBBBu, 0xBB, 0xBBBBBBBBu };
    for (int i = 0; i < 2000; i++) {
        test_cfg_t *c = (i & 1) ? &a : &b;
        cfg_txn_publish(&t, c, sizeof(*c), 0);
        test_cfg_t out;
        CHECK(cfg_txn_read(&t, &out, sizeof(out)) == AICAM_TRUE);
        CHECK(out.a == out.b && out.b == out.d);
        CHECK(out.c == (out.a & 0xFFu));
    }
}

static void test_commit_persist_failure_keeps_canonical(void) {
    static test_cfg_t canonical = { 1, 2, 3, 4 };
    static volatile uint32_t seq = 0;
    cfg_txn_t t;
    cfg_txn_init(&t, &canonical, &seq, sizeof(canonical));
    test_lock_t lk = { 0 };
    cfg_txn_lock_t lock = { &lk, tl_lock, tl_unlock };

    test_cfg_t candidate = { 9, 9, 9, 9 };
    CHECK(cfg_txn_commit(&t, &lock, &candidate, sizeof(candidate), 0,
                         persist_fail, NULL) == AICAM_ERROR_IO);
    CHECK(canonical.a == 1 && canonical.b == 2 && canonical.c == 3 && canonical.d == 4);
    CHECK(seq == 0);
    CHECK(lk.lock_calls == 0);

    CHECK(cfg_txn_commit(&t, &lock, &candidate, sizeof(candidate), 0,
                         persist_ok, NULL) == AICAM_OK);
    CHECK(canonical.a == 9 && canonical.d == 9);
    CHECK(seq == 2);
    CHECK(lk.lock_calls == 1 && lk.unlock_calls == 1 && lk.locked == 0);
}

static void test_commit_publishes_after_persist_only(void) {
    static test_cfg_t canonical = { 1, 2, 3, 4 };
    static volatile uint32_t seq = 0;
    cfg_txn_t t;
    cfg_txn_init(&t, &canonical, &seq, sizeof(canonical));
    test_lock_t lk = { 0 };
    cfg_txn_lock_t lock = { &lk, tl_lock, tl_unlock };

    uint32_t nd = 5;
    CHECK(cfg_txn_commit(&t, &lock, &nd, sizeof(nd),
                         offsetof(test_cfg_t, d), persist_ok, NULL) == AICAM_OK);
    CHECK(canonical.a == 1 && canonical.b == 2 && canonical.c == 3 && canonical.d == 5);
    CHECK(lk.lock_calls == 1);
}

static int g_added;

static void add_b(void *member, size_t n, void *user) {
    (void)user;
    test_cfg_t *m = (test_cfg_t *)member;
    (void)n;
    m->b += (uint32_t)g_added;
}

static aicam_result_t persist_capture(void *user, const void *candidate, size_t n) {
    (void)n;
    memcpy(user, candidate, sizeof(test_cfg_t));
    return AICAM_OK;
}

static void test_rmw_snapshots_inside_lock(void) {
    static test_cfg_t canonical = { 1, 2, 3, 4 };
    static volatile uint32_t seq = 0;
    cfg_txn_t t;
    cfg_txn_init(&t, &canonical, &seq, sizeof(canonical));
    test_lock_t lk = { 0 };
    cfg_txn_lock_t lock = { &lk, tl_lock, tl_unlock };

    test_cfg_t scratch;
    CHECK(cfg_txn_rmw(&t, &lock, offsetof(test_cfg_t, a), sizeof(uint32_t), &scratch,
                      NULL, NULL, persist_fail, NULL) == AICAM_ERROR_INVALID_PARAM);

    CHECK(cfg_txn_rmw(&t, &lock, 0, sizeof(scratch), &scratch,
                      add_b, NULL, persist_fail, NULL) == AICAM_ERROR_IO);
    CHECK(canonical.a == 1 && canonical.b == 2);
    CHECK(lk.lock_calls == 1 && lk.unlock_calls == 1);
}

static void test_rmw_composes_with_other_member_writer(void) {
    static test_cfg_t canonical = { 1, 2, 3, 4 };
    static volatile uint32_t seq = 0;
    cfg_txn_t t;
    cfg_txn_init(&t, &canonical, &seq, sizeof(canonical));
    test_lock_t lk = { 0 };
    cfg_txn_lock_t lock = { &lk, tl_lock, tl_unlock };

    g_added = 100;
    test_cfg_t scratch;
    test_cfg_t observed;
    CHECK(cfg_txn_rmw(&t, &lock, 0, sizeof(scratch), &scratch,
                      add_b, NULL, persist_capture, &observed) == AICAM_OK);
    CHECK(observed.a == 1 && observed.b == 102 && observed.d == 4);
    CHECK(canonical.b == 102);

    uint32_t nd = 77;
    CHECK(cfg_txn_commit(&t, &lock, &nd, sizeof(nd),
                         offsetof(test_cfg_t, d), persist_ok, NULL) == AICAM_OK);
    CHECK(canonical.b == 102 && canonical.d == 77);

    g_added = 1000;
    CHECK(cfg_txn_rmw(&t, &lock, 0, sizeof(scratch), &scratch,
                      add_b, NULL, persist_capture, &observed) == AICAM_OK);
    CHECK(observed.b == 1102 && observed.d == 77);
    CHECK(canonical.a == 1 && canonical.b == 1102 && canonical.c == 3 && canonical.d == 77);
}

static void test_rmw_persist_failure_keeps_canonical(void) {
    static test_cfg_t canonical = { 1, 2, 3, 4 };
    static volatile uint32_t seq = 0;
    cfg_txn_t t;
    cfg_txn_init(&t, &canonical, &seq, sizeof(canonical));
    test_lock_t lk = { 0 };
    cfg_txn_lock_t lock = { &lk, tl_lock, tl_unlock };

    g_added = 50;
    test_cfg_t scratch;
    CHECK(cfg_txn_rmw(&t, &lock, 0, sizeof(scratch), &scratch,
                      add_b, NULL, persist_fail, NULL) == AICAM_ERROR_IO);
    CHECK(canonical.a == 1 && canonical.b == 2 && canonical.c == 3 && canonical.d == 4);
    CHECK(seq == 0);
    CHECK(lk.lock_calls == 1 && lk.unlock_calls == 1);
}

static void test_lock_failure_paths(void) {
    static test_cfg_t canonical = { 1, 2, 3, 4 };
    static volatile uint32_t seq = 0;
    cfg_txn_t t;
    cfg_txn_init(&t, &canonical, &seq, sizeof(canonical));
    test_lock_t lk = { 0 };
    cfg_txn_lock_t lock = { &lk, tl_lock, tl_unlock };

    test_cfg_t candidate = { 8, 8, 8, 8 };
    lk.fail_lock = 1;
    CHECK(cfg_txn_commit(&t, &lock, &candidate, sizeof(candidate), 0,
                         persist_ok, NULL) == AICAM_ERROR_BUSY);
    CHECK(canonical.a == 1 && seq == 0);

    test_cfg_t scratch;
    lk.fail_lock = 1;
    CHECK(cfg_txn_rmw(&t, &lock, 0, sizeof(scratch), &scratch,
                      add_b, NULL, persist_ok, NULL) == AICAM_ERROR_BUSY);
    CHECK(canonical.b == 2 && seq == 0);
}

static void test_bounds_and_null_rejected(void) {
    static test_cfg_t canonical = { 1, 2, 3, 4 };
    static volatile uint32_t seq = 0;
    cfg_txn_t t;
    cfg_txn_init(&t, &canonical, &seq, sizeof(canonical));
    test_lock_t lk = { 0 };
    cfg_txn_lock_t lock = { &lk, tl_lock, tl_unlock };

    test_cfg_t candidate = { 0 };
    CHECK(cfg_txn_commit(&t, &lock, &candidate, sizeof(candidate) + 1, 0,
                         persist_ok, NULL) == AICAM_ERROR_INVALID_PARAM);
    CHECK(cfg_txn_commit(&t, &lock, &candidate, 4, sizeof(test_cfg_t),
                         persist_ok, NULL) == AICAM_ERROR_INVALID_PARAM);
    cfg_txn_publish(&t, NULL, 4, 0);
    CHECK(seq == 0);
    CHECK(cfg_txn_read(&t, NULL, sizeof(canonical)) == AICAM_FALSE);
    CHECK(canonical.a == 1 && seq == 0);
}

int main(void) {
    test_read_write_roundtrip();
    test_member_publish_offsets();
    test_read_never_torn_under_alternating_publish();
    test_commit_persist_failure_keeps_canonical();
    test_commit_publishes_after_persist_only();
    test_rmw_snapshots_inside_lock();
    test_rmw_composes_with_other_member_writer();
    test_rmw_persist_failure_keeps_canonical();
    test_lock_failure_paths();
    test_bounds_and_null_rejected();

    if (g_failures) {
        printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all cfg txn core tests passed\n");
    return 0;
}
