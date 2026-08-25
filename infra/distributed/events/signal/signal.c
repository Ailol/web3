#include "signal.h"

#include <string.h>
#include <stdio.h>

// ── Write Stage ─────────────────────────────────────────────────────
// SET/CLEAR → bitmap operations via registry backend

size_t stage_write(const event_t *in, size_t n_in,
                   event_t *out, size_t out_cap, void *ctx) {
    signal_ctx_t *sc = (signal_ctx_t *)ctx;
    bmp_backend_t *b = reg_backend(sc->reg, sc->backend);
    if (!b) return 0;

    size_t n_out = 0;
    for (size_t i = 0; i < n_in && n_out < out_cap; i++) {
        switch (in[i].op) {
        case EVT_SET:
            bmp_pack(b, in[i].arg0, (uint32_t)in[i].arg2, in[i].arg1);
            out[n_out] = in[i];
            out[n_out].flags |= EVT_FLAG_CONSUMED;
            n_out++;
            break;
        case EVT_CLEAR:
            bmp_pack(b, in[i].arg0, (uint32_t)in[i].arg2, 0);
            out[n_out] = in[i];
            out[n_out].flags |= EVT_FLAG_CONSUMED;
            n_out++;
            break;
        default:
            // pass through non-write events
            out[n_out++] = in[i];
            break;
        }
    }
    return n_out;
}

// ── Match Stage ─────────────────────────────────────────────────────
// MATCH → scan registry rows, emit EMIT events for hits

size_t stage_match(const event_t *in, size_t n_in,
                   event_t *out, size_t out_cap, void *ctx) {
    signal_ctx_t *sc = (signal_ctx_t *)ctx;
    size_t n_out = 0;

    for (size_t i = 0; i < n_in && n_out < out_cap; i++) {
        if (in[i].op == EVT_MATCH) {
            uint64_t offset = in[i].arg0;
            uint64_t mask   = in[i].arg1;
            uint32_t width  = (uint32_t)(in[i].arg2 >> 32);
            uint64_t expect = in[i].arg2 & 0xFFFFFFFF;

            reg_view_t view = {0};
            reg_query_inline(sc->reg, mask, expect, (uint32_t)offset, width, &view);

            for (size_t j = 0; j < view.n_ids && n_out < out_cap; j++) {
                out[n_out] = (event_t){
                    .id = in[i].id,
                    .op = EVT_EMIT,
                    .arg0 = view.ids[j],
                    .arg1 = in[i].arg1,
                    .arg2 = in[i].arg2,
                    .stage = in[i].stage,
                };
                n_out++;
            }
            reg_view_free(&view);
        } else if (in[i].op == EVT_HALT) {
            out[n_out++] = in[i];
        } else if (!(in[i].flags & EVT_FLAG_CONSUMED)) {
            out[n_out++] = in[i];
        }
    }
    return n_out;
}

// ── Collect Stage ───────────────────────────────────────────────────
// gathers EMIT events, marks as FINAL

size_t stage_collect(const event_t *in, size_t n_in,
                     event_t *out, size_t out_cap, void *ctx) {
    (void)ctx;
    size_t n_out = 0;
    for (size_t i = 0; i < n_in && n_out < out_cap; i++) {
        out[n_out] = in[i];
        if (in[i].op == EVT_EMIT) {
            out[n_out].flags |= EVT_FLAG_FINAL;
        }
        n_out++;
    }
    return n_out;
}

// ── Trace Stage ─────────────────────────────────────────────────────

size_t stage_trace(const event_t *in, size_t n_in,
                   event_t *out, size_t out_cap, void *ctx) {
    (void)ctx;
    static const char *OP_NAMES[] = {
        "NOP", "SET", "CLEAR", "MATCH", "EMIT",
        "BRANCH", "CALL", "RETURN", "HALT"
    };
    size_t n_out = 0;
    for (size_t i = 0; i < n_in && n_out < out_cap; i++) {
        const char *name = (in[i].op <= EVT_HALT) ? OP_NAMES[in[i].op] : "???";
        printf("  [trace] evt=%llu op=%s arg0=0x%llx arg1=0x%llx flags=0x%x\n",
               (unsigned long long)in[i].id, name,
               (unsigned long long)in[i].arg0,
               (unsigned long long)in[i].arg1,
               in[i].flags);
        out[n_out++] = in[i];
    }
    return n_out;
}

// ── Pipeline Builder ────────────────────────────────────────────────

chain_status_t signal_build_pipeline(chain_t *c, signal_ctx_t *ctx) {
    chain_status_t s;
    s = chain_add_stage(c, "write", stage_write, ctx);
    if (s != CHAIN_OK) return s;
    s = chain_add_stage(c, "match", stage_match, ctx);
    if (s != CHAIN_OK) return s;
    s = chain_add_stage(c, "collect", stage_collect, ctx);
    return s;
}
