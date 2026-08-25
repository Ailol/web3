#ifndef SIGNAL_H
#define SIGNAL_H

#include "../chain/chain.h"
#include "../../infra/registry/registry.h"

// built-in stage processors that wire chain events to registry operations
// each is a stage process function compatible with chain_add_stage

typedef struct {
    registry_t *reg;
    const char *backend;
} signal_ctx_t;

// stage: executes SET/CLEAR events against a registry backend
size_t stage_write(const event_t *in, size_t n_in,
                   event_t *out, size_t out_cap, void *ctx);

// stage: executes MATCH events, emits matched row IDs as EMIT events
size_t stage_match(const event_t *in, size_t n_in,
                   event_t *out, size_t out_cap, void *ctx);

// stage: collects EMIT events into a view (arg0 = row_id)
size_t stage_collect(const event_t *in, size_t n_in,
                     event_t *out, size_t out_cap, void *ctx);

// stage: pass-through, logs events (debug)
size_t stage_trace(const event_t *in, size_t n_in,
                   event_t *out, size_t out_cap, void *ctx);

// build a standard pipeline: write → match → collect
chain_status_t signal_build_pipeline(chain_t *c, signal_ctx_t *ctx);

#endif
