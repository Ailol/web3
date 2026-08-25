#include "plane.h"
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
#include <intrin.h>
static int ctz64(uint64_t x) { unsigned long i; _BitScanForward64(&i, x); return (int)i; }
#else
static int ctz64(uint64_t x) { return __builtin_ctzll(x); }
#endif

static size_t words_of(const qck_plane_t *p) {
    return (p->n_objects + 63) / 64;
}

qck_status_t plane_create(qck_plane_t *p, const char *name, uint64_t n_objects) {
    size_t pages = (n_objects + BM_PAGE_BITS - 1) / BM_PAGE_BITS;
    if (!pages) pages = 1;
    if (bm_create(&p->bits, pages) != BM_OK) return QCK_ERR_ALLOC;
    strncpy(p->name, name, 63); p->name[63] = '\0';
    p->n_objects = n_objects;
    return QCK_OK;
}

void plane_destroy(qck_plane_t *p) { bm_close(&p->bits); p->n_objects = 0; }

int  plane_get   (const qck_plane_t *p, qck_id_t id) { return bm_get  (&p->bits, id); }
void plane_set   (qck_plane_t *p,       qck_id_t id) { bm_set  (&p->bits, id); }
void plane_clear (qck_plane_t *p,       qck_id_t id) { bm_clear(&p->bits, id); }
void plane_toggle(qck_plane_t *p,       qck_id_t id) { bm_flip (&p->bits, id); }

void plane_and(const qck_plane_t *a, const qck_plane_t *b, qck_plane_t *dst) {
    size_t w = words_of(dst);
    for (size_t i = 0; i < w; i++) dst->bits.base[i] = a->bits.base[i] & b->bits.base[i];
}
void plane_or(const qck_plane_t *a, const qck_plane_t *b, qck_plane_t *dst) {
    size_t w = words_of(dst);
    for (size_t i = 0; i < w; i++) dst->bits.base[i] = a->bits.base[i] | b->bits.base[i];
}
void plane_xor(const qck_plane_t *a, const qck_plane_t *b, qck_plane_t *dst) {
    size_t w = words_of(dst);
    for (size_t i = 0; i < w; i++) dst->bits.base[i] = a->bits.base[i] ^ b->bits.base[i];
}
void plane_not(const qck_plane_t *a, qck_plane_t *dst) {
    size_t   w   = words_of(dst);
    uint64_t rem = dst->n_objects & 63;
    for (size_t i = 0; i < w; i++) dst->bits.base[i] = ~a->bits.base[i];
    if (rem) dst->bits.base[w - 1] &= (UINT64_C(1) << rem) - 1; // mask phantom bits
}
void plane_andnot(const qck_plane_t *a, const qck_plane_t *b, qck_plane_t *dst) {
    size_t w = words_of(dst);
    for (size_t i = 0; i < w; i++) dst->bits.base[i] = a->bits.base[i] & ~b->bits.base[i];
}

void plane_copy(const qck_plane_t *src, qck_plane_t *dst) {
    memcpy(dst->bits.base, src->bits.base, words_of(dst) * sizeof(uint64_t));
}

uint64_t plane_popcount(const qck_plane_t *p) { return bm_popcount(&p->bits); }

int plane_any(const qck_plane_t *p) {
    size_t w = words_of(p);
    for (size_t i = 0; i < w; i++) if (p->bits.base[i]) return 1;
    return 0;
}

qck_status_t plane_collect(const qck_plane_t *p, qck_id_t **ids_out, size_t *n_out) {
    uint64_t count = plane_popcount(p);
    *ids_out = NULL; *n_out = 0;
    if (!count) return QCK_OK;

    qck_id_t *ids = (qck_id_t *)malloc(count * sizeof(qck_id_t));
    if (!ids) return QCK_ERR_ALLOC;

    size_t w = words_of(p), n = 0;
    for (size_t i = 0; i < w; i++) {
        uint64_t word = p->bits.base[i];
        while (word) {
            int      bit = ctz64(word);
            qck_id_t id  = (qck_id_t)(i * 64 + (uint64_t)bit);
            if (id < p->n_objects) ids[n++] = id;
            word &= word - 1;
        }
    }
    *ids_out = ids; *n_out = n;
    return QCK_OK;
}
