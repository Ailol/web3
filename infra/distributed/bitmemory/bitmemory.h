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

// ── Byte / Blob View ────────────────────────────────────────────────
// same substrate, byte-addressed: hold executables or arbitrary payloads
// alongside the bit planes. Zero-copy over the mmap.

uint8_t    *bm_bytes(bm_mem_t *mem);
size_t      bm_size_bytes(const bm_mem_t *mem);

bm_status_t bm_blob_write(bm_mem_t *mem, uint64_t byte_off,
                          const void *data, size_t len);
bm_status_t bm_blob_read(const bm_mem_t *mem, uint64_t byte_off,
                         void *dst, size_t len);
// slurp a whole file (e.g. an .exe) into the substrate at byte_off
bm_status_t bm_blob_load(bm_mem_t *mem, uint64_t byte_off,
                         const char *path, size_t *out_len);
bm_status_t bm_blob_save(const bm_mem_t *mem, uint64_t byte_off,
                         size_t len, const char *path);

// ── Binary Vector Lanes ─────────────────────────────────────────────
// a region of the substrate viewed as a {0,1} vector. Similarity is
// popcount-based and SIMD-accelerated — the "vectorized cosine":
//   dot(a,b) = popcount(a & b)
//   |a|      = sqrt(popcount(a))
//   cosine   = dot / sqrt(pa * pb)      (Otsuka-Ochiai)
//   jaccard  = dot / (pa + pb - dot)    (Tanimoto)
//   hamming  = pa + pb - 2*dot
// Dense float/int8 vectors can live as BM_SEG_BLOB lanes; the native,
// zero-decode similarity of this substrate is binary.

typedef struct {
    uint64_t bit_off;   // first bit of the vector in the substrate
    uint64_t dim;       // length in bits
} bm_bvec_t;

uint64_t bm_bvec_popcount(const bm_mem_t *mem, const bm_bvec_t *v);
uint64_t bm_bvec_dot     (const bm_mem_t *mem, const bm_bvec_t *a, const bm_bvec_t *b);
uint64_t bm_bvec_hamming (const bm_mem_t *mem, const bm_bvec_t *a, const bm_bvec_t *b);
double   bm_bvec_cosine  (const bm_mem_t *mem, const bm_bvec_t *a, const bm_bvec_t *b);
double   bm_bvec_jaccard (const bm_mem_t *mem, const bm_bvec_t *a, const bm_bvec_t *b);

// ── Multipurpose Segment Directory ──────────────────────────────────
// optional, self-describing container: one .mem holds bitmaps, vectors
// and blobs side by side. The directory occupies page 0; segments are
// page-aligned and start at page 1, so raw bit ops stay valid.

typedef enum {
    BM_SEG_BITMAP = 1,   // occupancy / flag plane
    BM_SEG_BVEC   = 2,   // binary vector lane (similarity)
    BM_SEG_BLOB   = 3,   // raw bytes (executables, payloads)
} bm_seg_kind_t;

typedef struct {
    uint32_t kind;       // bm_seg_kind_t
    uint32_t elem_bits;  // bvec: dim; blob: 8; bitmap: 1
    uint64_t byte_off;   // start offset in substrate
    uint64_t byte_len;   // length in bytes
    char     name[32];   // nul-terminated label
} bm_seg_t;

bm_status_t     bm_dir_init (bm_mem_t *mem);
size_t          bm_dir_count(const bm_mem_t *mem);
const bm_seg_t *bm_dir_get  (const bm_mem_t *mem, size_t i);
const bm_seg_t *bm_dir_find (const bm_mem_t *mem, const char *name);

// append a segment; its page-aligned byte offset is returned via *out_off
bm_status_t bm_seg_add(bm_mem_t *mem, const char *name, bm_seg_kind_t kind,
                       uint32_t elem_bits, uint64_t byte_len, uint64_t *out_off);

// ── .mem Format I/O ─────────────────────────────────────────────────
// hex pair format compatible with MIPS .mem loader:
// 0xADDR\t0xVALUE per line

bm_status_t bm_load_hex(bm_mem_t *mem, const char *path);
bm_status_t bm_dump_hex(const bm_mem_t *mem, const char *path);

#endif
