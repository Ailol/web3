#include "relation.h"
#include <stdlib.h>
#include <string.h>

qck_status_t rel_create(qck_relation_t *r, const char *name, uint64_t omega_size) {
    memset(r, 0, sizeof(*r));
    strncpy(r->name, name, 63); r->name[63] = '\0';
    r->omega_size = omega_size;
    r->cap  = QCK_REL_INIT_CAP;
    r->rows = (qck_rel_row_t *)malloc(r->cap * sizeof(qck_rel_row_t));
    return r->rows ? QCK_OK : QCK_ERR_ALLOC;
}

void rel_destroy(qck_relation_t *r) {
    for (size_t i = 0; i < r->n_rows; i++) plane_destroy(&r->rows[i].row);
    free(r->rows);
    memset(r, 0, sizeof(*r));
}

static qck_rel_row_t *find_row(qck_relation_t *r, qck_id_t src) {
    for (size_t i = 0; i < r->n_rows; i++)
        if (r->rows[i].src == src) return &r->rows[i];
    return NULL;
}

static qck_rel_row_t *ensure_row(qck_relation_t *r, qck_id_t src) {
    qck_rel_row_t *row = find_row(r, src);
    if (row) return row;
    if (r->n_rows >= r->cap) {
        size_t nc = r->cap * 2;
        qck_rel_row_t *tmp = (qck_rel_row_t *)realloc(r->rows, nc * sizeof(*r->rows));
        if (!tmp) return NULL;
        r->rows = tmp; r->cap = nc;
    }
    qck_rel_row_t *nr = &r->rows[r->n_rows];
    nr->src = src;
    if (plane_create(&nr->row, "", r->omega_size) != QCK_OK) return NULL;
    r->n_rows++;
    return nr;
}

void rel_link(qck_relation_t *r, qck_id_t src, qck_id_t dst) {
    qck_rel_row_t *row = ensure_row(r, src);
    if (row) plane_set(&row->row, dst);
}

void rel_unlink(qck_relation_t *r, qck_id_t src, qck_id_t dst) {
    qck_rel_row_t *row = find_row(r, src);
    if (row) plane_clear(&row->row, dst);
}

int rel_has(const qck_relation_t *r, qck_id_t src, qck_id_t dst) {
    for (size_t i = 0; i < r->n_rows; i++)
        if (r->rows[i].src == src) return plane_get(&r->rows[i].row, dst);
    return 0;
}

const qck_plane_t *rel_row(const qck_relation_t *r, qck_id_t src) {
    for (size_t i = 0; i < r->n_rows; i++)
        if (r->rows[i].src == src) return &r->rows[i].row;
    return NULL;
}

qck_status_t rel_closure(const qck_relation_t *r, const qck_plane_t *seed, qck_plane_t *out) {
    uint64_t     n  = out->n_objects;
    qck_plane_t  frontier, next;
    qck_status_t st;

    plane_copy(seed, out);

    if ((st = plane_create(&frontier, "", n)) != QCK_OK) return st;
    if ((st = plane_create(&next,     "", n)) != QCK_OK) { plane_destroy(&frontier); return st; }
    plane_copy(seed, &frontier);

    while (plane_any(&frontier)) {
        // reset next
        memset(next.bits.base, 0, ((n + 63) / 64) * sizeof(uint64_t));

        // OR in all rows reachable from frontier
        qck_id_t *ids = NULL; size_t n_ids = 0;
        if (plane_collect(&frontier, &ids, &n_ids) != QCK_OK) break;
        for (size_t i = 0; i < n_ids; i++) {
            const qck_plane_t *rw = rel_row(r, ids[i]);
            if (rw) plane_or(&next, rw, &next);
        }
        free(ids);

        plane_andnot(&next, out, &frontier); // newly discovered
        plane_or(out, &next, out);
    }

    plane_destroy(&frontier);
    plane_destroy(&next);
    return QCK_OK;
}
