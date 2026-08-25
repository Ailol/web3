#ifndef QCK_RELATION_H
#define QCK_RELATION_H

#include "plane.h"

// R(relation, src) — Rₙ(Ω²): for each src, a bitplane of reachable dsts
// sparse: only rows with at least one link are allocated

#define QCK_REL_INIT_CAP 16

typedef struct {
    qck_id_t    src;
    qck_plane_t row;   // row.bits[dst] = 1  ↔  relation(src, dst)
} qck_rel_row_t;

typedef struct {
    char           name[64];
    qck_rel_row_t *rows;
    size_t         n_rows;
    size_t         cap;
    uint64_t       omega_size; // used to size new rows
} qck_relation_t;

qck_status_t rel_create (qck_relation_t *r, const char *name, uint64_t omega_size);
void         rel_destroy(qck_relation_t *r);

void rel_link  (qck_relation_t *r, qck_id_t src, qck_id_t dst);
void rel_unlink(qck_relation_t *r, qck_id_t src, qck_id_t dst);
int  rel_has   (const qck_relation_t *r, qck_id_t src, qck_id_t dst);

// R(rel, src): row plane for src, or NULL if no links from src
const qck_plane_t *rel_row(const qck_relation_t *r, qck_id_t src);

// transitive closure from seed: out contains all objects reachable via r
qck_status_t rel_closure(const qck_relation_t *r, const qck_plane_t *seed, qck_plane_t *out);

#endif
