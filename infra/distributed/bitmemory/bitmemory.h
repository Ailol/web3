#ifndef BITMEMORY_H
#define BITMEMORY_H

#include <stdint.h>
#include <stddef.h>

// adaptive rasterized quantum bitmemory
// sits below bitmatrix.rs — raw memory substrate with mmap + SIMD scan

// ── Memory Layout ───────────────────────────────────────────────────
// .mem file format: raw pages, mmap'd directly
// page = 4096 bytes = 32768 bits

#define BM_PAGE_SHIFT    12
#define BM_PAGE_SIZE     (1 << BM_PAGE_SHIFT)
#define BM_PAGE_BITS     (BM_PAGE_SIZE * 8)
#define BM_WORD_BITS     64
#define BM_WORDS_PER_PAGE (BM_PAGE_SIZE / sizeof(uint64_t))

#define BM_MAX_PAGES     65536
#define BM_ALIGN         64  // cache-line aligned

typedef enum {
    BM_OK = 0,
    BM_ERR_MMAP,
    BM_ERR_OOB,
    BM_ERR_PATTERN,
    BM_ERR_FULL,
} bm_status_t;

// ── Core Memory ─────────────────────────────────────────────────────

typedef struct {
    uint64_t *base;       // mmap'd or heap region
    size_t    n_pages;
    size_t    total_bits;
    int       fd;         // backing file descriptor, -1 if anonymous
    int       writable;
} bm_mem_t;

bm_status_t bm_open(bm_mem_t *mem, const char *path, int writable);
bm_status_t bm_create(bm_mem_t *mem, size_t n_pages);
void        bm_close(bm_mem_t *mem);

// direct bit ops
int      bm_get(const bm_mem_t *mem, uint64_t pos);
void     bm_set(bm_mem_t *mem, uint64_t pos);
void     bm_clear(bm_mem_t *mem, uint64_t pos);
void     bm_flip(bm_mem_t *mem, uint64_t pos);
uint64_t bm_popcount(const bm_mem_t *mem);
uint64_t bm_popcount_range(const bm_mem_t *mem, uint64_t lo, uint64_t hi);

// bulk: returns pointer to word containing bit `pos`
uint64_t *bm_word_at(bm_mem_t *mem, uint64_t pos);

// page-level
const uint64_t *bm_page(const bm_mem_t *mem, size_t page_idx);
uint64_t        bm_page_popcount(const bm_mem_t *mem, size_t page_idx);

// flush mmap'd pages back to .mem file
bm_status_t bm_sync(bm_mem_t *mem);

// ── SIMD Scan ───────────────────────────────────────────────────────
// bit-parallel pattern matching — the hardware regex

typedef struct {
    uint64_t mask;    // which bits matter
    uint64_t expect;  // expected values for those bits
    uint32_t stride;  // advance by this many bits per step
} bm_scan_op_t;

typedef struct {
    uint64_t *hits;     // positions where pattern matched
    size_t    n_hits;
    size_t    capacity;
} bm_scan_result_t;

// scan entire memory for pattern
bm_status_t bm_scan(const bm_mem_t *mem, const bm_scan_op_t *op,
                     bm_scan_result_t *out);

// scan within range [lo, hi)
bm_status_t bm_scan_range(const bm_mem_t *mem, const bm_scan_op_t *op,
                           uint64_t lo, uint64_t hi,
                           bm_scan_result_t *out);

void bm_scan_result_free(bm_scan_result_t *r);

// ── Adaptive Rasterization ──────────────────────────────────────────
// quantum-level: treat pages as raster tiles, density drives resolution

typedef struct {
    size_t   page_idx;
    uint64_t popcount;
    double   density;    // popcount / BM_PAGE_BITS
    uint8_t  quantum;    // adaptive resolution level 0-7
} bm_tile_t;

typedef struct {
    bm_tile_t *tiles;
    size_t     n_tiles;
} bm_raster_t;

bm_status_t bm_rasterize(const bm_mem_t *mem, bm_raster_t *out);
void        bm_raster_free(bm_raster_t *r);

// quantum level from density
uint8_t bm_quantum_level(double density);

// ── .mem Format I/O ─────────────────────────────────────────────────
// hex pair format compatible with MIPS .mem loader:
// 0xADDR\t0xVALUE per line

bm_status_t bm_load_hex(bm_mem_t *mem, const char *path);
bm_status_t bm_dump_hex(const bm_mem_t *mem, const char *path);

#endif
