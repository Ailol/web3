#ifndef REGISTRY_H
#define REGISTRY_H

#include <stdint.h>
#include <stddef.h>
#include "bitmapsz/bitmap_backend.h"

// registry = multi-bitmap accessor
// owns no data — routes through pluggable bitmap backends
// pattern keys define views, export converts to any format

#define REG_KEY_MAX       256
#define REG_MAX_BACKENDS  64
#define REG_BATCH_SIZE    4096   // rows per batch for bulk ops
#define REG_TARGET_ROWS   32000000

typedef enum {
    REG_OK = 0,
    REG_ERR_ALLOC,
    REG_ERR_NOT_FOUND,
    REG_ERR_DUPLICATE,
    REG_ERR_IO,
    REG_ERR_PATTERN,
    REG_ERR_FORMAT,
    REG_ERR_FULL,
    REG_ERR_BACKEND,
} reg_status_t;

// ── Schema ──────────────────────────────────────────────────────────

typedef struct {
    char     name[REG_KEY_MAX];
    uint32_t offset;
    uint32_t width;
} reg_col_t;

// Standard 1-bit predicates — P(property) over the address space.
// Use REG_P_COL(name, offset) to declare a bitplane column.
#define REG_P_COL(n, off)  { .name = (n), .offset = (off), .width = 1 }

#define REG_P_NEEDED    "needed"    // object is required by current world
#define REG_P_RUNNABLE  "runnable"  // object can execute
#define REG_P_KEEPALIVE "keepalive" // object must stay resident

typedef struct {
    reg_col_t *cols;
    size_t     n_cols;
    uint32_t   row_width;
} reg_schema_t;

// ── Pattern Key ─────────────────────────────────────────────────────

typedef struct {
    char     name[REG_KEY_MAX];
    uint64_t mask;
    uint64_t expect;
    uint32_t offset;
    uint32_t width;
} reg_pattern_t;

// ── Row Locator ─────────────────────────────────────────────────────
// points into a specific backend at a specific offset

typedef struct {
    uint64_t id;
    uint32_t backend_idx;
    uint64_t bit_offset;
    uint32_t bit_width;
    uint64_t timestamp;
} reg_row_t;

// ── View ────────────────────────────────────────────────────────────

typedef struct {
    uint64_t *ids;
    size_t    n_ids;
    size_t    capacity;
} reg_view_t;

// ── Export ───────────────────────────────────────────────────────────

typedef enum {
    REG_FMT_SQL,
    REG_FMT_MONGO,
    REG_FMT_CSV,
    REG_FMT_JSON,
    REG_FMT_TSV,
    REG_FMT_MEM,
} reg_format_t;

// ── Registry ────────────────────────────────────────────────────────

typedef struct {
    bmp_backend_t *backends[REG_MAX_BACKENDS];
    char           backend_names[REG_MAX_BACKENDS][REG_KEY_MAX];
    size_t         n_backends;
    uint32_t       active_backend;

    reg_row_t    *rows;
    size_t        n_rows;
    size_t        rows_cap;
    uint64_t      write_heads[REG_MAX_BACKENDS];

    reg_schema_t  schema;

    reg_pattern_t *patterns;
    size_t         n_patterns;
    size_t         patterns_cap;
} registry_t;

// ── Lifecycle ───────────────────────────────────────────────────────

reg_status_t reg_create(registry_t *r, const reg_col_t *cols, size_t n_cols);
void         reg_destroy(registry_t *r);

// ── Backend Management ──────────────────────────────────────────────

reg_status_t reg_attach(registry_t *r, const char *name, bmp_backend_t *backend);
reg_status_t reg_detach(registry_t *r, const char *name);
reg_status_t reg_set_active(registry_t *r, const char *name);
bmp_backend_t *reg_backend(const registry_t *r, const char *name);

// ── Write ───────────────────────────────────────────────────────────

reg_status_t reg_append(registry_t *r, const uint64_t *col_values, size_t n_values);
reg_status_t reg_append_to(registry_t *r, const char *backend_name,
                           const uint64_t *col_values, size_t n_values);
reg_status_t reg_append_batch(registry_t *r, const uint64_t *values,
                              size_t n_rows, size_t cols_per_row);

// pre-allocate row index for known size (avoids realloc churn on 30M+ inserts)
reg_status_t reg_reserve(registry_t *r, size_t n_rows);

// ── Pattern / View ──────────────────────────────────────────────────

reg_status_t reg_define_pattern(registry_t *r, const char *name,
                                uint64_t mask, uint64_t expect,
                                uint32_t offset, uint32_t width);

reg_status_t reg_query(const registry_t *r, const char *pattern_name,
                       reg_view_t *out);
reg_status_t reg_query_inline(const registry_t *r,
                              uint64_t mask, uint64_t expect,
                              uint32_t offset, uint32_t width,
                              reg_view_t *out);
reg_status_t reg_query_backend(const registry_t *r, const char *backend_name,
                               const char *pattern_name, reg_view_t *out);

// fast path: count matching rows without building view
uint64_t reg_count(const registry_t *r, const char *pattern_name);
uint64_t reg_count_inline(const registry_t *r,
                          uint64_t mask, uint64_t expect,
                          uint32_t offset, uint32_t width);

void reg_view_free(reg_view_t *v);

// ── Read ────────────────────────────────────────────────────────────

uint64_t     reg_read_col(const registry_t *r, uint64_t row_id, size_t col_idx);
reg_status_t reg_read_row(const registry_t *r, uint64_t row_id,
                          uint64_t *col_values, size_t n_values);

// ── Export ───────────────────────────────────────────────────────────

reg_status_t reg_export(const registry_t *r, const reg_view_t *view,
                        reg_format_t fmt, const char *table_name,
                        const char *path);
reg_status_t reg_export_buf(const registry_t *r, const reg_view_t *view,
                            reg_format_t fmt, const char *table_name,
                            char **buf, size_t *buf_len);
reg_status_t reg_export_all(const registry_t *r, reg_format_t fmt,
                            const char *table_name, const char *path);

// ── Persistence ─────────────────────────────────────────────────────

reg_status_t reg_save(const registry_t *r, const char *path);
reg_status_t reg_load(registry_t *r, const char *path);

#endif
