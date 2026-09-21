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

aicam_bool_t cfg_txn_read_member(const cfg_txn_t *t, size_t offset, size_t size, void *out)
{
    if (!t || !t->canonical || !t->seq || !out) return AICAM_FALSE;
    if (size == 0 || offset > t->size || size > t->size - offset) return AICAM_FALSE;
    for (;;) {
        uint32_t s1 = __atomic_load_n(t->seq, __ATOMIC_ACQUIRE);
        if (s1 & 1u) continue;
        memcpy(out, (const char *)t->canonical + offset, size);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        uint32_t s2 = __atomic_load_n(t->seq, __ATOMIC_ACQUIRE);
        if (s1 == s2) return AICAM_TRUE;
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
    if (!lk->lock(lk->ctx)) return AICAM_ERROR_BUSY;
    aicam_result_t r = persist(persist_user, candidate, n);
    if (r == AICAM_OK) {
        cfg_txn_publish(t, candidate, n, offset);
    }
    lk->unlock(lk->ctx);
    return r;
}
