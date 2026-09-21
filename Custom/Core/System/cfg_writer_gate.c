#include "cfg_writer_gate.h"

cfg_gate_state_t cfg_writer_gate_begin(cfg_writer_gate_t *g)
{
    if (!g || !g->lock || !g->unlock) return CFG_GATE_UNINITIALIZED;
    if (g->state == CFG_GATE_UNINITIALIZED) return CFG_GATE_UNINITIALIZED;
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
    if (__atomic_test_and_set(&o->claimed, __ATOMIC_ACQ_REL)) return 0u;
    return 1u;
}

void cfg_once_init_publish(cfg_once_init_t *o)
{
    if (!o) return;
    __atomic_store_n(&o->ready, (uint8_t)1u, __ATOMIC_RELEASE);
}

uint8_t cfg_once_init_ready(const cfg_once_init_t *o)
{
    if (!o) return 0u;
    return __atomic_load_n(&o->ready, __ATOMIC_ACQUIRE);
}
