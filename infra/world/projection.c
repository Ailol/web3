#include "projection.h"

qck_status_t projection_run(const qck_projection_t *proj, const qck_world_t *w) {
    qck_plane_t  result;
    qck_status_t st = ops_eval(w, &proj->query, &result);
    if (st != QCK_OK) return st;
    if (proj->render) st = proj->render(w, &result, proj->ctx);
    plane_destroy(&result);
    return st;
}
