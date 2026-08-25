#include "value.h"
#include <string.h>

qck_status_t val_create(qck_value_t *v, const char *name, uint32_t width, uint64_t n_objects) {
    uint64_t total = (uint64_t)width * n_objects;
    size_t   pages = (total + BM_PAGE_BITS - 1) / BM_PAGE_BITS;
    if (!pages) pages = 1;
    if (bm_create(&v->bits, pages) != BM_OK) return QCK_ERR_ALLOC;
    strncpy(v->name, name, 63); v->name[63] = '\0';
    v->width = width; v->n_objects = n_objects;
    return QCK_OK;
}

void val_destroy(qck_value_t *v) { bm_close(&v->bits); v->n_objects = 0; }

uint64_t val_get(const qck_value_t *v, qck_id_t id) {
    uint64_t pos = id * v->width, val = 0;
    for (uint32_t b = 0; b < v->width; b++)
        if (bm_get(&v->bits, pos + b)) val |= UINT64_C(1) << b;
    return val;
}

void val_set(qck_value_t *v, qck_id_t id, uint64_t value) {
    uint64_t pos = id * v->width;
    for (uint32_t b = 0; b < v->width; b++) {
        if (value & (UINT64_C(1) << b)) bm_set  (&v->bits, pos + b);
        else                             bm_clear(&v->bits, pos + b);
    }
}
