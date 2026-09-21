#ifndef CFG_TXN_H
#define CFG_TXN_H

#include <stddef.h>
#include <stdint.h>

#include "aicam_types.h"

typedef struct {
    void              *canonical;
    volatile uint32_t *seq;
    size_t             size;
} cfg_txn_t;

typedef aicam_result_t (*cfg_txn_persist_fn)(void *user, const void *candidate, size_t n,
                                              uint32_t *generation_out);
typedef void (*cfg_txn_patch_fn)(void *target_member, size_t member_size, void *user);
typedef void (*cfg_txn_post_commit_fn)(void *user, const void *committed, size_t size,
                                       uint32_t generation);

void cfg_txn_init(cfg_txn_t *t, void *canonical, volatile uint32_t *seq, size_t size);

aicam_bool_t cfg_txn_read(const cfg_txn_t *t, void *out, size_t size);

aicam_bool_t cfg_txn_read_member(const cfg_txn_t *t, size_t offset, size_t size, void *out);

void cfg_txn_publish(cfg_txn_t *t, const void *candidate, size_t n, size_t offset);

aicam_result_t cfg_txn_commit_replace_locked(cfg_txn_t *t, void *scratch,
                                             size_t struct_size, size_t member_offset,
                                             size_t member_size, const void *input,
                                             cfg_txn_persist_fn persist, void *persist_user,
                                             cfg_txn_post_commit_fn post_commit, void *post_user);

aicam_result_t cfg_txn_commit_patch_locked(cfg_txn_t *t, void *scratch,
                                           size_t struct_size, size_t member_offset,
                                           size_t member_size, cfg_txn_patch_fn patch, void *user,
                                           cfg_txn_persist_fn persist, void *persist_user,
                                           cfg_txn_post_commit_fn post_commit, void *post_user);

#endif
