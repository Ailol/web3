#ifndef QCK_PROJECTION_H
#define QCK_PROJECTION_H

#include "world.h"
#include "ops.h"

// projection = query over Ω  +  render to a target
//
// Every external system is a projection of the same world. The query half is
// data (a qck_query_t); only render differs per target:
//
//   {require:{"needed","linux",0}}  →  render_docker_compose
//   {require:{"needed","linux",0}}  →  render_systemd
//   {require:{"needed","runnable",0}} → render_mongo
//   {require:{"visible",0}}          →  render_vscode

typedef qck_status_t (*qck_render_fn)(const qck_world_t *w, const qck_plane_t *result, void *ctx);

typedef struct {
    char          name[64];
    qck_query_t   query;   // data-driven selection over the world
    qck_render_fn render;  // turns the selected set into the target representation
    void         *ctx;     // render-specific state (connection, handle, buffer, ...)
} qck_projection_t;

// run query → render; the result plane is created and destroyed internally
qck_status_t projection_run(const qck_projection_t *proj, const qck_world_t *w);

#endif
