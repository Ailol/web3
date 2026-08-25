#ifndef QCK_NIT_H
#define QCK_NIT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t nit_id_t;

enum {
    NIT_VISIBLE   = 1ull << 0,
    NIT_ENABLED   = 1ull << 1,
    NIT_CLICKABLE = 1ull << 2,
    NIT_SELECTED  = 1ull << 3
};

typedef struct {
    nit_id_t id;
    float x;
    float y;
    float w;
    float h;
    float z;
    nit_id_t parent;
    uint64_t flags;
} nit_box_t;

/* Geometry primitives. */
int nit_contains(const nit_box_t *box, float px, float py);
int nit_overlaps(const nit_box_t *a, const nit_box_t *b);
int nit_inside(const nit_box_t *inner, const nit_box_t *outer);

/* Resolve the top-most visible/enabled box at a point. 0 means no hit. */
nit_id_t nit_hit(const nit_box_t *boxes, size_t count, float px, float py);

/*
 * Write a bitmap of every box containing the point.
 * bit i corresponds to boxes[i]. Returns number of hit boxes.
 */
size_t nit_hit_bitmap(const nit_box_t *boxes, size_t count,
                      float px, float py,
                      uint64_t *words, size_t word_count);

/* Structural depth from parent relations. Root depth = 0. SIZE_MAX = invalid/cycle. */
size_t nit_depth(const nit_box_t *boxes, size_t count, nit_id_t id);

#ifdef __cplusplus
}
#endif

#endif
