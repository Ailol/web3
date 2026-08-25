#include "chain.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

chain_status_t chain_create(chain_t *c, const char *name, size_t event_capacity) {
    memset(c, 0, sizeof(*c));
    strncpy(c->name, name, CHAIN_NAME_MAX - 1);

    if (event_capacity == 0) event_capacity = CHAIN_MAX_EVENTS;
    c->buf_a = (event_t *)calloc(event_capacity, sizeof(event_t));
    c->buf_b = (event_t *)calloc(event_capacity, sizeof(event_t));
    if (!c->buf_a || !c->buf_b) {
        free(c->buf_a); free(c->buf_b);
        return CHAIN_ERR_ALLOC;
    }
    c->buf_cap = event_capacity;
    return CHAIN_OK;
}

void chain_destroy(chain_t *c) {
    free(c->buf_a);
    free(c->buf_b);
    memset(c, 0, sizeof(*c));
}

chain_status_t chain_add_stage(chain_t *c, const char *name,
                               size_t (*process)(const event_t *, size_t,
                                                 event_t *, size_t, void *),
                               void *ctx) {
    if (c->n_stages >= CHAIN_MAX_STAGES) return CHAIN_ERR_FULL;
    stage_t *s = &c->stages[c->n_stages];
    strncpy(s->name, name, CHAIN_NAME_MAX - 1);
    s->index = (uint32_t)c->n_stages;
    s->process = process;
    s->ctx = ctx;
    c->n_stages++;
    return CHAIN_OK;
}

chain_status_t chain_push(chain_t *c, evt_op_t op,
                          uint64_t arg0, uint64_t arg1, uint64_t arg2) {
    // find first empty slot in buf_a
    for (size_t i = 0; i < c->buf_cap; i++) {
        if (c->buf_a[i].op == EVT_NOP && !(c->buf_a[i].flags & EVT_FLAG_CONSUMED)) {
            c->buf_a[i] = (event_t){
                .id = c->next_evt_id++,
                .op = op,
                .arg0 = arg0,
                .arg1 = arg1,
                .arg2 = arg2,
                .stage = 0,
            };
            return CHAIN_OK;
        }
    }
    return CHAIN_ERR_FULL;
}

static size_t count_live(const event_t *buf, size_t cap) {
    size_t n = 0;
    for (size_t i = 0; i < cap; i++) {
        if (buf[i].op != EVT_NOP || (buf[i].flags & EVT_FLAG_FINAL))
            n++;
    }
    return n;
}

static size_t compact(const event_t *src, size_t src_cap,
                      event_t *dst, size_t dst_cap) {
    size_t n = 0;
    for (size_t i = 0; i < src_cap && n < dst_cap; i++) {
        if (src[i].op != EVT_NOP && !(src[i].flags & EVT_FLAG_CONSUMED)) {
            dst[n++] = src[i];
        }
    }
    return n;
}

chain_status_t chain_tick(chain_t *c, size_t *events_processed) {
    size_t total = 0;

    for (size_t si = 0; si < c->n_stages; si++) {
        stage_t *stage = &c->stages[si];

        // compact live events into buf_b as input
        size_t n_in = compact(c->buf_a, c->buf_cap, c->buf_b, c->buf_cap);
        if (n_in == 0) break;

        // clear buf_a for output
        memset(c->buf_a, 0, c->buf_cap * sizeof(event_t));

        // process: stage reads buf_b, writes buf_a
        size_t n_out = stage->process(c->buf_b, n_in,
                                       c->buf_a, c->buf_cap,
                                       stage->ctx);
        total += n_out;

        // tag emitted events with stage index
        for (size_t i = 0; i < n_out && i < c->buf_cap; i++) {
            if (c->buf_a[i].op != EVT_NOP)
                c->buf_a[i].stage = (uint32_t)si;
        }

        // check for HALT
        for (size_t i = 0; i < c->buf_cap; i++) {
            if (c->buf_a[i].op == EVT_HALT) {
                if (events_processed) *events_processed = total;
                c->cycles++;
                return CHAIN_ERR_HALT;
            }
        }
    }

    c->cycles++;
    if (events_processed) *events_processed = total;
    return CHAIN_OK;
}

chain_status_t chain_run(chain_t *c, size_t max_cycles, size_t *total_events) {
    size_t total = 0;
    for (size_t i = 0; i < max_cycles; i++) {
        size_t processed = 0;
        chain_status_t s = chain_tick(c, &processed);
        total += processed;

        if (s == CHAIN_ERR_HALT) {
            if (total_events) *total_events = total;
            return CHAIN_ERR_HALT;
        }

        if (processed == 0) break; // nothing left to process
    }
    if (total_events) *total_events = total;
    return CHAIN_OK;
}

size_t chain_pending(const chain_t *c) {
    return count_live(c->buf_a, c->buf_cap);
}

const event_t *chain_events(const chain_t *c, size_t *count) {
    *count = c->buf_cap;
    return c->buf_a;
}
