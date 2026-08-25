#include "bitmemory_backend.h"
#include "../../distributed/bitmemory/bitmemory.h"

#include <stdlib.h>
#include <string.h>

static void bm_destroy(bmp_backend_t *b) {
    bm_mem_t *m = (bm_mem_t *)b->ctx;
    bm_close(m);
    free(m);
    free(b);
}

static uint64_t bm_total(const bmp_backend_t *b) {
    return ((bm_mem_t *)b->ctx)->total_bits;
}

static bmp_status_t bm_grow_impl(bmp_backend_t *b, uint64_t new_bits) {
    (void)b; (void)new_bits;
    return BMP_ERR_UNSUPPORTED;
}

static int bm_get_impl(const bmp_backend_t *b, uint64_t pos) {
    return bm_get((const bm_mem_t *)b->ctx, pos);
}

static void bm_set_impl(bmp_backend_t *b, uint64_t pos) {
    bm_set((bm_mem_t *)b->ctx, pos);
}

static void bm_clear_impl(bmp_backend_t *b, uint64_t pos) {
    bm_clear((bm_mem_t *)b->ctx, pos);
}

static uint64_t bm_extract_impl(const bmp_backend_t *b, uint64_t offset, uint32_t width) {
    const bm_mem_t *m = (const bm_mem_t *)b->ctx;
    if (width == 0 || width > 64 || offset + width > m->total_bits) return 0;
    uint64_t w_idx = offset / 64;
    uint64_t shift = offset % 64;
    uint64_t mask = (width == 64) ? ~0ULL : ((1ULL << width) - 1);
    uint64_t val = m->base[w_idx] >> shift;
    if (shift + width > 64)
        val |= m->base[w_idx + 1] << (64 - shift);
    return val & mask;
}

static void bm_pack_impl(bmp_backend_t *b, uint64_t offset, uint32_t width, uint64_t val) {
    bm_mem_t *m = (bm_mem_t *)b->ctx;
    if (width == 0 || width > 64 || offset + width > m->total_bits) return;
    uint64_t w_idx = offset / 64;
    uint64_t shift = offset % 64;
    uint64_t mask = (width == 64) ? ~0ULL : ((1ULL << width) - 1);
    val &= mask;
    m->base[w_idx] &= ~(mask << shift);
    m->base[w_idx] |= val << shift;
    if (shift + width > 64) {
        uint32_t hi = (uint32_t)(shift + width - 64);
        uint64_t hi_mask = (1ULL << hi) - 1;
        m->base[w_idx + 1] &= ~hi_mask;
        m->base[w_idx + 1] |= val >> (64 - shift);
    }
}

static bmp_status_t bm_scan_impl(const bmp_backend_t *b,
                                  uint64_t mask, uint64_t expect,
                                  uint32_t offset, uint32_t width,
                                  uint64_t lo, uint64_t hi, uint32_t stride,
                                  uint64_t **hits, size_t *n_hits) {
    bm_scan_op_t op = { .mask = mask, .expect = expect, .stride = stride };
    bm_scan_result_t res = {0};
    (void)offset; (void)width;
    bm_status_t s = bm_scan_range((const bm_mem_t *)b->ctx, &op, lo, hi, &res);
    if (s != BM_OK) return BMP_ERR_OOB;
    *hits = res.hits;
    *n_hits = res.n_hits;
    return BMP_OK;
}

static uint64_t bm_popcount_impl(const bmp_backend_t *b) {
    return bm_popcount((const bm_mem_t *)b->ctx);
}

static uint64_t bm_popcount_range_impl(const bmp_backend_t *b, uint64_t lo, uint64_t hi) {
    return bm_popcount_range((const bm_mem_t *)b->ctx, lo, hi);
}

static bmp_status_t bm_save_impl(const bmp_backend_t *b, const char *path) {
    return bm_dump_hex((const bm_mem_t *)b->ctx, path) == BM_OK ? BMP_OK : BMP_ERR_IO;
}

static bmp_status_t bm_load_impl(bmp_backend_t *b, const char *path) {
    return bm_load_hex((bm_mem_t *)b->ctx, path) == BM_OK ? BMP_OK : BMP_ERR_IO;
}

static bmp_status_t bm_sync_impl(bmp_backend_t *b) {
    return bm_sync((bm_mem_t *)b->ctx) == BM_OK ? BMP_OK : BMP_ERR_IO;
}

static const bmp_vtable_t BITMEMORY_VT = {
    .name           = "bitmemory",
    .destroy        = bm_destroy,
    .total_bits     = bm_total,
    .grow           = bm_grow_impl,
    .get            = bm_get_impl,
    .set            = bm_set_impl,
    .clear          = bm_clear_impl,
    .extract        = bm_extract_impl,
    .pack           = bm_pack_impl,
    .scan           = bm_scan_impl,
    .popcount       = bm_popcount_impl,
    .popcount_range = bm_popcount_range_impl,
    .save           = bm_save_impl,
    .load           = bm_load_impl,
    .sync           = bm_sync_impl,
};

bmp_backend_t *bitmemory_backend_create(size_t n_pages) {
    bmp_backend_t *b = (bmp_backend_t *)malloc(sizeof(bmp_backend_t));
    if (!b) return NULL;
    bm_mem_t *m = (bm_mem_t *)malloc(sizeof(bm_mem_t));
    if (!m) { free(b); return NULL; }
    if (bm_create(m, n_pages) != BM_OK) { free(m); free(b); return NULL; }
    b->vt = &BITMEMORY_VT;
    b->ctx = m;
    return b;
}

bmp_backend_t *bitmemory_backend_open(const char *path, int writable) {
    bmp_backend_t *b = (bmp_backend_t *)malloc(sizeof(bmp_backend_t));
    if (!b) return NULL;
    bm_mem_t *m = (bm_mem_t *)malloc(sizeof(bm_mem_t));
    if (!m) { free(b); return NULL; }
    if (bm_open(m, path, writable) != BM_OK) { free(m); free(b); return NULL; }
    b->vt = &BITMEMORY_VT;
    b->ctx = m;
    return b;
}
