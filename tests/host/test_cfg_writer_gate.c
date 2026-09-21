#include "cfg_txn.h"
#include "cfg_writer_gate.h"
#include <pthread.h>
#include <unistd.h>
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
    uint32_t payload;
    uint32_t tail;
} orch_cfg_t;

typedef enum {
    EV_A_PERSIST, EV_A_CACHE, EV_A_MARKER,
    EV_B_PERSIST, EV_B_CACHE, EV_B_MARKER,
    EV_DEINIT_TEARDOWN
} orch_ev_t;

typedef struct {
    orch_ev_t ev[64];
    uint32_t gen[64];
    int n;
} orch_log_t;

typedef enum { WHO_NONE = 0, WHO_A, WHO_B } who_t;

typedef struct {
    int held;
} gate_lock_t;

typedef struct {
    who_t who;
    uint32_t payload;
    int cache_fail;
    int marker_fail;
} writer_ctx_t;

typedef struct {
    cfg_writer_gate_t gate;
    gate_lock_t shared_lock;
    int writer_acquires;
    int writer_releases;
    cfg_txn_t txn;
    volatile uint32_t seq;
    orch_cfg_t canonical;
    orch_cfg_t scratch;

    uint32_t blob_gen;
    int blob_writes;
    int cache_writes;
    int marker_writes;
    uint32_t marker_gen;
    uint32_t cache_payload[8];
    uint32_t cache_bound[8];

    writer_ctx_t cur;
    orch_log_t log;
} orch_t;

static orch_t g_orch;

static void orch_log_push(orch_ev_t ev, uint32_t gen) {
    if (g_orch.log.n < 64) {
        g_orch.log.ev[g_orch.log.n] = ev;
        g_orch.log.gen[g_orch.log.n] = gen;
        g_orch.log.n++;
    }
}

static int orch_log_index(orch_ev_t ev) {
    for (int i = 0; i < g_orch.log.n; i++) {
        if (g_orch.log.ev[i] == ev) return i;
    }
    return -1;
}

static aicam_bool_t orch_shared_lock(void *ctx) {
    gate_lock_t *l = (gate_lock_t *)ctx;
    if (l->held) return AICAM_FALSE;
    l->held = 1;
    g_orch.writer_acquires++;
    return AICAM_TRUE;
}

static void orch_shared_unlock(void *ctx) {
    gate_lock_t *l = (gate_lock_t *)ctx;
    l->held = 0;
    g_orch.writer_releases++;
}

static aicam_result_t orch_persist(void *user, const void *candidate, size_t n,
                                   uint32_t *generation_out) {
    (void)user;
    (void)candidate;
    (void)n;
    if (g_orch.cur.payload == 0xDEAD0000u) return AICAM_ERROR_IO;
    g_orch.blob_gen++;
    g_orch.blob_writes++;
    if (generation_out) *generation_out = g_orch.blob_gen;
    orch_log_push((g_orch.cur.who == WHO_A) ? EV_A_PERSIST : EV_B_PERSIST, g_orch.blob_gen);
    return AICAM_OK;
}

static void orch_post_commit(void *user, const void *committed, size_t size, uint32_t generation) {
    (void)user;
    (void)size;
    orch_cfg_t c;
    memcpy(&c, committed, sizeof(c));
    who_t who = g_orch.cur.who;
    orch_ev_t ev_cache = (who == WHO_A) ? EV_A_CACHE : EV_B_CACHE;
    orch_ev_t ev_marker = (who == WHO_A) ? EV_A_MARKER : EV_B_MARKER;

    if (g_orch.cur.cache_fail) {
        orch_log_push(ev_cache, generation);
        return;
    }
    if (g_orch.cache_writes < 8) {
        g_orch.cache_payload[g_orch.cache_writes] = c.payload;
        g_orch.cache_bound[g_orch.cache_writes] = generation;
    }
    g_orch.cache_writes++;
    orch_log_push(ev_cache, generation);
    if (g_orch.cur.marker_fail) {
        orch_log_push(ev_marker, generation);
        return;
    }
    g_orch.marker_writes++;
    g_orch.marker_gen = generation;
    orch_log_push(ev_marker, generation);
}

static void orch_init(orch_t *o) {
    memset(o, 0, sizeof(*o));
    o->gate.lock_ctx = &o->shared_lock;
    o->gate.lock = orch_shared_lock;
    o->gate.unlock = orch_shared_unlock;
    o->gate.state = CFG_GATE_UNINITIALIZED;
    cfg_txn_init(&o->txn, &o->canonical, &o->seq, sizeof(o->canonical));
}

static aicam_result_t orch_writer(orch_t *o, who_t who, uint32_t payload,
                                  int cache_fail, int marker_fail,
                                  int use_patch, uint32_t patch_value,
                                  aicam_bool_t internal_path) {
    cfg_gate_state_t st = CFG_GATE_READY;
    if (!internal_path) {
        st = cfg_writer_gate_begin(&o->gate);
        if (st != CFG_GATE_READY) {
            return (st == CFG_GATE_DEINITIALIZING || st == CFG_GATE_CONTENDED)
                       ? AICAM_ERROR_BUSY : AICAM_ERROR_NOT_INITIALIZED;
        }
    } else {
        if (!orch_shared_lock(&o->shared_lock)) return AICAM_ERROR_BUSY;
    }
    g_orch.cur.who = who;
    g_orch.cur.payload = payload;
    g_orch.cur.cache_fail = cache_fail;
    g_orch.cur.marker_fail = marker_fail;
    aicam_result_t r;
    if (use_patch) {
        r = cfg_txn_commit_patch_locked(&o->txn, &o->scratch, sizeof(o->canonical),
                                        offsetof(orch_cfg_t, payload), sizeof(uint32_t),
                                        NULL, &patch_value,
                                        orch_persist, NULL, orch_post_commit, NULL);
    } else {
        orch_cfg_t input;
        memset(&input, 0, sizeof(input));
        input.payload = payload;
        r = cfg_txn_commit_replace_locked(&o->txn, &o->scratch, sizeof(o->canonical),
                                          0, sizeof(input), &input,
                                          orch_persist, NULL, orch_post_commit, NULL);
    }
    if (!internal_path) {
        cfg_writer_gate_end(&o->gate);
    } else {
        orch_shared_unlock(&o->shared_lock);
    }
    g_orch.cur.who = WHO_NONE;
    return r;
}

static void orch_deinit(orch_t *o, int expect_busy) {
    if (!orch_shared_lock(&o->shared_lock)) {
        CHECK(expect_busy == 1);
        return;
    }
    if (o->gate.state != CFG_GATE_READY) {
        orch_shared_unlock(&o->shared_lock);
        CHECK(expect_busy == 1);
        return;
    }
    CHECK(expect_busy == 0);
    cfg_writer_gate_transition(&o->gate, CFG_GATE_DEINITIALIZING);
    memset(&o->canonical, 0, sizeof(o->canonical));
    cfg_writer_gate_transition(&o->gate, CFG_GATE_UNINITIALIZED);
    orch_shared_unlock(&o->shared_lock);
    orch_log_push(EV_DEINIT_TEARDOWN, 0);
}

static void test_ordered_ab_commits(void) {
    orch_init(&g_orch);
    cfg_writer_gate_transition(&g_orch.gate, CFG_GATE_READY);

    CHECK(orch_writer(&g_orch, WHO_A, 100u, 0, 0, 0, 0, AICAM_FALSE) == AICAM_OK);
    CHECK(orch_writer(&g_orch, WHO_B, 200u, 0, 0, 0, 0, AICAM_FALSE) == AICAM_OK);

    int ia_p = orch_log_index(EV_A_PERSIST);
    int ia_c = orch_log_index(EV_A_CACHE);
    int ia_m = orch_log_index(EV_A_MARKER);
    int ib_p = orch_log_index(EV_B_PERSIST);
    int ib_c = orch_log_index(EV_B_CACHE);
    int ib_m = orch_log_index(EV_B_MARKER);
    CHECK(ia_p >= 0 && ia_c > ia_p && ia_m > ia_c);
    CHECK(ib_p > ia_m);
    CHECK(ib_c > ib_p && ib_m > ib_c);
    CHECK(g_orch.log.gen[ia_p] == 1u && g_orch.log.gen[ib_p] == 2u);
    CHECK(g_orch.log.gen[ia_m] == 1u && g_orch.log.gen[ib_m] == 2u);
    CHECK(g_orch.marker_gen == 2u);
    CHECK(g_orch.cache_payload[0] == 100u && g_orch.cache_bound[0] == 1u);
    CHECK(g_orch.cache_payload[1] == 200u && g_orch.cache_bound[1] == 2u);
    CHECK(g_orch.canonical.payload == 200u);
}

static void test_cache_failure_then_recovery(void) {
    orch_init(&g_orch);
    cfg_writer_gate_transition(&g_orch.gate, CFG_GATE_READY);

    CHECK(orch_writer(&g_orch, WHO_A, 300u, 1, 0, 0, 0, AICAM_FALSE) == AICAM_OK);
    CHECK(g_orch.blob_gen == 1u);
    CHECK(g_orch.marker_gen == 0u);
    CHECK(g_orch.canonical.payload == 300u);

    CHECK(orch_writer(&g_orch, WHO_B, 400u, 0, 0, 0, 0, AICAM_FALSE) == AICAM_OK);
    CHECK(g_orch.marker_gen == 2u);
    CHECK(g_orch.cache_payload[g_orch.cache_writes - 1] == 400u);
    CHECK(g_orch.cache_bound[g_orch.cache_writes - 1] == 2u);
}

static void test_marker_failure_then_recovery(void) {
    orch_init(&g_orch);
    cfg_writer_gate_transition(&g_orch.gate, CFG_GATE_READY);

    CHECK(orch_writer(&g_orch, WHO_A, 500u, 0, 1, 0, 0, AICAM_FALSE) == AICAM_OK);
    CHECK(g_orch.marker_gen == 0u);
    CHECK(g_orch.cache_writes == 1u);

    CHECK(orch_writer(&g_orch, WHO_B, 600u, 0, 0, 0, 0, AICAM_FALSE) == AICAM_OK);
    CHECK(g_orch.marker_gen == 2u);
    int ia_m = orch_log_index(EV_A_MARKER);
    int ib_p = orch_log_index(EV_B_PERSIST);
    CHECK(ia_m >= 0 && ib_p > ia_m);
}

static void test_lifecycle_before_init_all_wrapper_classes(void) {
    orch_init(&g_orch);
    cfg_writer_gate_transition(&g_orch.gate, CFG_GATE_UNINITIALIZED);

    aicam_result_t r1 = orch_writer(&g_orch, WHO_A, 10u, 0, 0, 0, 0, AICAM_FALSE);
    aicam_result_t r2 = orch_writer(&g_orch, WHO_A, 11u, 0, 0, 1, 77u, AICAM_FALSE);
    cfg_writer_gate_transition(&g_orch.gate, CFG_GATE_INITIALIZING);
    aicam_result_t r3 = orch_writer(&g_orch, WHO_A, 12u, 0, 0, 0, 0, AICAM_FALSE);

    CHECK(r1 == AICAM_ERROR_NOT_INITIALIZED);
    CHECK(r2 == AICAM_ERROR_NOT_INITIALIZED);
    CHECK(r3 == AICAM_ERROR_NOT_INITIALIZED);
    CHECK(g_orch.blob_writes == 0);
    CHECK(g_orch.cache_writes == 0);
    CHECK(g_orch.marker_writes == 0);
    CHECK(g_orch.canonical.payload == 0u);
    CHECK(g_orch.log.n == 0);
}

static void test_writer_vs_deinit_interleave(void) {
    orch_init(&g_orch);
    cfg_writer_gate_transition(&g_orch.gate, CFG_GATE_READY);

    cfg_gate_state_t st = cfg_writer_gate_begin(&g_orch.gate);
    CHECK(st == CFG_GATE_READY);
    g_orch.cur.who = WHO_A;
    g_orch.cur.payload = 700u;

    orch_deinit(&g_orch, 1);
    CHECK(g_orch.gate.state == CFG_GATE_READY);

    orch_cfg_t input;
    memset(&input, 0, sizeof(input));
    input.payload = 700u;
    CHECK(cfg_txn_commit_replace_locked(&g_orch.txn, &g_orch.scratch,
                                        sizeof(g_orch.canonical), 0, sizeof(input), &input,
                                        orch_persist, NULL, orch_post_commit, NULL) == AICAM_OK);
    CHECK(g_orch.writer_acquires == 1);
    CHECK(g_orch.writer_releases == 0);
    cfg_writer_gate_end(&g_orch.gate);
    CHECK(g_orch.writer_acquires == 1);
    CHECK(g_orch.writer_releases == 1);

    orch_deinit(&g_orch, 0);
    CHECK(g_orch.gate.state == CFG_GATE_UNINITIALIZED);
    CHECK(orch_log_index(EV_DEINIT_TEARDOWN) >= 0);

    CHECK(orch_writer(&g_orch, WHO_B, 800u, 0, 0, 0, 0, AICAM_FALSE) == AICAM_ERROR_NOT_INITIALIZED);
    CHECK(g_orch.blob_writes == 1u);
    CHECK(g_orch.cache_writes == 1u);
    CHECK(g_orch.marker_writes == 1u);
}

static void test_init_blocks_external_writers(void) {
    orch_init(&g_orch);
    cfg_writer_gate_transition(&g_orch.gate, CFG_GATE_INITIALIZING);

    CHECK(orch_writer(&g_orch, WHO_B, 10u, 0, 0, 0, 0, AICAM_FALSE) == AICAM_ERROR_NOT_INITIALIZED);
    CHECK(g_orch.blob_writes == 0);

    CHECK(orch_writer(&g_orch, WHO_A, 900u, 0, 0, 0, 0, AICAM_TRUE) == AICAM_OK);
    CHECK(g_orch.blob_writes == 1u);

    CHECK(orch_writer(&g_orch, WHO_B, 20u, 0, 0, 0, 0, AICAM_FALSE) == AICAM_ERROR_NOT_INITIALIZED);

    cfg_writer_gate_transition(&g_orch.gate, CFG_GATE_READY);
    CHECK(orch_writer(&g_orch, WHO_B, 30u, 0, 0, 0, 0, AICAM_FALSE) == AICAM_OK);
    CHECK(g_orch.blob_gen == 2u);
}

static cfg_once_init_t g_once;
static pthread_mutex_t g_once_mutex;
static int g_creators = 0;
static int g_critical_entered = 0;
static int g_critical_max = 0;

static void *once_thread(void *arg) {
    (void)arg;
    if (cfg_once_init_claim(&g_once)) {
        __atomic_add_fetch(&g_creators, 1, __ATOMIC_ACQ_REL);
        pthread_mutex_init(&g_once_mutex, NULL);
        cfg_once_init_publish(&g_once);
    } else {
        while (!cfg_once_init_ready(&g_once)) {
            sched_yield();
        }
    }
    pthread_mutex_lock(&g_once_mutex);
    int now = ++g_critical_entered;
    if (now > g_critical_max) g_critical_max = now;
    --g_critical_entered;
    pthread_mutex_unlock(&g_once_mutex);
    return NULL;
}

static void test_two_init_callers_serialize(void) {
    orch_init(&g_orch);

    CHECK(orch_shared_lock(&g_orch.shared_lock));
    CHECK(g_orch.gate.state == CFG_GATE_UNINITIALIZED);
    cfg_writer_gate_transition(&g_orch.gate, CFG_GATE_INITIALIZING);
    orch_shared_unlock(&g_orch.shared_lock);

    CHECK(orch_writer(&g_orch, WHO_B, 15u, 0, 0, 0, 0, AICAM_FALSE) == AICAM_ERROR_NOT_INITIALIZED);
    CHECK(g_orch.blob_writes == 0);

    CHECK(orch_writer(&g_orch, WHO_A, 910u, 0, 0, 0, 0, AICAM_TRUE) == AICAM_OK);

    CHECK(orch_shared_lock(&g_orch.shared_lock));
    cfg_writer_gate_transition(&g_orch.gate, CFG_GATE_READY);
    orch_shared_unlock(&g_orch.shared_lock);

    CHECK(orch_shared_lock(&g_orch.shared_lock));
    CHECK(g_orch.gate.state == CFG_GATE_READY);
    orch_shared_unlock(&g_orch.shared_lock);

    CHECK(orch_writer(&g_orch, WHO_B, 920u, 0, 0, 0, 0, AICAM_FALSE) == AICAM_OK);
    CHECK(g_orch.blob_gen == 2u);
}

static void test_once_failure_is_retryable(void) {
    cfg_once_init_t once;
    memset(&once, 0, sizeof(once));

    CHECK(cfg_once_init_claim(&once) == 1u);
    CHECK(cfg_once_init_claim(&once) == 0u);
    CHECK(cfg_once_init_state(&once) == CFG_ONCE_CREATING);
    cfg_once_init_fail(&once);
    CHECK(cfg_once_init_state(&once) == CFG_ONCE_IDLE);
    CHECK(cfg_once_init_ready(&once) == 0u);

    CHECK(cfg_once_init_claim(&once) == 1u);
    cfg_once_init_publish(&once);
    CHECK(cfg_once_init_ready(&once) == 1u);
    CHECK(cfg_once_init_state(&once) == CFG_ONCE_READY);
}

static cfg_once_init_t g_fail_once;
static int g_waiter_failures = 0;
static int g_waiter_ready = 0;

static void *waiter_thread(void *arg) {
    (void)arg;
    for (;;) {
        uint8_t st = cfg_once_init_state(&g_fail_once);
        if (st == CFG_ONCE_READY) {
            g_waiter_ready = 1;
            break;
        }
        if (st == CFG_ONCE_IDLE) {
            g_waiter_failures = 1;
            break;
        }
        sched_yield();
    }
    return NULL;
}

static void test_once_waiter_unblocked_by_failure(void) {
    memset(&g_fail_once, 0, sizeof(g_fail_once));
    g_waiter_failures = 0;
    g_waiter_ready = 0;

    CHECK(cfg_once_init_claim(&g_fail_once) == 1u);
    pthread_t th;
    CHECK(pthread_create(&th, NULL, waiter_thread, NULL) == 0);
    usleep(20 * 1000);
    CHECK(g_waiter_failures == 0);
    cfg_once_init_fail(&g_fail_once);
    pthread_join(th, NULL);
    CHECK(g_waiter_failures == 1);
    CHECK(g_waiter_ready == 0);

    CHECK(cfg_once_init_claim(&g_fail_once) == 1u);
    cfg_once_init_publish(&g_fail_once);
    CHECK(cfg_once_init_ready(&g_fail_once) == 1u);
}

static void test_cache_first_use_race(void) {
    memset(&g_once, 0, sizeof(g_once));
    g_creators = 0;
    g_critical_entered = 0;
    g_critical_max = 0;

    pthread_t th[4];
    for (int i = 0; i < 4; i++) {
        CHECK(pthread_create(&th[i], NULL, once_thread, NULL) == 0);
    }
    for (int i = 0; i < 4; i++) {
        pthread_join(th[i], NULL);
    }
    CHECK(g_creators == 1);
    CHECK(g_critical_max == 1);
}

int main(void) {
    test_ordered_ab_commits();
    test_cache_failure_then_recovery();
    test_marker_failure_then_recovery();
    test_lifecycle_before_init_all_wrapper_classes();
    test_writer_vs_deinit_interleave();
    test_init_blocks_external_writers();
    test_two_init_callers_serialize();
    test_once_failure_is_retryable();
    test_once_waiter_unblocked_by_failure();
    test_cache_first_use_race();

    if (g_failures) {
        printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all cfg writer gate tests passed\n");
    return 0;
}
