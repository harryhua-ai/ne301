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

static aicam_result_t cfg_txn_commit_write(cfg_txn_t *t, const cfg_txn_lock_t *lk, void *scratch,
                                           size_t struct_size, size_t member_offset,
                                           size_t member_size, const void *input,
                                           cfg_txn_patch_fn patch, void *user,
                                           cfg_txn_persist_fn persist, void *persist_user,
                                           cfg_txn_commit_capture_t *capture)
{
    if (!t || !t->canonical || !t->seq || !lk || !lk->lock || !lk->unlock || !scratch ||
        !persist) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (struct_size == 0 || struct_size > t->size ||
        member_offset > struct_size || member_size > struct_size - member_offset) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if ((input == NULL) == (patch == NULL)) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (member_size == 0) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (!lk->lock(lk->ctx)) return AICAM_ERROR_BUSY;

    memcpy(scratch, t->canonical, struct_size);
    void *target = (char *)scratch + member_offset;
    if (input) {
        memcpy(target, input, member_size);
    } else {
        patch(target, member_size, user);
    }

    uint32_t committed_generation = 0;
    aicam_result_t r = persist(persist_user, scratch, struct_size, &committed_generation);
    if (r == AICAM_OK) {
        cfg_txn_publish(t, scratch, struct_size, 0);
        if (capture && capture->buffer && capture->size >= struct_size) {
            memcpy(capture->buffer, scratch, struct_size);
            capture->size = struct_size;
            capture->generation = committed_generation;
        }
    }
    lk->unlock(lk->ctx);
    return r;
}

aicam_result_t cfg_txn_commit_replace(cfg_txn_t *t, const cfg_txn_lock_t *lk, void *scratch,
                                      size_t struct_size, size_t member_offset, size_t member_size,
                                      const void *input,
                                      cfg_txn_persist_fn persist, void *persist_user,
                                      cfg_txn_commit_capture_t *capture)
{
    return cfg_txn_commit_write(t, lk, scratch, struct_size, member_offset, member_size,
                                input, NULL, NULL, persist, persist_user, capture);
}

aicam_result_t cfg_txn_commit_patch(cfg_txn_t *t, const cfg_txn_lock_t *lk, void *scratch,
                                    size_t struct_size, size_t member_offset, size_t member_size,
                                    cfg_txn_patch_fn patch, void *user,
                                    cfg_txn_persist_fn persist, void *persist_user,
                                    cfg_txn_commit_capture_t *capture)
{
    return cfg_txn_commit_write(t, lk, scratch, struct_size, member_offset, member_size,
                                NULL, patch, user, persist, persist_user, capture);
}
