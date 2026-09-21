#include "cfg_txn.h"

#include <string.h>

void cfg_txn_init(cfg_txn_t *t, void *canonical, volatile uint32_t *seq, size_t size)
{
    if (!t) return;
    t->canonical = canonical;
    t->seq = seq;
    t->size = size;
}

aicam_bool_t cfg_txn_read(const cfg_txn_t *t, void *out, size_t size)
{
    if (!t || !t->canonical || !t->seq || !out) return AICAM_FALSE;
    if (size == 0 || size > t->size) return AICAM_FALSE;
    for (;;) {
        uint32_t s1 = __atomic_load_n(t->seq, __ATOMIC_ACQUIRE);
        if (s1 & 1u) continue;
        memcpy(out, t->canonical, size);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        uint32_t s2 = __atomic_load_n(t->seq, __ATOMIC_ACQUIRE);
        if (s1 == s2) return AICAM_TRUE;
    }
}

const void *cfg_txn_read_ptr(const cfg_txn_t *t)
{
    if (!t || !t->canonical || !t->seq) return NULL;
    for (;;) {
        uint32_t s1 = __atomic_load_n(t->seq, __ATOMIC_ACQUIRE);
        if (s1 & 1u) continue;
        const void *p = t->canonical;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        uint32_t s2 = __atomic_load_n(t->seq, __ATOMIC_ACQUIRE);
        if (s1 == s2) return p;
    }
}

void cfg_txn_publish(cfg_txn_t *t, const void *candidate, size_t n, size_t offset)
{
    if (!t || !t->canonical || !t->seq || !candidate) return;
    if (n == 0 || offset > t->size || n > t->size - offset) return;
    __atomic_fetch_add(t->seq, 1, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    memcpy((char *)t->canonical + offset, candidate, n);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_fetch_add(t->seq, 1, __ATOMIC_RELEASE);
}

aicam_result_t cfg_txn_commit(cfg_txn_t *t, const cfg_txn_lock_t *lk, const void *candidate,
                              size_t n, size_t offset,
                              cfg_txn_persist_fn persist, void *persist_user)
{
    if (!t || !lk || !lk->lock || !lk->unlock || !candidate || !persist) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (n == 0 || offset > t->size || n > t->size - offset) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    aicam_result_t r = persist(persist_user, candidate, n);
    if (r != AICAM_OK) return r;
    if (!lk->lock(lk->ctx)) return AICAM_ERROR_BUSY;
    cfg_txn_publish(t, candidate, n, offset);
    lk->unlock(lk->ctx);
    return AICAM_OK;
}

aicam_result_t cfg_txn_rmw(cfg_txn_t *t, const cfg_txn_lock_t *lk, size_t offset, size_t size,
                           void *scratch,
                           void (*mutate)(void *member, size_t n, void *user), void *user,
                           cfg_txn_persist_fn persist, void *persist_user)
{
    if (!t || !lk || !lk->lock || !lk->unlock || !scratch || !mutate || !persist) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (size == 0 || offset > t->size || size > t->size - offset) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (!lk->lock(lk->ctx)) return AICAM_ERROR_BUSY;
    memcpy(scratch, (const char *)t->canonical + offset, size);
    mutate(scratch, size, user);
    aicam_result_t r = persist(persist_user, scratch, size);
    if (r == AICAM_OK) {
        cfg_txn_publish(t, scratch, size, offset);
    }
    lk->unlock(lk->ctx);
    return r;
}
