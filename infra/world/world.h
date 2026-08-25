#ifndef QCK_WORLD_H
#define QCK_WORLD_H

#include "plane.h"
#include "relation.h"
#include "value.h"

// world = named composition of P + R + V over Ω
//
// desired world D  ~  current world C
//         Δ(C, D)  =  minimal ops to transform C into D
//
// planes/relations/values are caller-owned; world is a view over them

#define QCK_WORLD_MAX_PLANES    64
#define QCK_WORLD_MAX_RELATIONS 32
#define QCK_WORLD_MAX_VALUES    32

typedef struct {
    char            name[64];
    qck_plane_t    *planes[QCK_WORLD_MAX_PLANES];
    size_t          n_planes;
    qck_relation_t *relations[QCK_WORLD_MAX_RELATIONS];
    size_t          n_relations;
    qck_value_t    *values[QCK_WORLD_MAX_VALUES];
    size_t          n_values;
    uint64_t        n_objects;
} qck_world_t;

// per-plane diff
typedef struct {
    char        name[64];
    qck_plane_t to_set;    // in D, not in C  →  must set
    qck_plane_t to_clear;  // in C, not in D  →  must clear
} qck_plane_diff_t;

typedef struct {
    qck_plane_diff_t *planes;
    size_t            n_planes;
} qck_delta_t;

void         world_init   (qck_world_t *w, const char *name, uint64_t n_objects);
void         world_destroy(qck_world_t *w); // does NOT destroy planes/relations/values

qck_status_t world_add_plane   (qck_world_t *w, qck_plane_t *p);
qck_status_t world_add_relation(qck_world_t *w, qck_relation_t *r);
qck_status_t world_add_value   (qck_world_t *w, qck_value_t *v);

qck_plane_t    *world_plane   (const qck_world_t *w, const char *name);
qck_relation_t *world_relation(const qck_world_t *w, const char *name);
qck_value_t    *world_value   (const qck_world_t *w, const char *name);

// Δ(current, desired) — caller must world_delta_free(out) when done
qck_status_t world_diff      (const qck_world_t *current, const qck_world_t *desired, qck_delta_t *out);
qck_status_t world_apply     (qck_world_t *world, const qck_delta_t *delta); // minimal materialization
void         world_delta_free(qck_delta_t *delta);

#endif
