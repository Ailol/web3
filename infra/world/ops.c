#include "ops.h"

qck_status_t ops_eval(const qck_world_t *w, const qck_query_t *q, qck_plane_t *out) {
    qck_status_t st = plane_create(out, "", w->n_objects);
    if (st != QCK_OK) return st;

    // start = AND of all require planes; empty require means "all objects"
    int started = 0;
    if (q->require) {
        for (const char *const *n = q->require; *n; n++) {
            qck_plane_t *p = world_plane(w, *n);
            if (!p) return QCK_OK;              // unknown predicate → empty result
            if (!started) { plane_copy(p, out); started = 1; }
            else            plane_and(out, p, out);
        }
    }
    if (!started) plane_not(out, out);          // no require → all-ones (masked)

    if (q->with) plane_and(out, q->with, out);

    if (q->exclude) {
        for (const char *const *n = q->exclude; *n; n++) {
            qck_plane_t *p = world_plane(w, *n);
            if (p) plane_andnot(out, p, out);
        }
    }
    return QCK_OK;
}
