#ifndef QCK_VALUE_H
#define QCK_VALUE_H

#include "qck_dev.h"
#include "infra/distributed/bitmemory/bitmemory.h"

// V(value, id) — Vₙ(Ω): multi-bit value per object (width ≤ 64)
// layout: object i occupies bits [i*width .. i*width + width − 1]
// use for: versions, hashes, sizes, timestamps

typedef struct {
    char     name[64];
    uint32_t width;      // bits per value
    bm_mem_t bits;
    uint64_t n_objects;
} qck_value_t;

qck_status_t val_create (qck_value_t *v, const char *name, uint32_t width, uint64_t n_objects);
void         val_destroy(qck_value_t *v);

uint64_t val_get(const qck_value_t *v, qck_id_t id);
void     val_set(qck_value_t *v, qck_id_t id, uint64_t value);

#endif
