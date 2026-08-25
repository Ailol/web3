#ifndef BITMAP_BACKEND_H
#define BITMAP_BACKEND_H

#include <stdint.h>
#include <stddef.h>

// vtable for any bitmap backend — registry routes through this
// backends own the data, registry just accesses

typedef enum {
    BMP_OK = 0,
    BMP_ERR_ALLOC,
    BMP_ERR_OOB,
    BMP_ERR_IO,
    BMP_ERR_UNSUPPORTED,
} bmp_status_t;

typedef struct bmp_backend bmp_backend_t;

typedef struct {
    const char *name;

    void         (*destroy)(bmp_backend_t *b);

    uint64_t     (*total_bits)(const bmp_backend_t *b);
    bmp_status_t (*grow)(bmp_backend_t *b, uint64_t new_bits);

    int          (*get)(const bmp_backend_t *b, uint64_t pos);
    void         (*set)(bmp_backend_t *b, uint64_t pos);
    void         (*clear)(bmp_backend_t *b, uint64_t pos);

    uint64_t     (*extract)(const bmp_backend_t *b, uint64_t offset, uint32_t width);
    void         (*pack)(bmp_backend_t *b, uint64_t offset, uint32_t width, uint64_t val);

    bmp_status_t (*scan)(const bmp_backend_t *b,
                         uint64_t mask, uint64_t expect,
                         uint32_t offset, uint32_t width,
                         uint64_t lo, uint64_t hi, uint32_t stride,
                         uint64_t **hits, size_t *n_hits);

    uint64_t     (*popcount)(const bmp_backend_t *b);
    uint64_t     (*popcount_range)(const bmp_backend_t *b, uint64_t lo, uint64_t hi);

    bmp_status_t (*save)(const bmp_backend_t *b, const char *path);
    bmp_status_t (*load)(bmp_backend_t *b, const char *path);
    bmp_status_t (*sync)(bmp_backend_t *b);
} bmp_vtable_t;

struct bmp_backend {
    const bmp_vtable_t *vt;
    void               *ctx;
};

#define bmp_get(b, pos)             (b)->vt->get((b), (pos))
#define bmp_set(b, pos)             (b)->vt->set((b), (pos))
#define bmp_clear(b, pos)           (b)->vt->clear((b), (pos))
#define bmp_extract(b, off, w)      (b)->vt->extract((b), (off), (w))
#define bmp_pack(b, off, w, v)      (b)->vt->pack((b), (off), (w), (v))
#define bmp_total_bits(b)           (b)->vt->total_bits((b))
#define bmp_popcount(b)             (b)->vt->popcount((b))

#endif
