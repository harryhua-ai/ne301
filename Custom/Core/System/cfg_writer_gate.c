#include "cfg_writer_gate.h"

cfg_gate_state_t cfg_writer_gate_begin(cfg_writer_gate_t *g)
{
    if (!g || !g->lock || !g->unlock) return CFG_GATE_UNINITIALIZED;
    if (!g->lock(g->lock_ctx)) return CFG_GATE_CONTENDED;
    if (g->state != CFG_GATE_READY) {
        cfg_gate_state_t observed = g->state;
        g->unlock(g->lock_ctx);
        return observed;
    }
    return CFG_GATE_READY;
}

void cfg_writer_gate_end(cfg_writer_gate_t *g)
{
    if (!g || !g->unlock) return;
    g->unlock(g->lock_ctx);
}

void cfg_writer_gate_transition(cfg_writer_gate_t *g, cfg_gate_state_t state)
{
    if (!g) return;
    g->state = state;
}

uint8_t cfg_once_init_claim(cfg_once_init_t *o)
{
    if (!o) return 0u;
    uint8_t expected = CFG_ONCE_IDLE;
    return __atomic_compare_exchange_n(&o->state, &expected, CFG_ONCE_CREATING,
                                       0u, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ? 1u : 0u;
}

void cfg_once_init_publish(cfg_once_init_t *o)
{
    if (!o) return;
    __atomic_store_n(&o->state, (uint8_t)CFG_ONCE_READY, __ATOMIC_RELEASE);
}

void cfg_once_init_fail(cfg_once_init_t *o)
{
    if (!o) return;
    __atomic_store_n(&o->state, (uint8_t)CFG_ONCE_IDLE, __ATOMIC_RELEASE);
}

uint8_t cfg_once_init_ready(const cfg_once_init_t *o)
{
    if (!o) return 0u;
    return (__atomic_load_n(&o->state, __ATOMIC_ACQUIRE) == CFG_ONCE_READY) ? 1u : 0u;
}

uint8_t cfg_once_init_state(const cfg_once_init_t *o)
{
    if (!o) return CFG_ONCE_IDLE;
    return __atomic_load_n(&o->state, __ATOMIC_ACQUIRE);
}
