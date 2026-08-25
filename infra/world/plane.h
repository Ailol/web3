#ifndef QCK_PLANE_H
#define QCK_PLANE_H

#include "qck_dev.h"
#include "infra/distributed/bitmemory/bitmemory.h"

// P(property) — one bit per object in Ω; physical form of a predicate
// bit[id] = 1  ↔  property(id) holds

typedef struct {
    char     name[64];
    bm_mem_t bits;
    uint64_t n_objects;
} qck_plane_t;

qck_status_t plane_create (qck_plane_t *p, const char *name, uint64_t n_objects);
void         plane_destroy(qck_plane_t *p);

int  plane_get   (const qck_plane_t *p, qck_id_t id);
void plane_set   (qck_plane_t *p, qck_id_t id);
void plane_clear (qck_plane_t *p, qck_id_t id);
void plane_toggle(qck_plane_t *p, qck_id_t id);

// word-level boolean composition into dst (dst may alias a or b)
void plane_and   (const qck_plane_t *a, const qck_plane_t *b, qck_plane_t *dst);
void plane_or    (const qck_plane_t *a, const qck_plane_t *b, qck_plane_t *dst);
void plane_xor   (const qck_plane_t *a, const qck_plane_t *b, qck_plane_t *dst);
void plane_not   (const qck_plane_t *a, qck_plane_t *dst);
void plane_andnot(const qck_plane_t *a, const qck_plane_t *b, qck_plane_t *dst); // a & ~b

void         plane_copy    (const qck_plane_t *src, qck_plane_t *dst);
uint64_t     plane_popcount(const qck_plane_t *p);
int          plane_any     (const qck_plane_t *p);

// collect IDs where predicate is set; caller must free(*ids_out)
qck_status_t plane_collect(const qck_plane_t *p, qck_id_t **ids_out, size_t *n_out);

#endif
