#include "nit.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    const nit_box_t boxes[] = {
        { 1,   0,   0, 800, 600, 0, 0, NIT_VISIBLE | NIT_ENABLED },
        { 2, 100, 100, 300, 200, 1, 1, NIT_VISIBLE | NIT_ENABLED },
        { 3, 150, 140, 120,  48, 2, 2, NIT_VISIBLE | NIT_ENABLED | NIT_CLICKABLE },
        { 4, 500, 100, 100, 100, 1, 1, NIT_VISIBLE | NIT_ENABLED }
    };

    assert(nit_contains(&boxes[2], 160, 150));
    assert(!nit_contains(&boxes[2], 20, 20));
    assert(nit_inside(&boxes[2], &boxes[1]));
    assert(nit_overlaps(&boxes[1], &boxes[2]));

    assert(nit_hit(boxes, 4, 160, 150) == 3);
    assert(nit_hit(boxes, 4, 520, 120) == 4);
    assert(nit_hit(boxes, 4, 900, 900) == 0);

    uint64_t hit[1];
    assert(nit_hit_bitmap(boxes, 4, 160, 150, hit, 1) == 3);
    assert((hit[0] & (1ull << 0)) != 0);
    assert((hit[0] & (1ull << 1)) != 0);
    assert((hit[0] & (1ull << 2)) != 0);

    assert(nit_depth(boxes, 4, 1) == 0);
    assert(nit_depth(boxes, 4, 2) == 1);
    assert(nit_depth(boxes, 4, 3) == 2);

    puts("qck.nit: 12/12 passed");
    return 0;
}
