#include "nit.h"

#include <float.h>
#include <string.h>

static const nit_box_t *nit_find(const nit_box_t *boxes, size_t count, nit_id_t id) {
    if (!boxes || id == 0) return NULL;
    for (size_t i = 0; i < count; ++i)
        if (boxes[i].id == id) return &boxes[i];
    return NULL;
}

int nit_contains(const nit_box_t *b, float px, float py) {
    if (!b || b->w < 0.0f || b->h < 0.0f) return 0;
    return px >= b->x && py >= b->y &&
           px < (b->x + b->w) && py < (b->y + b->h);
}

int nit_overlaps(const nit_box_t *a, const nit_box_t *b) {
    if (!a || !b || a->w < 0.0f || a->h < 0.0f ||
        b->w < 0.0f || b->h < 0.0f) return 0;

    return a->x < b->x + b->w && a->x + a->w > b->x &&
           a->y < b->y + b->h && a->y + a->h > b->y;
}

int nit_inside(const nit_box_t *inner, const nit_box_t *outer) {
    if (!inner || !outer || inner->w < 0.0f || inner->h < 0.0f ||
        outer->w < 0.0f || outer->h < 0.0f) return 0;

    return inner->x >= outer->x && inner->y >= outer->y &&
           inner->x + inner->w <= outer->x + outer->w &&
           inner->y + inner->h <= outer->y + outer->h;
}

nit_id_t nit_hit(const nit_box_t *boxes, size_t count, float px, float py) {
    if (!boxes) return 0;

    nit_id_t hit = 0;
    float best_z = -FLT_MAX;

    for (size_t i = 0; i < count; ++i) {
        const nit_box_t *b = &boxes[i];
        const uint64_t gate = NIT_VISIBLE | NIT_ENABLED;
        if ((b->flags & gate) != gate) continue;
        if (!nit_contains(b, px, py)) continue;

        if (hit == 0 || b->z > best_z || (b->z == best_z && b->id > hit)) {
            best_z = b->z;
            hit = b->id;
        }
    }

    return hit;
}

size_t nit_hit_bitmap(const nit_box_t *boxes, size_t count,
                      float px, float py,
                      uint64_t *words, size_t word_count) {
    if (!words || word_count == 0) return 0;
    memset(words, 0, word_count * sizeof(*words));
    if (!boxes) return 0;

    const size_t limit = count < word_count * 64 ? count : word_count * 64;
    size_t hits = 0;

    for (size_t i = 0; i < limit; ++i) {
        const nit_box_t *b = &boxes[i];
        if (!(b->flags & NIT_VISIBLE)) continue;
        if (!nit_contains(b, px, py)) continue;
        words[i >> 6] |= 1ull << (i & 63);
        ++hits;
    }

    return hits;
}

size_t nit_depth(const nit_box_t *boxes, size_t count, nit_id_t id) {
    const nit_box_t *node = nit_find(boxes, count, id);
    if (!node) return SIZE_MAX;

    size_t depth = 0;
    nit_id_t parent = node->parent;

    while (parent != 0) {
        if (depth >= count) return SIZE_MAX; /* parent cycle */
        node = nit_find(boxes, count, parent);
        if (!node) return SIZE_MAX;
        parent = node->parent;
        ++depth;
    }

    return depth;
}
