#include "registry.h"
#include "bitmapsz/bitmemory_backend.h"
#include "bitmapsz/ingest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define TEST(name) static int name(void)
#define RUN(name) do { printf("  %-40s ", #name); if (name()) { printf("PASS\n"); pass++; } else { printf("FAIL\n"); fail++; } } while(0)

static reg_col_t COLS[] = {
    { .name = "id",     .offset = 0,  .width = 32 },
    { .name = "status", .offset = 32, .width = 8  },
    { .name = "amount", .offset = 40, .width = 32 },
};
#define N_COLS 3

static void make_registry(registry_t *r, bmp_backend_t **b) {
    *b = bitmemory_backend_create(64);
    assert(*b);
    assert(reg_create(r, COLS, N_COLS) == REG_OK);
    assert(reg_attach(r, "main", *b) == REG_OK);
}

static void insert_test_data(registry_t *r) {
    uint64_t rows[][3] = {
        { 1, 0x01, 10000 },
        { 2, 0x02, 25000 },
        { 3, 0x01, 50000 },
        { 4, 0x03, 75000 },
        { 5, 0x01, 99000 },
    };
    for (int i = 0; i < 5; i++)
        assert(reg_append(r, rows[i], 3) == REG_OK);
}

static char *read_file_contents(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(sz + 1);
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

// ── Export Tests ─────────────────────────────────────────────────────

TEST(test_export_csv) {
    registry_t r; bmp_backend_t *b;
    make_registry(&r, &b);
    insert_test_data(&r);

    reg_view_t view = {0};
    // export all
    int ok = reg_export_all(&r, REG_FMT_CSV, "data", "_test.csv") == REG_OK;

    char *contents = read_file_contents("_test.csv");
    ok = ok && contents != NULL;
    ok = ok && strstr(contents, "id,status,amount") != NULL;
    ok = ok && strstr(contents, "10000") != NULL;
    ok = ok && strstr(contents, "99000") != NULL;

    // count lines: header + 5 data rows = 6
    int lines = 0;
    for (char *p = contents; *p; p++) if (*p == '\n') lines++;
    ok = ok && lines == 6;

    printf("[%d lines] ", lines);
    free(contents);
    remove("_test.csv");
    reg_destroy(&r);
    b->vt->destroy(b);
    return ok;
}

TEST(test_export_tsv) {
    registry_t r; bmp_backend_t *b;
    make_registry(&r, &b);
    insert_test_data(&r);

    int ok = reg_export_all(&r, REG_FMT_TSV, "data", "_test.tsv") == REG_OK;

    char *contents = read_file_contents("_test.tsv");
    ok = ok && contents != NULL;
    ok = ok && strstr(contents, "id\tstatus\tamount") != NULL;
    ok = ok && strstr(contents, "\t") != NULL;

    printf("[tab-sep] ");
    free(contents);
    remove("_test.tsv");
    reg_destroy(&r);
    b->vt->destroy(b);
    return ok;
}

TEST(test_export_sql) {
    registry_t r; bmp_backend_t *b;
    make_registry(&r, &b);
    insert_test_data(&r);

    int ok = reg_export_all(&r, REG_FMT_SQL, "orders", "_test.sql") == REG_OK;

    char *contents = read_file_contents("_test.sql");
    ok = ok && contents != NULL;
    ok = ok && strstr(contents, "INSERT INTO orders") != NULL;
    ok = ok && strstr(contents, "id, status, amount") != NULL;
    ok = ok && strstr(contents, "VALUES") != NULL;

    // should have 5 INSERT statements
    int inserts = 0;
    char *p = contents;
    while ((p = strstr(p, "INSERT")) != NULL) { inserts++; p++; }
    ok = ok && inserts == 5;

    printf("[%d INSERTs] ", inserts);
    free(contents);
    remove("_test.sql");
    reg_destroy(&r);
    b->vt->destroy(b);
    return ok;
}

TEST(test_export_mongo) {
    registry_t r; bmp_backend_t *b;
    make_registry(&r, &b);
    insert_test_data(&r);

    int ok = reg_export_all(&r, REG_FMT_MONGO, "orders", "_test.js") == REG_OK;

    char *contents = read_file_contents("_test.js");
    ok = ok && contents != NULL;
    ok = ok && strstr(contents, "db.orders.insertMany") != NULL;
    ok = ok && strstr(contents, "\"id\"") != NULL;
    ok = ok && strstr(contents, "\"amount\"") != NULL;

    printf("[insertMany] ");
    free(contents);
    remove("_test.js");
    reg_destroy(&r);
    b->vt->destroy(b);
    return ok;
}

TEST(test_export_json) {
    registry_t r; bmp_backend_t *b;
    make_registry(&r, &b);
    insert_test_data(&r);

    int ok = reg_export_all(&r, REG_FMT_JSON, "data", "_test.json") == REG_OK;

    char *contents = read_file_contents("_test.json");
    ok = ok && contents != NULL;
    ok = ok && contents[0] == '[';
    ok = ok && strstr(contents, "\"id\"") != NULL;
    ok = ok && strstr(contents, "\"status\"") != NULL;

    // 5 objects
    int braces = 0;
    for (char *p = contents; *p; p++) if (*p == '{') braces++;
    ok = ok && braces == 5;

    printf("[%d objects] ", braces);
    free(contents);
    remove("_test.json");
    reg_destroy(&r);
    b->vt->destroy(b);
    return ok;
}

TEST(test_export_mem) {
    registry_t r; bmp_backend_t *b;
    make_registry(&r, &b);
    insert_test_data(&r);

    int ok = reg_export_all(&r, REG_FMT_MEM, "data", "_test.mem") == REG_OK;

    char *contents = read_file_contents("_test.mem");
    ok = ok && contents != NULL;
    ok = ok && strstr(contents, "0x") != NULL;

    printf("[hex pairs] ");
    free(contents);
    remove("_test.mem");
    reg_destroy(&r);
    b->vt->destroy(b);
    return ok;
}

// ── Export → Import Round-trip ───────────────────────────────────────

TEST(test_roundtrip_csv) {
    // export 5 rows to CSV, then ingest back and verify
    registry_t r1; bmp_backend_t *b1;
    make_registry(&r1, &b1);
    insert_test_data(&r1);
    int ok = reg_export_all(&r1, REG_FMT_CSV, "data", "_rt.csv") == REG_OK;

    // ingest into fresh registry
    registry_t r2; bmp_backend_t *b2;
    make_registry(&r2, &b2);
    ingest_opts_t opts = INGEST_DEFAULTS;
    opts.fmt = INGEST_CSV;
    ok = ok && ingest_file(&r2, "main", "_rt.csv", opts) == REG_OK;
    ok = ok && r2.n_rows == 5;

    // verify values survived round-trip
    for (size_t i = 0; i < 5 && ok; i++) {
        uint64_t v1 = reg_read_col(&r1, i, 0); // id
        uint64_t v2 = reg_read_col(&r2, i, 0);
        ok = ok && v1 == v2;
        v1 = reg_read_col(&r1, i, 2); // amount
        v2 = reg_read_col(&r2, i, 2);
        ok = ok && v1 == v2;
    }

    printf("[%zu rows] ", r2.n_rows);
    remove("_rt.csv");
    reg_destroy(&r1); b1->vt->destroy(b1);
    reg_destroy(&r2); b2->vt->destroy(b2);
    return ok;
}

TEST(test_roundtrip_tsv) {
    registry_t r1; bmp_backend_t *b1;
    make_registry(&r1, &b1);
    insert_test_data(&r1);
    int ok = reg_export_all(&r1, REG_FMT_TSV, "data", "_rt.tsv") == REG_OK;

    registry_t r2; bmp_backend_t *b2;
    make_registry(&r2, &b2);
    ingest_opts_t opts = INGEST_DEFAULTS;
    opts.fmt = INGEST_TSV;
    ok = ok && ingest_file(&r2, "main", "_rt.tsv", opts) == REG_OK;
    ok = ok && r2.n_rows == 5;

    for (size_t i = 0; i < 5 && ok; i++) {
        ok = ok && reg_read_col(&r1, i, 2) == reg_read_col(&r2, i, 2);
    }

    printf("[%zu rows] ", r2.n_rows);
    remove("_rt.tsv");
    reg_destroy(&r1); b1->vt->destroy(b1);
    reg_destroy(&r2); b2->vt->destroy(b2);
    return ok;
}

TEST(test_roundtrip_sql) {
    registry_t r1; bmp_backend_t *b1;
    make_registry(&r1, &b1);
    insert_test_data(&r1);
    int ok = reg_export_all(&r1, REG_FMT_SQL, "orders", "_rt.sql") == REG_OK;

    registry_t r2; bmp_backend_t *b2;
    make_registry(&r2, &b2);
    ingest_opts_t opts = INGEST_DEFAULTS;
    opts.fmt = INGEST_SQL;
    ok = ok && ingest_file(&r2, "main", "_rt.sql", opts) == REG_OK;
    ok = ok && r2.n_rows == 5;

    for (size_t i = 0; i < 5 && ok; i++) {
        ok = ok && reg_read_col(&r1, i, 2) == reg_read_col(&r2, i, 2);
    }

    printf("[%zu rows] ", r2.n_rows);
    remove("_rt.sql");
    reg_destroy(&r1); b1->vt->destroy(b1);
    reg_destroy(&r2); b2->vt->destroy(b2);
    return ok;
}

TEST(test_roundtrip_json) {
    registry_t r1; bmp_backend_t *b1;
    make_registry(&r1, &b1);
    insert_test_data(&r1);
    int ok = reg_export_all(&r1, REG_FMT_JSON, "data", "_rt.json") == REG_OK;

    registry_t r2; bmp_backend_t *b2;
    make_registry(&r2, &b2);
    ingest_opts_t opts = INGEST_DEFAULTS;
    opts.fmt = INGEST_JSON;
    ok = ok && ingest_file(&r2, "main", "_rt.json", opts) == REG_OK;
    ok = ok && r2.n_rows == 5;

    for (size_t i = 0; i < 5 && ok; i++) {
        ok = ok && reg_read_col(&r1, i, 2) == reg_read_col(&r2, i, 2);
    }

    printf("[%zu rows] ", r2.n_rows);
    remove("_rt.json");
    reg_destroy(&r1); b1->vt->destroy(b1);
    reg_destroy(&r2); b2->vt->destroy(b2);
    return ok;
}

// ── Pattern Query on Imported Data ──────────────────────────────────

TEST(test_query_after_csv_import) {
    registry_t r1; bmp_backend_t *b1;
    make_registry(&r1, &b1);
    insert_test_data(&r1);
    reg_export_all(&r1, REG_FMT_CSV, "data", "_q.csv");

    registry_t r2; bmp_backend_t *b2;
    make_registry(&r2, &b2);
    ingest_opts_t opts = INGEST_DEFAULTS;
    opts.fmt = INGEST_CSV;
    ingest_file(&r2, "main", "_q.csv", opts);

    // pattern: status == 0x01 (3 rows have status=1)
    reg_define_pattern(&r2, "active", 0xFF, 0x01, 32, 8);
    reg_view_t view = {0};
    int ok = reg_query(&r2, "active", &view) == REG_OK;
    ok = ok && view.n_ids == 3;

    // count should match
    uint64_t cnt = reg_count(&r2, "active");
    ok = ok && cnt == 3;

    printf("[%zu matches, count=%llu] ", view.n_ids, (unsigned long long)cnt);
    reg_view_free(&view);
    remove("_q.csv");
    reg_destroy(&r1); b1->vt->destroy(b1);
    reg_destroy(&r2); b2->vt->destroy(b2);
    return ok;
}

// ── Export to Buffer ────────────────────────────────────────────────

TEST(test_export_buf_json) {
    registry_t r; bmp_backend_t *b;
    make_registry(&r, &b);
    insert_test_data(&r);

    // query all rows: mask=0 expect=0 matches everything
    reg_view_t all = {0};
    reg_query_inline(&r, 0, 0, 0, 1, &all);

    char *buf = NULL;
    size_t len = 0;
    int ok = reg_export_buf(&r, &all, REG_FMT_JSON, "data", &buf, &len) == REG_OK;
    ok = ok && buf != NULL;
    ok = ok && len > 0;
    ok = ok && buf[0] == '[';
    ok = ok && strstr(buf, "\"amount\"") != NULL;

    printf("[%zu bytes] ", len);
    free(buf);
    reg_view_free(&all);
    reg_destroy(&r);
    b->vt->destroy(b);
    return ok;
}

// ── Multi-backend Export ────────────────────────────────────────────

TEST(test_multi_backend_roundtrip) {
    registry_t r; bmp_backend_t *b1, *b2;
    b1 = bitmemory_backend_create(32);
    b2 = bitmemory_backend_create(32);
    assert(b1 && b2);

    reg_create(&r, COLS, N_COLS);
    reg_attach(&r, "hot", b1);
    reg_attach(&r, "cold", b2);

    // write to hot
    uint64_t row1[] = { 100, 0x01, 5000 };
    uint64_t row2[] = { 200, 0x02, 8000 };
    reg_append_to(&r, "hot", row1, 3);
    reg_append_to(&r, "hot", row2, 3);

    // write to cold
    uint64_t row3[] = { 300, 0x01, 3000 };
    reg_append_to(&r, "cold", row3, 3);

    int ok = r.n_rows == 3;

    // export all across backends to CSV
    ok = ok && reg_export_all(&r, REG_FMT_CSV, "mixed", "_multi.csv") == REG_OK;

    char *contents = read_file_contents("_multi.csv");
    ok = ok && contents != NULL;

    // all 3 rows should appear
    int lines = 0;
    for (char *p = contents; *p; p++) if (*p == '\n') lines++;
    ok = ok && lines == 4; // header + 3

    // query only hot backend
    reg_define_pattern(&r, "active", 0xFF, 0x01, 32, 8);
    reg_view_t view = {0};
    reg_query_backend(&r, "hot", "active", &view);
    ok = ok && view.n_ids == 1; // only row1 in hot has status=1

    printf("[3 rows, 2 backends, %zu hot matches] ", view.n_ids);
    reg_view_free(&view);
    free(contents);
    remove("_multi.csv");
    reg_destroy(&r);
    b1->vt->destroy(b1);
    b2->vt->destroy(b2);
    return ok;
}

// ── Main ────────────────────────────────────────────────────────────

int main(void) {
    int pass = 0, fail = 0;

    printf("\n=== Registry Export Tests ===\n");
    RUN(test_export_csv);
    RUN(test_export_tsv);
    RUN(test_export_sql);
    RUN(test_export_mongo);
    RUN(test_export_json);
    RUN(test_export_mem);

    printf("\n=== Round-trip Tests (export → import) ===\n");
    RUN(test_roundtrip_csv);
    RUN(test_roundtrip_tsv);
    RUN(test_roundtrip_sql);
    RUN(test_roundtrip_json);

    printf("\n=== Query After Import ===\n");
    RUN(test_query_after_csv_import);

    printf("\n=== Buffer & Multi-backend ===\n");
    RUN(test_export_buf_json);
    RUN(test_multi_backend_roundtrip);

    printf("\n%d passed, %d failed\n\n", pass, fail);
    return fail > 0 ? 1 : 0;
}
