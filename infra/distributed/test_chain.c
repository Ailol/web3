#include "chain/chain.h"
#include "compiler/compiler.h"
#include "signal/signal.h"
#include "../infra/registry/registry.h"
#include "../infra/registry/bitmapsz/bitmemory_backend.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>

#define TEST(name) static int name(void)
#define RUN(name) do { printf("  %-40s ", #name); if (name()) { printf("PASS\n"); pass++; } else { printf("FAIL\n"); fail++; } } while(0)

static reg_col_t COLS[] = {
    { .name = "id",     .offset = 0,  .width = 32 },
    { .name = "status", .offset = 32, .width = 8  },
    { .name = "amount", .offset = 40, .width = 32 },
};

// ── Basic chain lifecycle ───────────────────────────────────────────

TEST(test_chain_create_destroy) {
    chain_t c;
    int ok = chain_create(&c, "test", 256) == CHAIN_OK;
    ok = ok && chain_pending(&c) == 0;
    chain_destroy(&c);
    return ok;
}

TEST(test_chain_push_events) {
    chain_t c;
    chain_create(&c, "test", 256);
    chain_push(&c, EVT_SET, 0, 42, 32);
    chain_push(&c, EVT_SET, 32, 1, 8);
    int ok = chain_pending(&c) == 2;
    chain_destroy(&c);
    return ok;
}

// ── Compiler ────────────────────────────────────────────────────────

TEST(test_compile_set) {
    chain_t c;
    chain_create(&c, "test", 256);
    int ok = compile_line(&c, "SET 0 32 0xFF") == COMP_OK;
    ok = ok && chain_pending(&c) == 1;
    chain_destroy(&c);
    return ok;
}

TEST(test_compile_program) {
    chain_t c;
    chain_create(&c, "test", 256);
    const char *prog =
        "# write some data\n"
        "SET 0 32 100\n"
        "SET 32 8 0x01\n"
        "SET 40 32 50000\n"
        "MATCH 32 8 0xFF 0x01\n"
        "HALT\n";
    int ok = compile_program(&c, prog) == COMP_OK;
    ok = ok && chain_pending(&c) == 5;  // 3 SETs + 1 MATCH + 1 HALT
    chain_destroy(&c);
    return ok;
}

TEST(test_compile_comments_blank) {
    chain_t c;
    chain_create(&c, "test", 256);
    int ok = compile_line(&c, "# comment") == COMP_OK;
    ok = ok && compile_line(&c, "") == COMP_OK;
    ok = ok && compile_line(&c, "   ") == COMP_OK;
    ok = ok && chain_pending(&c) == 0;
    chain_destroy(&c);
    return ok;
}

TEST(test_compile_syntax_error) {
    chain_t c;
    chain_create(&c, "test", 256);
    int ok = compile_line(&c, "BOGUS 1 2 3") == COMP_ERR_SYNTAX;
    chain_destroy(&c);
    return ok;
}

// ── Full pipeline: compile → chain → registry ───────────────────────

TEST(test_full_pipeline) {
    // set up registry with bitmemory backend
    bmp_backend_t *b = bitmemory_backend_create(16);
    assert(b);
    registry_t r;
    reg_create(&r, COLS, 3);
    reg_attach(&r, "main", b);

    // insert test data directly
    uint64_t rows[][3] = {
        { 1, 0x01, 10000 },
        { 2, 0x02, 25000 },
        { 3, 0x01, 50000 },
        { 4, 0x03, 75000 },
        { 5, 0x01, 99000 },
    };
    for (int i = 0; i < 5; i++)
        reg_append(&r, rows[i], 3);

    // build chain with signal pipeline
    chain_t c;
    chain_create(&c, "query_active", 1024);

    signal_ctx_t ctx = { .reg = &r, .backend = "main" };
    signal_build_pipeline(&c, &ctx);

    // compile a MATCH program: find all rows with status == 0x01
    compile_program(&c,
        "MATCH 32 8 0xFF 0x01\n"
        "HALT\n"
    );

    // run
    size_t total = 0;
    chain_status_t s = chain_run(&c, 10, &total);
    int ok = (s == CHAIN_ERR_HALT); // should halt

    // count EMIT events with FLAG_FINAL in output
    size_t buf_sz;
    const event_t *events = chain_events(&c, &buf_sz);
    size_t matches = 0;
    for (size_t i = 0; i < buf_sz; i++) {
        if (events[i].op == EVT_EMIT && (events[i].flags & EVT_FLAG_FINAL))
            matches++;
    }
    ok = ok && matches == 3; // rows 0, 2, 4 have status=0x01

    printf("[%zu events, %zu matches] ", total, matches);
    chain_destroy(&c);
    reg_destroy(&r);
    b->vt->destroy(b);
    return ok;
}

TEST(test_pipeline_with_trace) {
    bmp_backend_t *b = bitmemory_backend_create(16);
    registry_t r;
    reg_create(&r, COLS, 3);
    reg_attach(&r, "main", b);

    uint64_t row[] = { 42, 0x05, 12345 };
    reg_append(&r, row, 3);

    chain_t c;
    chain_create(&c, "traced", 256);

    signal_ctx_t ctx = { .reg = &r, .backend = "main" };
    // add trace before the standard pipeline
    chain_add_stage(&c, "trace", stage_trace, NULL);
    signal_build_pipeline(&c, &ctx);

    compile_program(&c,
        "MATCH 32 8 0xFF 0x05\n"
        "HALT\n"
    );

    printf("\n");
    size_t total = 0;
    chain_run(&c, 5, &total);

    size_t buf_sz;
    const event_t *events = chain_events(&c, &buf_sz);
    size_t matches = 0;
    for (size_t i = 0; i < buf_sz; i++) {
        if (events[i].op == EVT_EMIT && (events[i].flags & EVT_FLAG_FINAL))
            matches++;
    }
    int ok = matches == 1;
    printf("  %-40s ", "");
    printf("[%zu match] ", matches);

    chain_destroy(&c);
    reg_destroy(&r);
    b->vt->destroy(b);
    return ok;
}

TEST(test_write_then_match) {
    bmp_backend_t *b = bitmemory_backend_create(16);
    registry_t r;
    reg_create(&r, COLS, 3);
    reg_attach(&r, "main", b);

    // pre-insert rows to query against
    uint64_t row1[] = { 10, 0x02, 500 };
    uint64_t row2[] = { 20, 0x07, 900 };
    reg_append(&r, row1, 3);
    reg_append(&r, row2, 3);

    chain_t c;
    chain_create(&c, "write_match", 512);

    signal_ctx_t ctx = { .reg = &r, .backend = "main" };
    signal_build_pipeline(&c, &ctx);

    // match status == 0x07
    compile_program(&c,
        "MATCH 32 8 0xFF 0x07\n"
        "HALT\n"
    );

    size_t total = 0;
    chain_run(&c, 10, &total);

    size_t buf_sz;
    const event_t *events = chain_events(&c, &buf_sz);
    size_t matches = 0;
    uint64_t matched_id = 0;
    for (size_t i = 0; i < buf_sz; i++) {
        if (events[i].op == EVT_EMIT && (events[i].flags & EVT_FLAG_FINAL)) {
            matched_id = events[i].arg0;
            matches++;
        }
    }
    int ok = matches == 1 && matched_id == 1; // row index 1 (id=20, status=7)

    printf("[matched row %llu] ", (unsigned long long)matched_id);
    chain_destroy(&c);
    reg_destroy(&r);
    b->vt->destroy(b);
    return ok;
}

int main(void) {
    int pass = 0, fail = 0;

    printf("\n=== Chain Lifecycle ===\n");
    RUN(test_chain_create_destroy);
    RUN(test_chain_push_events);

    printf("\n=== Compiler ===\n");
    RUN(test_compile_set);
    RUN(test_compile_program);
    RUN(test_compile_comments_blank);
    RUN(test_compile_syntax_error);

    printf("\n=== Full Pipeline (compile → chain → registry) ===\n");
    RUN(test_full_pipeline);
    RUN(test_pipeline_with_trace);
    RUN(test_write_then_match);

    printf("\n%d passed, %d failed\n\n", pass, fail);
    return fail > 0 ? 1 : 0;
}
