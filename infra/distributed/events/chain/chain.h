#ifndef CHAIN_H
#define CHAIN_H

#include <stdint.h>
#include <stddef.h>

// compiler-style event chain
// source → lex → parse → emit → execute
// each event is a typed token flowing through pipeline stages

#define CHAIN_MAX_STAGES  32
#define CHAIN_MAX_EVENTS  4096
#define CHAIN_NAME_MAX    64

// ── Event Types ─────────────────────────────────────────────────────

typedef enum {
    EVT_NOP = 0,
    EVT_SET,        // set bit/field
    EVT_CLEAR,      // clear bit/field
    EVT_MATCH,      // pattern match (mask & expect)
    EVT_EMIT,       // produce output
    EVT_BRANCH,     // conditional: jump if match
    EVT_CALL,       // invoke sub-chain
    EVT_RETURN,     // return from sub-chain
    EVT_HALT,       // stop execution
} evt_op_t;

typedef struct {
    uint64_t id;
    evt_op_t op;
    uint64_t arg0;      // operand: bit position, offset, chain_id
    uint64_t arg1;      // operand: value, mask, target
    uint64_t arg2;      // operand: width, expect
    uint64_t timestamp;
    uint32_t stage;     // which stage produced this
    uint32_t flags;
} event_t;

#define EVT_FLAG_CONSUMED  0x01
#define EVT_FLAG_ERROR     0x02
#define EVT_FLAG_FINAL     0x04

// ── Stage ───────────────────────────────────────────────────────────
// a stage processes events and emits new ones downstream

typedef struct chain chain_t;

typedef struct {
    char     name[CHAIN_NAME_MAX];
    uint32_t index;
    // returns number of events emitted into `out`
    size_t (*process)(const event_t *in, size_t n_in,
                      event_t *out, size_t out_cap,
                      void *ctx);
    void    *ctx;
} stage_t;

// ── Chain ───────────────────────────────────────────────────────────

struct chain {
    char      name[CHAIN_NAME_MAX];
    stage_t   stages[CHAIN_MAX_STAGES];
    size_t    n_stages;

    // double-buffered event queues
    event_t  *buf_a;
    event_t  *buf_b;
    size_t    buf_cap;

    uint64_t  next_evt_id;
    uint64_t  cycles;      // how many full passes through all stages
};

typedef enum {
    CHAIN_OK = 0,
    CHAIN_ERR_ALLOC,
    CHAIN_ERR_FULL,
    CHAIN_ERR_HALT,
    CHAIN_ERR_NOT_FOUND,
} chain_status_t;

// lifecycle
chain_status_t chain_create(chain_t *c, const char *name, size_t event_capacity);
void           chain_destroy(chain_t *c);

// build pipeline
chain_status_t chain_add_stage(chain_t *c, const char *name,
                               size_t (*process)(const event_t *, size_t,
                                                 event_t *, size_t, void *),
                               void *ctx);

// inject events
chain_status_t chain_push(chain_t *c, evt_op_t op,
                          uint64_t arg0, uint64_t arg1, uint64_t arg2);

// run one cycle (all stages process current buffer)
chain_status_t chain_tick(chain_t *c, size_t *events_processed);

// run until HALT or max_cycles
chain_status_t chain_run(chain_t *c, size_t max_cycles, size_t *total_events);

// inspect
size_t         chain_pending(const chain_t *c);
const event_t *chain_events(const chain_t *c, size_t *count);

#endif
