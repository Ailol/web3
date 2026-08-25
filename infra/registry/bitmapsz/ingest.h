#ifndef INGEST_H
#define INGEST_H

#include "bitmap_backend.h"
#include "../registry.h"

// any data → bitmap
// every format is just addr+value pairs at the end of the day

typedef enum
{
    INGEST_MEM,  // 0xADDR\t0xVALUE (MIPS .mem format)
    INGEST_CSV,  // header row + comma-separated values
    INGEST_TSV,  // header row + tab-separated (Excel paste)
    INGEST_JSON, // array of objects [{col: val, ...}, ...]
    INGEST_SQL,  // INSERT INTO statements
    INGEST_AUTO, // detect from file extension / content
} ingest_format_t;

typedef struct
{
    ingest_format_t fmt;
    char separator; // for CSV/TSV override
    int has_header; // 0 = no header row
    int hex_values; // 1 = parse values as hex
} ingest_opts_t;

#define INGEST_DEFAULTS (ingest_opts_t){.fmt = INGEST_AUTO, .separator = ',', .has_header = 1, .hex_values = 0}

// ingest file into an existing registry+backend
reg_status_t ingest_file(registry_t *r, const char *backend_name,
                         const char *path, ingest_opts_t opts);

// ingest raw buffer
reg_status_t ingest_buf(registry_t *r, const char *backend_name,
                        const char *buf, size_t len, ingest_opts_t opts);

// ingest .mem format directly into a backend (no registry schema needed)
bmp_status_t ingest_mem_raw(bmp_backend_t *b, const char *path);

// auto-detect format from file extension
ingest_format_t ingest_detect(const char *path);
// ── qck.dev State Sync ──────────────────────────────────────────────

typedef enum
{
    QCK_DEV_GDRIVE = 1,
    QCK_DEV_ONEDRIVE,
    QCK_DEV_CUSTOM
} qck_dev_provider_t;

typedef struct
{
    qck_dev_provider_t provider;
    const char *remote_root;
} qck_dev_target_t;

/*
 * Provider adapter.
 *
 * Google Drive / OneDrive / local / whatever implements this.
 * Core qck.dev does not care how bytes get there.
 */
typedef int (*qck_dev_put_fn)(
    qck_dev_provider_t provider,
    const char *remote_path,
    const void *data,
    size_t len,
    void *ctx);

typedef enum
{
    QCK_DEV_SYNC_OK = 0,
    QCK_DEV_SYNC_IO,
    QCK_DEV_SYNC_UPLOAD
} qck_dev_sync_status_t;

qck_dev_sync_status_t qckdev_up(
    const char *state_path,
    const qck_dev_target_t *targets,
    size_t n_targets,
    qck_dev_put_fn put,
    void *ctx);

#endif
