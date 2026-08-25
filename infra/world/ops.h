#ifndef QCK_OPS_H
#define QCK_OPS_H

#include "world.h"

// A query is data, not code.
//
//   out = ( & require[i] )  &  with  &  ~( | exclude[j] )
//
// Plane names are plain strings, resolved against the world at eval time.
// A vocabulary (needed/runnable/linux, audio/lossless/liked,
// plugin/muted/frozen, tab/active/pinned, ...) therefore lives as data —
// a .qck file or an inline table — never as compiled #defines. Adding an app
// = adding a query table + a render function. Zero recompile.
//
// Missing require plane → unknown predicate → false everywhere → empty result.
// Missing exclude plane → nothing to subtract → skipped.

typedef struct {
    const char *const *require;  // NULL-terminated; ANDed together (NULL/empty = all objects)
    const char *const *exclude;  // NULL-terminated or NULL; each ANDNOT'd if present
    const qck_plane_t *with;     // optional runtime plane ANDed in (e.g. a closure result); may be NULL
} qck_query_t;

// out is created by the call; caller must plane_destroy(out).
qck_status_t ops_eval(const qck_world_t *w, const qck_query_t *q, qck_plane_t *out);

#endif
