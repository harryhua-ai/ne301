#ifndef CFG_WRITER_GATE_H
#define CFG_WRITER_GATE_H

#include <stdint.h>

#include "aicam_types.h"

typedef enum {
    CFG_GATE_UNINITIALIZED = 0,
    CFG_GATE_INITIALIZING,
    CFG_GATE_READY,
    CFG_GATE_DEINITIALIZING,
    CFG_GATE_CONTENDED
} cfg_gate_state_t;

typedef struct {
    void *lock_ctx;
    aicam_bool_t (*lock)(void *ctx);
    void (*unlock)(void *ctx);
    cfg_gate_state_t state;
} cfg_writer_gate_t;

cfg_gate_state_t cfg_writer_gate_begin(cfg_writer_gate_t *g);

void cfg_writer_gate_end(cfg_writer_gate_t *g);

void cfg_writer_gate_transition(cfg_writer_gate_t *g, cfg_gate_state_t state);

#define CFG_ONCE_IDLE     0u
#define CFG_ONCE_CREATING 1u
#define CFG_ONCE_READY    2u

typedef struct {
    volatile uint8_t state;
} cfg_once_init_t;

uint8_t cfg_once_init_claim(cfg_once_init_t *o);

void cfg_once_init_publish(cfg_once_init_t *o);

void cfg_once_init_fail(cfg_once_init_t *o);

uint8_t cfg_once_init_ready(const cfg_once_init_t *o);

uint8_t cfg_once_init_state(const cfg_once_init_t *o);

#endif
