#include "registry.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#endif

// ── Helpers ─────────────────────────────────────────────────────────

static int find_backend_idx(const registry_t *r, const char *name) {
    for (size_t i = 0; i < r->n_backends; i++) {
        if (strcmp(r->backend_names[i], name) == 0)
            return (int)i;
    }
    return -1;
}

static bmp_backend_t *active(const registry_t *r) {
    if (r->n_backends == 0) return NULL;
    return r->backends[r->active_backend];
}

static reg_status_t view_push(reg_view_t *v, uint64_t id) {
    if (v->n_ids >= v->capacity) {
        size_t new_cap = v->capacity ? v->capacity * 2 : 256;
        uint64_t *tmp = (uint64_t *)realloc(v->ids, new_cap * sizeof(uint64_t));
        if (!tmp) return REG_ERR_ALLOC;
        v->ids = tmp;
        v->capacity = new_cap;
    }
    v->ids[v->n_ids++] = id;
    return REG_OK;
}

static reg_status_t ensure_rows(registry_t *r) {
    if (r->n_rows < r->rows_cap) return REG_OK;
    size_t new_cap = r->rows_cap * 2;
    reg_row_t *tmp = (reg_row_t *)realloc(r->rows, new_cap * sizeof(reg_row_t));
    if (!tmp) return REG_ERR_ALLOC;
    r->rows = tmp;
    r->rows_cap = new_cap;
    return REG_OK;
}

// ── Lifecycle ───────────────────────────────────────────────────────

reg_status_t reg_create(registry_t *r, const reg_col_t *cols, size_t n_cols) {
    memset(r, 0, sizeof(*r));

    r->schema.cols = (reg_col_t *)malloc(n_cols * sizeof(reg_col_t));
    if (!r->schema.cols) return REG_ERR_ALLOC;
    memcpy(r->schema.cols, cols, n_cols * sizeof(reg_col_t));
    r->schema.n_cols = n_cols;

    uint32_t total = 0;
    for (size_t i = 0; i < n_cols; i++)
        total += cols[i].width;
    r->schema.row_width = total;

    r->rows_cap = 1024;
    r->rows = (reg_row_t *)malloc(r->rows_cap * sizeof(reg_row_t));
    if (!r->rows) { free(r->schema.cols); return REG_ERR_ALLOC; }

    r->patterns_cap = 64;
    r->patterns = (reg_pattern_t *)malloc(r->patterns_cap * sizeof(reg_pattern_t));
    if (!r->patterns) { free(r->rows); free(r->schema.cols); return REG_ERR_ALLOC; }

    return REG_OK;
}

void reg_destroy(registry_t *r) {
    free(r->rows);
    free(r->schema.cols);
    free(r->patterns);
    // backends are not owned — caller destroys them
    memset(r, 0, sizeof(*r));
}

// ── Backend Management ──────────────────────────────────────────────

reg_status_t reg_attach(registry_t *r, const char *name, bmp_backend_t *backend) {
    if (r->n_backends >= REG_MAX_BACKENDS) return REG_ERR_FULL;
    if (find_backend_idx(r, name) >= 0) return REG_ERR_DUPLICATE;

    size_t idx = r->n_backends;
    r->backends[idx] = backend;
    strncpy(r->backend_names[idx], name, REG_KEY_MAX - 1);
    r->backend_names[idx][REG_KEY_MAX - 1] = '\0';
    r->write_heads[idx] = 0;
    r->n_backends++;

    if (r->n_backends == 1) r->active_backend = 0;
    return REG_OK;
}

reg_status_t reg_detach(registry_t *r, const char *name) {
    int idx = find_backend_idx(r, name);
    if (idx < 0) return REG_ERR_NOT_FOUND;

    // shift remaining backends down
    for (size_t i = (size_t)idx; i + 1 < r->n_backends; i++) {
        r->backends[i] = r->backends[i + 1];
        memcpy(r->backend_names[i], r->backend_names[i + 1], REG_KEY_MAX);
        r->write_heads[i] = r->write_heads[i + 1];
    }
    r->n_backends--;

    if (r->active_backend >= (uint32_t)r->n_backends && r->n_backends > 0)
        r->active_backend = (uint32_t)(r->n_backends - 1);

    return REG_OK;
}

reg_status_t reg_set_active(registry_t *r, const char *name) {
    int idx = find_backend_idx(r, name);
    if (idx < 0) return REG_ERR_NOT_FOUND;
    r->active_backend = (uint32_t)idx;
    return REG_OK;
}

bmp_backend_t *reg_backend(const registry_t *r, const char *name) {
    int idx = find_backend_idx(r, name);
    return idx >= 0 ? r->backends[idx] : NULL;
}

// ── Write ───────────────────────────────────────────────────────────

static reg_status_t append_to_idx(registry_t *r, uint32_t bi,
                                  const uint64_t *col_values, size_t n_values) {
    if (n_values != r->schema.n_cols) return REG_ERR_FORMAT;
    if (bi >= r->n_backends) return REG_ERR_BACKEND;

    bmp_backend_t *b = r->backends[bi];
    uint64_t head = r->write_heads[bi];

    // grow backend if needed
    while (head + r->schema.row_width > bmp_total_bits(b)) {
        bmp_status_t s = b->vt->grow(b, bmp_total_bits(b) * 2);
        if (s != BMP_OK) return REG_ERR_BACKEND;
    }

    reg_status_t s = ensure_rows(r);
    if (s != REG_OK) return s;

    // pack columns into backend
    for (size_t i = 0; i < n_values; i++) {
        bmp_pack(b, head + r->schema.cols[i].offset,
                 r->schema.cols[i].width, col_values[i]);
    }

    r->rows[r->n_rows] = (reg_row_t){
        .id = r->n_rows,
        .backend_idx = bi,
        .bit_offset = head,
        .bit_width = r->schema.row_width,
        .timestamp = 0,
    };
    r->n_rows++;
    r->write_heads[bi] = head + r->schema.row_width;
    return REG_OK;
}

reg_status_t reg_append(registry_t *r, const uint64_t *col_values, size_t n_values) {
    if (r->n_backends == 0) return REG_ERR_BACKEND;
    return append_to_idx(r, r->active_backend, col_values, n_values);
}

reg_status_t reg_append_to(registry_t *r, const char *backend_name,
                           const uint64_t *col_values, size_t n_values) {
    int idx = find_backend_idx(r, backend_name);
    if (idx < 0) return REG_ERR_NOT_FOUND;
    return append_to_idx(r, (uint32_t)idx, col_values, n_values);
}

reg_status_t reg_append_batch(registry_t *r, const uint64_t *values,
                              size_t n_rows, size_t cols_per_row) {
    if (cols_per_row != r->schema.n_cols) return REG_ERR_FORMAT;
    if (r->n_backends == 0) return REG_ERR_BACKEND;

    // pre-allocate to avoid realloc churn on large batches
    reg_status_t s = reg_reserve(r, r->n_rows + n_rows);
    if (s != REG_OK) return s;

    for (size_t i = 0; i < n_rows; i++) {
        s = reg_append(r, &values[i * cols_per_row], cols_per_row);
        if (s != REG_OK) return s;
    }
    return REG_OK;
}

reg_status_t reg_reserve(registry_t *r, size_t n_rows) {
    if (n_rows <= r->rows_cap) return REG_OK;
    // round up to next power of 2
    size_t cap = r->rows_cap;
    while (cap < n_rows) cap *= 2;
    reg_row_t *tmp = (reg_row_t *)realloc(r->rows, cap * sizeof(reg_row_t));
    if (!tmp) return REG_ERR_ALLOC;
    r->rows = tmp;
    r->rows_cap = cap;
    return REG_OK;
}

// ── Read (routes through backend) ───────────────────────────────────

uint64_t reg_read_col(const registry_t *r, uint64_t row_id, size_t col_idx) {
    if (row_id >= r->n_rows || col_idx >= r->schema.n_cols) return 0;
    const reg_row_t *row = &r->rows[row_id];
    const reg_col_t *col = &r->schema.cols[col_idx];
    bmp_backend_t *b = r->backends[row->backend_idx];
    return bmp_extract(b, row->bit_offset + col->offset, col->width);
}

reg_status_t reg_read_row(const registry_t *r, uint64_t row_id,
                          uint64_t *col_values, size_t n_values) {
    if (row_id >= r->n_rows) return REG_ERR_NOT_FOUND;
    size_t n = n_values < r->schema.n_cols ? n_values : r->schema.n_cols;
    for (size_t i = 0; i < n; i++)
        col_values[i] = reg_read_col(r, row_id, i);
    return REG_OK;
}

// ── Pattern / View ──────────────────────────────────────────────────

reg_status_t reg_define_pattern(registry_t *r, const char *name,
                                uint64_t mask, uint64_t expect,
                                uint32_t offset, uint32_t width) {
    if (r->n_patterns >= r->patterns_cap) {
        size_t new_cap = r->patterns_cap * 2;
        reg_pattern_t *tmp = (reg_pattern_t *)realloc(r->patterns, new_cap * sizeof(reg_pattern_t));
        if (!tmp) return REG_ERR_ALLOC;
        r->patterns = tmp;
        r->patterns_cap = new_cap;
    }

    reg_pattern_t *p = &r->patterns[r->n_patterns++];
    strncpy(p->name, name, REG_KEY_MAX - 1);
    p->name[REG_KEY_MAX - 1] = '\0';
    p->mask = mask;
    p->expect = expect;
    p->offset = offset;
    p->width = width;
    return REG_OK;
}

static const reg_pattern_t *find_pattern(const registry_t *r, const char *name) {
    for (size_t i = 0; i < r->n_patterns; i++) {
        if (strcmp(r->patterns[i].name, name) == 0)
            return &r->patterns[i];
    }
    return NULL;
}

reg_status_t reg_query_inline(const registry_t *r,
                              uint64_t mask, uint64_t expect,
                              uint32_t offset, uint32_t width,
                              reg_view_t *out) {
    memset(out, 0, sizeof(*out));
    for (size_t i = 0; i < r->n_rows; i++) {
        const reg_row_t *row = &r->rows[i];
        bmp_backend_t *b = r->backends[row->backend_idx];
        uint64_t val = bmp_extract(b, row->bit_offset + offset, width);
        if ((val & mask) == expect) {
            reg_status_t s = view_push(out, row->id);
            if (s != REG_OK) return s;
        }
    }
    return REG_OK;
}

reg_status_t reg_query(const registry_t *r, const char *pattern_name,
                       reg_view_t *out) {
    const reg_pattern_t *p = find_pattern(r, pattern_name);
    if (!p) return REG_ERR_NOT_FOUND;
    return reg_query_inline(r, p->mask, p->expect, p->offset, p->width, out);
}

reg_status_t reg_query_backend(const registry_t *r, const char *backend_name,
                               const char *pattern_name, reg_view_t *out) {
    int bi = find_backend_idx(r, backend_name);
    if (bi < 0) return REG_ERR_NOT_FOUND;
    const reg_pattern_t *p = find_pattern(r, pattern_name);
    if (!p) return REG_ERR_NOT_FOUND;

    memset(out, 0, sizeof(*out));
    for (size_t i = 0; i < r->n_rows; i++) {
        const reg_row_t *row = &r->rows[i];
        if (row->backend_idx != (uint32_t)bi) continue;
        bmp_backend_t *b = r->backends[row->backend_idx];
        uint64_t val = bmp_extract(b, row->bit_offset + p->offset, p->width);
        if ((val & p->mask) == p->expect) {
            reg_status_t s = view_push(out, row->id);
            if (s != REG_OK) return s;
        }
    }
    return REG_OK;
}

void reg_view_free(reg_view_t *v) {
    free(v->ids);
    memset(v, 0, sizeof(*v));
}

// no allocation, just count
uint64_t reg_count_inline(const registry_t *r,
                          uint64_t mask, uint64_t expect,
                          uint32_t offset, uint32_t width) {
    uint64_t count = 0;
    for (size_t i = 0; i < r->n_rows; i++) {
        const reg_row_t *row = &r->rows[i];
        bmp_backend_t *b = r->backends[row->backend_idx];
        uint64_t val = bmp_extract(b, row->bit_offset + offset, width);
        if ((val & mask) == expect) count++;
    }
    return count;
}

uint64_t reg_count(const registry_t *r, const char *pattern_name) {
    const reg_pattern_t *p = find_pattern(r, pattern_name);
    if (!p) return 0;
    return reg_count_inline(r, p->mask, p->expect, p->offset, p->width);
}

// ── Export ───────────────────────────────────────────────────────────

static void write_row_values(const registry_t *r, uint64_t row_id,
                             uint64_t *vals) {
    for (size_t c = 0; c < r->schema.n_cols; c++)
        vals[c] = reg_read_col(r, row_id, c);
}

static void emit_sql(FILE *f, const registry_t *r, const reg_view_t *view,
                     const char *table) {
    for (size_t i = 0; i < view->n_ids; i++) {
        uint64_t vals[64];
        write_row_values(r, view->ids[i], vals);
        fprintf(f, "INSERT INTO %s (", table);
        for (size_t c = 0; c < r->schema.n_cols; c++) {
            if (c) fprintf(f, ", ");
            fprintf(f, "%s", r->schema.cols[c].name);
        }
        fprintf(f, ") VALUES (");
        for (size_t c = 0; c < r->schema.n_cols; c++) {
            if (c) fprintf(f, ", ");
            fprintf(f, "%llu", (unsigned long long)vals[c]);
        }
        fprintf(f, ");\n");
    }
}

static void emit_mongo(FILE *f, const registry_t *r, const reg_view_t *view,
                       const char *collection) {
    fprintf(f, "db.%s.insertMany([\n", collection);
    for (size_t i = 0; i < view->n_ids; i++) {
        uint64_t vals[64];
        write_row_values(r, view->ids[i], vals);
        fprintf(f, "  {");
        for (size_t c = 0; c < r->schema.n_cols; c++) {
            if (c) fprintf(f, ", ");
            fprintf(f, "\"%s\": %llu", r->schema.cols[c].name,
                    (unsigned long long)vals[c]);
        }
        fprintf(f, "}%s\n", (i + 1 < view->n_ids) ? "," : "");
    }
    fprintf(f, "]);\n");
}

static void emit_csv(FILE *f, const registry_t *r, const reg_view_t *view,
                     char sep) {
    for (size_t c = 0; c < r->schema.n_cols; c++) {
        if (c) fputc(sep, f);
        fprintf(f, "%s", r->schema.cols[c].name);
    }
    fputc('\n', f);
    for (size_t i = 0; i < view->n_ids; i++) {
        uint64_t vals[64];
        write_row_values(r, view->ids[i], vals);
        for (size_t c = 0; c < r->schema.n_cols; c++) {
            if (c) fputc(sep, f);
            fprintf(f, "%llu", (unsigned long long)vals[c]);
        }
        fputc('\n', f);
    }
}

static void emit_json(FILE *f, const registry_t *r, const reg_view_t *view) {
    fprintf(f, "[\n");
    for (size_t i = 0; i < view->n_ids; i++) {
        uint64_t vals[64];
        write_row_values(r, view->ids[i], vals);
        fprintf(f, "  {");
        for (size_t c = 0; c < r->schema.n_cols; c++) {
            if (c) fprintf(f, ", ");
            fprintf(f, "\"%s\": %llu", r->schema.cols[c].name,
                    (unsigned long long)vals[c]);
        }
        fprintf(f, "}%s\n", (i + 1 < view->n_ids) ? "," : "");
    }
    fprintf(f, "]\n");
}

static void emit_mem(FILE *f, const registry_t *r, const reg_view_t *view) {
    for (size_t i = 0; i < view->n_ids; i++) {
        uint64_t row_id = view->ids[i];
        if (row_id >= r->n_rows) continue;
        uint64_t vals[64];
        write_row_values(r, row_id, vals);
        for (size_t c = 0; c < r->schema.n_cols; c++) {
            fprintf(f, "0x%08llx\t0x%016llx\n",
                    (unsigned long long)(r->schema.cols[c].offset),
                    (unsigned long long)vals[c]);
        }
    }
}

reg_status_t reg_export(const registry_t *r, const reg_view_t *view,
                        reg_format_t fmt, const char *table_name,
                        const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return REG_ERR_IO;

    switch (fmt) {
    case REG_FMT_SQL:   emit_sql(f, r, view, table_name);  break;
    case REG_FMT_MONGO: emit_mongo(f, r, view, table_name); break;
    case REG_FMT_CSV:   emit_csv(f, r, view, ',');          break;
    case REG_FMT_TSV:   emit_csv(f, r, view, '\t');         break;
    case REG_FMT_JSON:  emit_json(f, r, view);              break;
    case REG_FMT_MEM:   emit_mem(f, r, view);               break;
    }

    fclose(f);
    return REG_OK;
}

reg_status_t reg_export_buf(const registry_t *r, const reg_view_t *view,
                            reg_format_t fmt, const char *table_name,
                            char **buf, size_t *buf_len) {
#ifdef _WIN32
    char tmp_path[260];
    GetTempPathA(260, tmp_path);
    strncat(tmp_path, "reg_export.tmp", 260 - strlen(tmp_path) - 1);
    FILE *f = fopen(tmp_path, "w+b");
#else
    FILE *f = tmpfile();
#endif
    if (!f) return REG_ERR_IO;

    switch (fmt) {
    case REG_FMT_SQL:   emit_sql(f, r, view, table_name);  break;
    case REG_FMT_MONGO: emit_mongo(f, r, view, table_name); break;
    case REG_FMT_CSV:   emit_csv(f, r, view, ',');          break;
    case REG_FMT_TSV:   emit_csv(f, r, view, '\t');         break;
    case REG_FMT_JSON:  emit_json(f, r, view);              break;
    case REG_FMT_MEM:   emit_mem(f, r, view);               break;
    }

    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    *buf = (char *)malloc(sz + 1);
    if (!*buf) { fclose(f); return REG_ERR_ALLOC; }
    fread(*buf, 1, sz, f);
    (*buf)[sz] = '\0';
    *buf_len = (size_t)sz;
    fclose(f);
#ifdef _WIN32
    remove(tmp_path);
#endif
    return REG_OK;
}

reg_status_t reg_export_all(const registry_t *r, reg_format_t fmt,
                            const char *table_name, const char *path) {
    reg_view_t all = {0};
    for (size_t i = 0; i < r->n_rows; i++) {
        reg_status_t s = view_push(&all, r->rows[i].id);
        if (s != REG_OK) { reg_view_free(&all); return s; }
    }
    reg_status_t s = reg_export(r, &all, fmt, table_name, path);
    reg_view_free(&all);
    return s;
}

// ── Persistence (index only — backends persist themselves) ──────────

#define REG_MAGIC 0x52454742544D5000ULL

reg_status_t reg_save(const registry_t *r, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return REG_ERR_IO;

    fwrite(&(uint64_t){REG_MAGIC}, 8, 1, f);

    // schema
    fwrite(&r->schema.n_cols, sizeof(size_t), 1, f);
    fwrite(&r->schema.row_width, sizeof(uint32_t), 1, f);
    fwrite(r->schema.cols, sizeof(reg_col_t), r->schema.n_cols, f);

    // backend names (so we can re-attach on load)
    fwrite(&r->n_backends, sizeof(size_t), 1, f);
    for (size_t i = 0; i < r->n_backends; i++)
        fwrite(r->backend_names[i], REG_KEY_MAX, 1, f);
    fwrite(&r->active_backend, sizeof(uint32_t), 1, f);
    fwrite(r->write_heads, sizeof(uint64_t), r->n_backends, f);

    // row index
    fwrite(&r->n_rows, sizeof(size_t), 1, f);
    fwrite(r->rows, sizeof(reg_row_t), r->n_rows, f);

    // patterns
    fwrite(&r->n_patterns, sizeof(size_t), 1, f);
    fwrite(r->patterns, sizeof(reg_pattern_t), r->n_patterns, f);

    fclose(f);
    return REG_OK;
}

reg_status_t reg_load(registry_t *r, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return REG_ERR_IO;

    uint64_t magic;
    fread(&magic, 8, 1, f);
    if (magic != REG_MAGIC) { fclose(f); return REG_ERR_FORMAT; }

    memset(r, 0, sizeof(*r));

    fread(&r->schema.n_cols, sizeof(size_t), 1, f);
    fread(&r->schema.row_width, sizeof(uint32_t), 1, f);
    r->schema.cols = (reg_col_t *)malloc(r->schema.n_cols * sizeof(reg_col_t));
    fread(r->schema.cols, sizeof(reg_col_t), r->schema.n_cols, f);

    fread(&r->n_backends, sizeof(size_t), 1, f);
    for (size_t i = 0; i < r->n_backends; i++)
        fread(r->backend_names[i], REG_KEY_MAX, 1, f);
    fread(&r->active_backend, sizeof(uint32_t), 1, f);
    fread(r->write_heads, sizeof(uint64_t), r->n_backends, f);
    // backends must be re-attached by caller after load

    fread(&r->n_rows, sizeof(size_t), 1, f);
    r->rows_cap = r->n_rows ? r->n_rows * 2 : 1024;
    r->rows = (reg_row_t *)malloc(r->rows_cap * sizeof(reg_row_t));
    fread(r->rows, sizeof(reg_row_t), r->n_rows, f);

    fread(&r->n_patterns, sizeof(size_t), 1, f);
    r->patterns_cap = r->n_patterns ? r->n_patterns * 2 : 64;
    r->patterns = (reg_pattern_t *)malloc(r->patterns_cap * sizeof(reg_pattern_t));
    fread(r->patterns, sizeof(reg_pattern_t), r->n_patterns, f);

    fclose(f);
    return REG_OK;
}
