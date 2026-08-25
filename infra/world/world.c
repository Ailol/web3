#include "world.h"
#include <stdlib.h>
#include <string.h>

// ── omega ────────────────────────────────────────────────────────────

void omega_init(qck_omega_t *o, qck_id_t initial_cap) {
    o->next = 0;
    o->cap  = initial_cap ? initial_cap : 65536;
}

qck_id_t omega_alloc(qck_omega_t *o) {
    qck_id_t id = o->next++;
    if (o->next > o->cap) o->cap = o->next;
    return id;
}

uint64_t omega_size(const qck_omega_t *o) { return o->next; }

// ── world ────────────────────────────────────────────────────────────

void world_init(qck_world_t *w, const char *name, uint64_t n_objects) {
    memset(w, 0, sizeof(*w));
    strncpy(w->name, name, 63); w->name[63] = '\0';
    w->n_objects = n_objects;
}

void world_destroy(qck_world_t *w) { memset(w, 0, sizeof(*w)); }

qck_status_t world_add_plane(qck_world_t *w, qck_plane_t *p) {
    if (w->n_planes >= QCK_WORLD_MAX_PLANES) return QCK_ERR_FULL;
    w->planes[w->n_planes++] = p;
    return QCK_OK;
}
qck_status_t world_add_relation(qck_world_t *w, qck_relation_t *r) {
    if (w->n_relations >= QCK_WORLD_MAX_RELATIONS) return QCK_ERR_FULL;
    w->relations[w->n_relations++] = r;
    return QCK_OK;
}
qck_status_t world_add_value(qck_world_t *w, qck_value_t *v) {
    if (w->n_values >= QCK_WORLD_MAX_VALUES) return QCK_ERR_FULL;
    w->values[w->n_values++] = v;
    return QCK_OK;
}

qck_plane_t *world_plane(const qck_world_t *w, const char *name) {
    for (size_t i = 0; i < w->n_planes; i++)
        if (strcmp(w->planes[i]->name, name) == 0) return w->planes[i];
    return NULL;
}
qck_relation_t *world_relation(const qck_world_t *w, const char *name) {
    for (size_t i = 0; i < w->n_relations; i++)
        if (strcmp(w->relations[i]->name, name) == 0) return w->relations[i];
    return NULL;
}
qck_value_t *world_value(const qck_world_t *w, const char *name) {
    for (size_t i = 0; i < w->n_values; i++)
        if (strcmp(w->values[i]->name, name) == 0) return w->values[i];
    return NULL;
}

// ── Δ(C, D) ──────────────────────────────────────────────────────────

qck_status_t world_diff(const qck_world_t *current, const qck_world_t *desired,
                         qck_delta_t *out) {
    memset(out, 0, sizeof(*out));
    if (!desired->n_planes) return QCK_OK;

    out->planes = (qck_plane_diff_t *)calloc(desired->n_planes, sizeof(qck_plane_diff_t));
    if (!out->planes) return QCK_ERR_ALLOC;

    for (size_t i = 0; i < desired->n_planes; i++) {
        qck_plane_t      *d    = desired->planes[i];
        qck_plane_t      *c    = world_plane(current, d->name);
        qck_plane_diff_t *diff = &out->planes[out->n_planes];

        strncpy(diff->name, d->name, 63); diff->name[63] = '\0';
        if (plane_create(&diff->to_set,   "", d->n_objects) != QCK_OK) return QCK_ERR_ALLOC;
        if (plane_create(&diff->to_clear, "", d->n_objects) != QCK_OK) return QCK_ERR_ALLOC;

        if (!c) {
            plane_copy(d, &diff->to_set); // no current state: everything in desired must be set
        } else {
            plane_andnot(d, c, &diff->to_set);   // in D, not in C
            plane_andnot(c, d, &diff->to_clear); // in C, not in D
        }
        out->n_planes++;
    }
    return QCK_OK;
}

qck_status_t world_apply(qck_world_t *world, const qck_delta_t *delta) {
    for (size_t i = 0; i < delta->n_planes; i++) {
        const qck_plane_diff_t *diff = &delta->planes[i];
        qck_plane_t *p = world_plane(world, diff->name);
        if (!p) continue;

        qck_id_t *ids = NULL; size_t n = 0;
        if (plane_collect(&diff->to_set, &ids, &n) == QCK_OK) {
            for (size_t j = 0; j < n; j++) plane_set(p, ids[j]);
            free(ids);
        }
        ids = NULL; n = 0;
        if (plane_collect(&diff->to_clear, &ids, &n) == QCK_OK) {
            for (size_t j = 0; j < n; j++) plane_clear(p, ids[j]);
            free(ids);
        }
    }
    return QCK_OK;
}

void world_delta_free(qck_delta_t *delta) {
    for (size_t i = 0; i < delta->n_planes; i++) {
        plane_destroy(&delta->planes[i].to_set);
        plane_destroy(&delta->planes[i].to_clear);
    }
    free(delta->planes);
    delta->planes  = NULL;
    delta->n_planes = 0;
}
