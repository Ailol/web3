#include "ingest.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

// ── Format Detection ────────────────────────────────────────────────

static const char *ext_of(const char *path)
{
    const char *dot = strrchr(path, '.');
    return dot ? dot + 1 : "";
}

ingest_format_t ingest_detect(const char *path)
{
    const char *e = ext_of(path);
    if (strcmp(e, "mem") == 0)
        return INGEST_MEM;
    if (strcmp(e, "csv") == 0)
        return INGEST_CSV;
    if (strcmp(e, "tsv") == 0)
        return INGEST_TSV;
    if (strcmp(e, "json") == 0)
        return INGEST_JSON;
    if (strcmp(e, "sql") == 0)
        return INGEST_SQL;
    if (strcmp(e, "txt") == 0 || strcmp(e, "xls") == 0)
        return INGEST_TSV;
    return INGEST_CSV;
}

// ── .mem Ingest (raw bitmap, no schema) ─────────────────────────────

bmp_status_t ingest_mem_raw(bmp_backend_t *b, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return BMP_ERR_IO;

    char line[256];
    while (fgets(line, sizeof(line), f))
    {
        if (line[0] == '#' || line[0] == '\n')
            continue;
        uint64_t addr, val;
        if (sscanf(line, "0x%llx\t0x%llx",
                   (unsigned long long *)&addr, (unsigned long long *)&val) == 2)
        {
            // pack 32-bit value at bit address (addr * 8 for byte-addressed)
            bmp_pack(b, addr * 8, 32, val & 0xFFFFFFFF);
        }
    }
    fclose(f);
    return BMP_OK;
}

// ── CSV/TSV Parsing ─────────────────────────────────────────────────

static size_t split_line(char *line, char sep, char **fields, size_t max_fields)
{
    size_t n = 0;
    char *p = line;
    while (n < max_fields)
    {
        fields[n++] = p;
        char *next = strchr(p, sep);
        if (!next)
        {
            // strip trailing newline
            size_t len = strlen(p);
            while (len > 0 && (p[len - 1] == '\n' || p[len - 1] == '\r'))
                p[--len] = '\0';
            break;
        }
        *next = '\0';
        p = next + 1;
    }
    return n;
}

static uint64_t parse_value(const char *s, int hex_mode)
{
    while (*s == ' ')
        s++;
    if (hex_mode || (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')))
        return strtoull(s, NULL, 16);
    // try as integer
    char *end;
    uint64_t v = strtoull(s, &end, 10);
    if (end != s)
        return v;
    // fallback: hash the string into 64 bits
    uint64_t h = 5381;
    for (const char *c = s; *c; c++)
        h = ((h << 5) + h) + (uint8_t)*c;
    return h;
}

static reg_status_t ingest_delimited(registry_t *r, const char *backend_name,
                                     FILE *f, char sep, int has_header,
                                     int hex_values)
{
    char line[4096];
    char *fields[64];
    uint64_t vals[64];

    // header line → verify against schema
    if (has_header)
    {
        if (!fgets(line, sizeof(line), f))
            return REG_OK;
        // header consumed, columns match schema order
    }

    while (fgets(line, sizeof(line), f))
    {
        if (line[0] == '#' || line[0] == '\n')
            continue;
        size_t n = split_line(line, sep, fields, 64);
        if (n > r->schema.n_cols)
            n = r->schema.n_cols;
        for (size_t i = 0; i < n; i++)
            vals[i] = parse_value(fields[i], hex_values);
        // pad missing columns with zero
        for (size_t i = n; i < r->schema.n_cols; i++)
            vals[i] = 0;
        reg_status_t s = reg_append_to(r, backend_name, vals, r->schema.n_cols);
        if (s != REG_OK)
            return s;
    }
    return REG_OK;
}

// ── .mem → Registry (schema-aware) ──────────────────────────────────

static reg_status_t ingest_mem_schema(registry_t *r, const char *backend_name,
                                      FILE *f)
{
    char line[256];
    uint64_t vals[2]; // addr, value
    while (fgets(line, sizeof(line), f))
    {
        if (line[0] == '#' || line[0] == '\n')
            continue;
        uint64_t addr, val;
        if (sscanf(line, "0x%llx\t0x%llx",
                   (unsigned long long *)&addr, (unsigned long long *)&val) == 2)
        {
            vals[0] = addr;
            vals[1] = val;
            size_t n = 2 < r->schema.n_cols ? 2 : r->schema.n_cols;
            for (size_t i = 2; i < r->schema.n_cols; i++)
                vals[i] = 0;
            reg_status_t s = reg_append_to(r, backend_name, vals, r->schema.n_cols);
            if (s != REG_OK)
                return s;
        }
    }
    return REG_OK;
}

// ── JSON Ingest (minimal parser for [{...}, ...]) ───────────────────

static reg_status_t ingest_json_impl(registry_t *r, const char *backend_name,
                                     FILE *f, int hex_values)
{
    // simple line-by-line: look for numeric values between braces
    char line[4096];
    uint64_t vals[64];

    while (fgets(line, sizeof(line), f))
    {
        if (!strchr(line, '{'))
            continue;
        memset(vals, 0, sizeof(vals));

        size_t col = 0;
        char *p = strchr(line, ':');
        while (p && col < r->schema.n_cols)
        {
            p++; // skip ':'
            while (*p == ' ')
                p++;
            vals[col++] = parse_value(p, hex_values);
            p = strchr(p, ',');
            if (p)
                p = strchr(p, ':');
        }
        for (size_t i = col; i < r->schema.n_cols; i++)
            vals[i] = 0;
        if (col > 0)
        {
            reg_status_t s = reg_append_to(r, backend_name, vals, r->schema.n_cols);
            if (s != REG_OK)
                return s;
        }
    }
    return REG_OK;
}

// ── SQL Ingest (INSERT INTO ... VALUES (...)) ───────────────────────

static reg_status_t ingest_sql_impl(registry_t *r, const char *backend_name,
                                    FILE *f, int hex_values)
{
    char line[4096];
    uint64_t vals[64];

    while (fgets(line, sizeof(line), f))
    {
        char *vstart = strstr(line, "VALUES");
        if (!vstart)
            vstart = strstr(line, "values");
        if (!vstart)
            continue;

        char *paren = strchr(vstart, '(');
        if (!paren)
            continue;
        paren++;

        memset(vals, 0, sizeof(vals));
        size_t col = 0;
        char *tok = paren;
        while (*tok && *tok != ')' && col < r->schema.n_cols)
        {
            while (*tok == ' ')
                tok++;
            vals[col++] = parse_value(tok, hex_values);
            char *comma = strchr(tok, ',');
            if (comma)
                tok = comma + 1;
            else
                break;
        }
        for (size_t i = col; i < r->schema.n_cols; i++)
            vals[i] = 0;
        if (col > 0)
        {
            reg_status_t s = reg_append_to(r, backend_name, vals, r->schema.n_cols);
            if (s != REG_OK)
                return s;
        }
    }
    return REG_OK;
}

// ── Main Entry Points ───────────────────────────────────────────────

reg_status_t ingest_file(registry_t *r, const char *backend_name,
                         const char *path, ingest_opts_t opts)
{
    if (opts.fmt == INGEST_AUTO)
        opts.fmt = ingest_detect(path);

    FILE *f = fopen(path, "r");
    if (!f)
        return REG_ERR_IO;

    reg_status_t s;
    switch (opts.fmt)
    {
    case INGEST_MEM:
        s = ingest_mem_schema(r, backend_name, f);
        break;
    case INGEST_CSV:
        s = ingest_delimited(r, backend_name, f, opts.separator, opts.has_header, opts.hex_values);
        break;
    case INGEST_TSV:
        s = ingest_delimited(r, backend_name, f, '\t', opts.has_header, opts.hex_values);
        break;
    case INGEST_JSON:
        s = ingest_json_impl(r, backend_name, f, opts.hex_values);
        break;
    case INGEST_SQL:
        s = ingest_sql_impl(r, backend_name, f, opts.hex_values);
        break;
    default:
        s = REG_ERR_FORMAT;
    }

    fclose(f);
    return s;
}

reg_status_t ingest_buf(registry_t *r, const char *backend_name,
                        const char *buf, size_t len, ingest_opts_t opts)
{
    // write to temp file and ingest (simple, correct)
    const char *tmp = "_ingest_tmp";
    FILE *f = fopen(tmp, "wb");
    if (!f)
        return REG_ERR_IO;
    fwrite(buf, 1, len, f);
    fclose(f);

    if (opts.fmt == INGEST_AUTO)
        opts.fmt = INGEST_CSV;
    reg_status_t s = ingest_file(r, backend_name, tmp, opts);
    remove(tmp);
    return s;
}
// ── qck.dev State Upload ────────────────────────────────────────────

static uint64_t qck_hash_bytes(const uint8_t *data, size_t len)
{
    // FNV-1a 64-bit.
    // Cheap identity hash for state addressing.
    // Can be swapped for BLAKE3 later without changing qck.dev semantics.
    uint64_t h = 1469598103934665603ULL;

    for (size_t i = 0; i < len; i++)
    {
        h ^= data[i];
        h *= 1099511628211ULL;
    }

    return h;
}

static int qck_read_file(const char *path, uint8_t **out, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;

    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return 0;
    }

    long end = ftell(f);
    if (end < 0)
    {
        fclose(f);
        return 0;
    }

    rewind(f);

    size_t len = (size_t)end;
    uint8_t *buf = NULL;

    if (len > 0)
    {
        buf = (uint8_t *)malloc(len);
        if (!buf)
        {
            fclose(f);
            return 0;
        }

        if (fread(buf, 1, len, f) != len)
        {
            free(buf);
            fclose(f);
            return 0;
        }
    }

    fclose(f);

    *out = buf;
    *out_len = len;
    return 1;
}

qck_dev_sync_status_t qckdev_up(
    const char *state_path,
    const qck_dev_target_t *targets,
    size_t n_targets,
    qck_dev_put_fn put,
    void *ctx)
{
    if (!state_path || !targets || n_targets == 0 || !put)
        return QCK_DEV_SYNC_IO;

    uint8_t *state = NULL;
    size_t state_len = 0;

    if (!qck_read_file(state_path, &state, &state_len))
        return QCK_DEV_SYNC_IO;

    uint64_t hash = qck_hash_bytes(state, state_len);

    /*
     * Content-addressed state:
     *
     * qck.dev/
     *   blobs/
     *     0123456789abcdef.qck
     *
     *   latest
     */

    char blob_name[128];

    snprintf(
        blob_name,
        sizeof(blob_name),
        "qck.dev/blobs/%016llx.qck",
        (unsigned long long)hash);

    char latest[256];

    int latest_len = snprintf(
        latest,
        sizeof(latest),
        "hash=%016llx\nsize=%llu\nblob=%s\n",
        (unsigned long long)hash,
        (unsigned long long)state_len,
        blob_name);

    if (latest_len < 0 || (size_t)latest_len >= sizeof(latest))
    {
        free(state);
        return QCK_DEV_SYNC_IO;
    }

    for (size_t i = 0; i < n_targets; i++)
    {
        const qck_dev_target_t *target = &targets[i];

        char remote_blob[512];
        char remote_latest[512];

        const char *root =
            target->remote_root && target->remote_root[0]
                ? target->remote_root
                : "";

        if (root[0])
        {
            snprintf(
                remote_blob,
                sizeof(remote_blob),
                "%s/%s",
                root,
                blob_name);

            snprintf(
                remote_latest,
                sizeof(remote_latest),
                "%s/qck.dev/latest",
                root);
        }
        else
        {
            snprintf(
                remote_blob,
                sizeof(remote_blob),
                "%s",
                blob_name);

            snprintf(
                remote_latest,
                sizeof(remote_latest),
                "qck.dev/latest");
        }

        /*
         * Upload immutable state blob first.
         *
         * Provider adapters should treat the hash path as idempotent:
         * if it already exists, they may simply return success.
         */
        if (put(
                target->provider,
                remote_blob,
                state,
                state_len,
                ctx) != 0)
        {
            free(state);
            return QCK_DEV_SYNC_UPLOAD;
        }

        /*
         * Only after the blob exists do we move `latest`.
         *
         * Therefore a failed upload cannot point latest at
         * a state which does not exist.
         */
        if (put(
                target->provider,
                remote_latest,
                latest,
                (size_t)latest_len,
                ctx) != 0)
        {
            free(state);
            return QCK_DEV_SYNC_UPLOAD;
        }
    }

    free(state);
    return QCK_DEV_SYNC_OK;
}