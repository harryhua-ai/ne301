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

typedef struct {
    void        *ctx;
    aicam_bool_t (*lock)(void *ctx);
    void         (*unlock)(void *ctx);
} cfg_txn_lock_t;

typedef aicam_result_t (*cfg_txn_persist_fn)(void *user, const void *candidate, size_t n);

void cfg_txn_init(cfg_txn_t *t, void *canonical, volatile uint32_t *seq, size_t size);

aicam_bool_t cfg_txn_read(const cfg_txn_t *t, void *out, size_t size);

aicam_bool_t cfg_txn_read_member(const cfg_txn_t *t, size_t offset, size_t size, void *out);

void cfg_txn_publish(cfg_txn_t *t, const void *candidate, size_t n, size_t offset);

aicam_result_t cfg_txn_commit(cfg_txn_t *t, const cfg_txn_lock_t *lk, const void *candidate,
                              size_t n, size_t offset,
                              cfg_txn_persist_fn persist, void *persist_user);

#endif
