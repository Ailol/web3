#include "bitmemory.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#ifdef __AVX2__
#include <immintrin.h>
#endif

// ── Platform mmap abstraction ───────────────────────────────────────

#ifdef _WIN32

static uint64_t *plat_mmap(int fd, size_t size, int writable, HANDLE *mapping) {
    HANDLE fh = (HANDLE)_get_osfhandle(fd);
    DWORD protect = writable ? PAGE_READWRITE : PAGE_READONLY;
    DWORD access  = writable ? FILE_MAP_WRITE : FILE_MAP_READ;
    *mapping = CreateFileMappingA(fh, NULL, protect, 0, (DWORD)size, NULL);
    if (!*mapping) return NULL;
    return (uint64_t *)MapViewOfFile(*mapping, access, 0, 0, size);
}

static void plat_munmap(uint64_t *base, size_t size, HANDLE mapping) {
    (void)size;
    UnmapViewOfFile(base);
    CloseHandle(mapping);
}

static int plat_msync(uint64_t *base, size_t size) {
    return FlushViewOfFile(base, size) ? 0 : -1;
}

#else

static uint64_t *plat_mmap_unix(int fd, size_t size, int writable) {
    int prot = PROT_READ | (writable ? PROT_WRITE : 0);
    void *p = mmap(NULL, size, prot, MAP_SHARED, fd, 0);
    return (p == MAP_FAILED) ? NULL : (uint64_t *)p;
}

static void plat_munmap_unix(uint64_t *base, size_t size) {
    munmap(base, size);
}

static int plat_msync_unix(uint64_t *base, size_t size) {
    return msync(base, size, MS_SYNC);
}

#endif

// ── Aligned heap fallback ───────────────────────────────────────────

static uint64_t *aligned_calloc(size_t size) {
#ifdef _WIN32
    return (uint64_t *)_aligned_malloc(size, BM_ALIGN);
#else
    void *p = NULL;
    if (posix_memalign(&p, BM_ALIGN, size)) return NULL;
    memset(p, 0, size);
    return (uint64_t *)p;
#endif
}

static void aligned_free(uint64_t *p) {
#ifdef _WIN32
    _aligned_free(p);
#else
    free(p);
#endif
}

// ── Core Memory ─────────────────────────────────────────────────────

#ifdef _WIN32
static HANDLE g_mapping = NULL;
#endif

bm_status_t bm_open(bm_mem_t *mem, const char *path, int writable) {
    memset(mem, 0, sizeof(*mem));
    mem->fd = -1;

#ifdef _WIN32
    int flags = writable ? _O_RDWR | _O_BINARY : _O_RDONLY | _O_BINARY;
    int fd = _open(path, flags);
    if (fd < 0) return BM_ERR_MMAP;

    long sz = _filelength(fd);
    if (sz <= 0) { _close(fd); return BM_ERR_MMAP; }

    size_t file_size = (size_t)sz;
    size_t n_pages = (file_size + BM_PAGE_SIZE - 1) / BM_PAGE_SIZE;
    size_t map_size = n_pages * BM_PAGE_SIZE;

    HANDLE mapping;
    uint64_t *base = plat_mmap(fd, map_size, writable, &mapping);
    if (!base) { _close(fd); return BM_ERR_MMAP; }
    g_mapping = mapping;
#else
    int flags = writable ? O_RDWR : O_RDONLY;
    int fd = open(path, flags);
    if (fd < 0) return BM_ERR_MMAP;

    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); return BM_ERR_MMAP; }

    size_t file_size = (size_t)st.st_size;
    size_t n_pages = (file_size + BM_PAGE_SIZE - 1) / BM_PAGE_SIZE;
    size_t map_size = n_pages * BM_PAGE_SIZE;

    uint64_t *base = plat_mmap_unix(fd, map_size, writable);
    if (!base) { close(fd); return BM_ERR_MMAP; }
#endif

    mem->base = base;
    mem->n_pages = n_pages;
    mem->total_bits = n_pages * BM_PAGE_BITS;
    mem->fd = fd;
    mem->writable = writable;
    return BM_OK;
}

bm_status_t bm_create(bm_mem_t *mem, size_t n_pages) {
    memset(mem, 0, sizeof(*mem));
    if (n_pages == 0 || n_pages > BM_MAX_PAGES) return BM_ERR_OOB;

    size_t size = n_pages * BM_PAGE_SIZE;
    uint64_t *base = aligned_calloc(size);
    if (!base) return BM_ERR_MMAP;

    mem->base = base;
    mem->n_pages = n_pages;
    mem->total_bits = n_pages * BM_PAGE_BITS;
    mem->fd = -1;
    mem->writable = 1;
    return BM_OK;
}

void bm_close(bm_mem_t *mem) {
    if (!mem->base) return;

    if (mem->fd >= 0) {
        size_t map_size = mem->n_pages * BM_PAGE_SIZE;
#ifdef _WIN32
        plat_munmap(mem->base, map_size, g_mapping);
        g_mapping = NULL;
        _close(mem->fd);
#else
        plat_munmap_unix(mem->base, map_size);
        close(mem->fd);
#endif
    } else {
        aligned_free(mem->base);
    }
    memset(mem, 0, sizeof(*mem));
    mem->fd = -1;
}

bm_status_t bm_sync(bm_mem_t *mem) {
    if (mem->fd < 0 || !mem->writable) return BM_OK;
    size_t size = mem->n_pages * BM_PAGE_SIZE;
#ifdef _WIN32
    return plat_msync(mem->base, size) == 0 ? BM_OK : BM_ERR_MMAP;
#else
    return plat_msync_unix(mem->base, size) == 0 ? BM_OK : BM_ERR_MMAP;
#endif
}

// ── Bit Operations ──────────────────────────────────────────────────

static inline int oob(const bm_mem_t *mem, uint64_t pos) {
    return pos >= mem->total_bits;
}

int bm_get(const bm_mem_t *mem, uint64_t pos) {
    if (oob(mem, pos)) return 0;
    uint64_t word = mem->base[pos / BM_WORD_BITS];
    return (word >> (pos % BM_WORD_BITS)) & 1;
}

void bm_set(bm_mem_t *mem, uint64_t pos) {
    if (oob(mem, pos)) return;
    mem->base[pos / BM_WORD_BITS] |= (1ULL << (pos % BM_WORD_BITS));
}

void bm_clear(bm_mem_t *mem, uint64_t pos) {
    if (oob(mem, pos)) return;
    mem->base[pos / BM_WORD_BITS] &= ~(1ULL << (pos % BM_WORD_BITS));
}

void bm_flip(bm_mem_t *mem, uint64_t pos) {
    if (oob(mem, pos)) return;
    mem->base[pos / BM_WORD_BITS] ^= (1ULL << (pos % BM_WORD_BITS));
}

uint64_t *bm_word_at(bm_mem_t *mem, uint64_t pos) {
    if (oob(mem, pos)) return NULL;
    return &mem->base[pos / BM_WORD_BITS];
}

const uint64_t *bm_page(const bm_mem_t *mem, size_t page_idx) {
    if (page_idx >= mem->n_pages) return NULL;
    return &mem->base[page_idx * BM_WORDS_PER_PAGE];
}

// ── Popcount ────────────────────────────────────────────────────────

#if defined(__GNUC__) || defined(__clang__)
#define POPCNT64(x) __builtin_popcountll(x)
#elif defined(_MSC_VER)
#include <intrin.h>
#define POPCNT64(x) __popcnt64(x)
#else
static inline uint64_t popcnt_soft(uint64_t x) {
    x = x - ((x >> 1) & 0x5555555555555555ULL);
    x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
    return (((x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL) * 0x0101010101010101ULL) >> 56;
}
#define POPCNT64(x) popcnt_soft(x)
#endif

uint64_t bm_page_popcount(const bm_mem_t *mem, size_t page_idx) {
    const uint64_t *page = bm_page(mem, page_idx);
    if (!page) return 0;
    uint64_t count = 0;

#ifdef __AVX2__
    const __m256i *vp = (const __m256i *)page;
    __m256i acc = _mm256_setzero_si256();
    const __m256i lookup = _mm256_setr_epi8(
        0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4,
        0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4);
    const __m256i mask4 = _mm256_set1_epi8(0x0F);

    for (size_t i = 0; i < BM_WORDS_PER_PAGE / 4; i++) {
        __m256i v = _mm256_load_si256(&vp[i]);
        __m256i lo = _mm256_and_si256(v, mask4);
        __m256i hi = _mm256_and_si256(_mm256_srli_epi16(v, 4), mask4);
        __m256i popcnt = _mm256_add_epi8(
            _mm256_shuffle_epi8(lookup, lo),
            _mm256_shuffle_epi8(lookup, hi));
        acc = _mm256_add_epi64(acc, _mm256_sad_epu8(popcnt, _mm256_setzero_si256()));
    }
    // horizontal sum of 4 x uint64
    uint64_t tmp[4];
    _mm256_storeu_si256((__m256i *)tmp, acc);
    count = tmp[0] + tmp[1] + tmp[2] + tmp[3];
#else
    for (size_t i = 0; i < BM_WORDS_PER_PAGE; i++)
        count += POPCNT64(page[i]);
#endif
    return count;
}

uint64_t bm_popcount(const bm_mem_t *mem) {
    uint64_t total = 0;
    for (size_t p = 0; p < mem->n_pages; p++)
        total += bm_page_popcount(mem, p);
    return total;
}

uint64_t bm_popcount_range(const bm_mem_t *mem, uint64_t lo, uint64_t hi) {
    if (lo >= mem->total_bits || lo > hi) return 0;
    if (hi >= mem->total_bits) hi = mem->total_bits - 1;

    uint64_t count = 0;
    uint64_t w_lo = lo / BM_WORD_BITS;
    uint64_t w_hi = hi / BM_WORD_BITS;

    if (w_lo == w_hi) {
        uint64_t mask = 0;
        for (uint64_t b = lo % BM_WORD_BITS; b <= hi % BM_WORD_BITS; b++)
            mask |= (1ULL << b);
        return POPCNT64(mem->base[w_lo] & mask);
    }

    // partial first word
    uint64_t first_shift = lo % BM_WORD_BITS;
    count += POPCNT64(mem->base[w_lo] >> first_shift);

    // full words
    for (uint64_t w = w_lo + 1; w < w_hi; w++)
        count += POPCNT64(mem->base[w]);

    // partial last word
    uint64_t last_bits = (hi % BM_WORD_BITS) + 1;
    uint64_t last_mask = (last_bits == 64) ? ~0ULL : ((1ULL << last_bits) - 1);
    count += POPCNT64(mem->base[w_hi] & last_mask);

    return count;
}

// ── SIMD Scan ───────────────────────────────────────────────────────
// bit-parallel pattern matching: mask/expect against sliding window

static bm_status_t scan_push(bm_scan_result_t *r, uint64_t pos) {
    if (r->n_hits >= r->capacity) {
        size_t new_cap = r->capacity ? r->capacity * 2 : 256;
        uint64_t *tmp = (uint64_t *)realloc(r->hits, new_cap * sizeof(uint64_t));
        if (!tmp) return BM_ERR_FULL;
        r->hits = tmp;
        r->capacity = new_cap;
    }
    r->hits[r->n_hits++] = pos;
    return BM_OK;
}

bm_status_t bm_scan_range(const bm_mem_t *mem, const bm_scan_op_t *op,
                           uint64_t lo, uint64_t hi,
                           bm_scan_result_t *out) {
    if (!op->mask) return BM_ERR_PATTERN;
    if (lo >= mem->total_bits) return BM_ERR_OOB;
    if (hi > mem->total_bits) hi = mem->total_bits;

    memset(out, 0, sizeof(*out));
    uint32_t stride = op->stride ? op->stride : 1;

    // word-aligned fast path: pattern fits in one word and stride == word width
    if (stride == BM_WORD_BITS && (lo % BM_WORD_BITS == 0)) {
        uint64_t w_lo = lo / BM_WORD_BITS;
        uint64_t w_hi = (hi + BM_WORD_BITS - 1) / BM_WORD_BITS;
        size_t n_words = mem->n_pages * BM_WORDS_PER_PAGE;
        if (w_hi > n_words) w_hi = n_words;

        for (uint64_t w = w_lo; w < w_hi; w++) {
            if ((mem->base[w] & op->mask) == op->expect) {
                bm_status_t s = scan_push(out, w * BM_WORD_BITS);
                if (s != BM_OK) return s;
            }
        }
        return BM_OK;
    }

    // general sliding-window scan
    for (uint64_t pos = lo; pos + BM_WORD_BITS <= hi; pos += stride) {
        uint64_t w_idx = pos / BM_WORD_BITS;
        uint64_t shift = pos % BM_WORD_BITS;
        uint64_t sample;

        if (shift == 0) {
            sample = mem->base[w_idx];
        } else {
            sample = mem->base[w_idx] >> shift;
            if (w_idx + 1 < mem->n_pages * BM_WORDS_PER_PAGE)
                sample |= mem->base[w_idx + 1] << (BM_WORD_BITS - shift);
        }

        if ((sample & op->mask) == op->expect) {
            bm_status_t s = scan_push(out, pos);
            if (s != BM_OK) return s;
        }
    }
    return BM_OK;
}

bm_status_t bm_scan(const bm_mem_t *mem, const bm_scan_op_t *op,
                     bm_scan_result_t *out) {
    return bm_scan_range(mem, op, 0, mem->total_bits, out);
}

void bm_scan_result_free(bm_scan_result_t *r) {
    free(r->hits);
    memset(r, 0, sizeof(*r));
}

// ── Adaptive Rasterization ──────────────────────────────────────────

uint8_t bm_quantum_level(double density) {
    if (density < 0.001) return 0;
    if (density < 0.01)  return 1;
    if (density < 0.05)  return 2;
    if (density < 0.10)  return 3;
    if (density < 0.25)  return 4;
    if (density < 0.50)  return 5;
    if (density < 0.75)  return 6;
    return 7;
}

bm_status_t bm_rasterize(const bm_mem_t *mem, bm_raster_t *out) {
    out->tiles = (bm_tile_t *)malloc(mem->n_pages * sizeof(bm_tile_t));
    if (!out->tiles) return BM_ERR_FULL;
    out->n_tiles = mem->n_pages;

    for (size_t i = 0; i < mem->n_pages; i++) {
        uint64_t pc = bm_page_popcount(mem, i);
        double d = (double)pc / BM_PAGE_BITS;
        out->tiles[i] = (bm_tile_t){
            .page_idx = i,
            .popcount = pc,
            .density  = d,
            .quantum  = bm_quantum_level(d),
        };
    }
    return BM_OK;
}

void bm_raster_free(bm_raster_t *r) {
    free(r->tiles);
    memset(r, 0, sizeof(*r));
}

// ── .mem Hex Format ─────────────────────────────────────────────────
// MIPS-compatible: "0xADDR\t0xVALUE" per line

bm_status_t bm_load_hex(bm_mem_t *mem, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return BM_ERR_MMAP;

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        uint64_t addr, val;
        if (sscanf(line, "0x%llx\t0x%llx", (unsigned long long *)&addr, (unsigned long long *)&val) == 2) {
            // set bits from val at bit position addr
            if (addr + 64 <= mem->total_bits) {
                mem->base[addr / BM_WORD_BITS] = val;
            }
        }
    }
    fclose(f);
    return BM_OK;
}

bm_status_t bm_dump_hex(const bm_mem_t *mem, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return BM_ERR_MMAP;

    size_t n_words = mem->n_pages * BM_WORDS_PER_PAGE;
    for (size_t i = 0; i < n_words; i++) {
        if (mem->base[i] != 0) {
            fprintf(f, "0x%08llx\t0x%016llx\n",
                    (unsigned long long)(i * BM_WORD_BITS),
                    (unsigned long long)mem->base[i]);
        }
    }
    fclose(f);
    return BM_OK;
}

// ── Byte / Blob View ────────────────────────────────────────────────
// the substrate is just bytes; expose them so the same .mem can carry
// executables and other payloads next to the bit planes.

uint8_t *bm_bytes(bm_mem_t *mem) {
    return (uint8_t *)mem->base;
}

size_t bm_size_bytes(const bm_mem_t *mem) {
    return mem->n_pages * BM_PAGE_SIZE;
}

bm_status_t bm_blob_write(bm_mem_t *mem, uint64_t byte_off,
                          const void *data, size_t len) {
    if (!mem->writable) return BM_ERR_OOB;
    if (byte_off + len > bm_size_bytes(mem)) return BM_ERR_OOB;
    memcpy((uint8_t *)mem->base + byte_off, data, len);
    return BM_OK;
}

bm_status_t bm_blob_read(const bm_mem_t *mem, uint64_t byte_off,
                         void *dst, size_t len) {
    if (byte_off + len > bm_size_bytes(mem)) return BM_ERR_OOB;
    memcpy(dst, (const uint8_t *)mem->base + byte_off, len);
    return BM_OK;
}

bm_status_t bm_blob_load(bm_mem_t *mem, uint64_t byte_off,
                         const char *path, size_t *out_len) {
    if (!mem->writable) return BM_ERR_OOB;
    FILE *f = fopen(path, "rb");
    if (!f) return BM_ERR_MMAP;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return BM_ERR_MMAP; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return BM_ERR_MMAP; }
    rewind(f);

    if (byte_off + (size_t)sz > bm_size_bytes(mem)) { fclose(f); return BM_ERR_OOB; }
    size_t got = fread((uint8_t *)mem->base + byte_off, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) return BM_ERR_MMAP;

    if (out_len) *out_len = got;
    return BM_OK;
}

bm_status_t bm_blob_save(const bm_mem_t *mem, uint64_t byte_off,
                         size_t len, const char *path) {
    if (byte_off + len > bm_size_bytes(mem)) return BM_ERR_OOB;
    FILE *f = fopen(path, "wb");
    if (!f) return BM_ERR_MMAP;
    size_t put = fwrite((const uint8_t *)mem->base + byte_off, 1, len, f);
    fclose(f);
    return put == len ? BM_OK : BM_ERR_MMAP;
}

// ── Binary Vector Lanes ─────────────────────────────────────────────
// popcount-based similarity over {0,1} regions of the substrate.

static inline size_t bvec_words(uint64_t dim) {
    return (size_t)((dim + BM_WORD_BITS - 1) / BM_WORD_BITS);
}

static inline uint64_t bvec_tail_mask(uint64_t dim) {
    uint64_t r = dim & (BM_WORD_BITS - 1);
    return r ? ((1ULL << r) - 1) : ~0ULL;
}

// 64-bit window at (bit_off + 64*i), zero-filled past the substrate.
static uint64_t bvec_word(const bm_mem_t *mem, uint64_t bit_off, size_t i) {
    uint64_t pos = bit_off + (uint64_t)i * BM_WORD_BITS;
    if (pos >= mem->total_bits) return 0;
    size_t nw = mem->n_pages * BM_WORDS_PER_PAGE;
    uint64_t w = pos / BM_WORD_BITS, sh = pos % BM_WORD_BITS;
    uint64_t out = mem->base[w] >> sh;
    if (sh && (w + 1) < nw) out |= mem->base[w + 1] << (BM_WORD_BITS - sh);
    return out;
}

#ifdef __AVX2__
static uint64_t and_popcount_avx2(const uint64_t *a, const uint64_t *b, size_t nwords) {
    const __m256i lookup = _mm256_setr_epi8(
        0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4,
        0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4);
    const __m256i mask4 = _mm256_set1_epi8(0x0F);
    __m256i acc = _mm256_setzero_si256();

    size_t blocks = nwords / 4;
    const __m256i *va = (const __m256i *)a;
    const __m256i *vb = (const __m256i *)b;
    for (size_t i = 0; i < blocks; i++) {
        __m256i v  = _mm256_and_si256(_mm256_loadu_si256(&va[i]),
                                      _mm256_loadu_si256(&vb[i]));
        __m256i lo = _mm256_and_si256(v, mask4);
        __m256i hi = _mm256_and_si256(_mm256_srli_epi16(v, 4), mask4);
        __m256i pc = _mm256_add_epi8(_mm256_shuffle_epi8(lookup, lo),
                                     _mm256_shuffle_epi8(lookup, hi));
        acc = _mm256_add_epi64(acc, _mm256_sad_epu8(pc, _mm256_setzero_si256()));
    }
    uint64_t tmp[4];
    _mm256_storeu_si256((__m256i *)tmp, acc);
    uint64_t c = tmp[0] + tmp[1] + tmp[2] + tmp[3];
    for (size_t i = blocks * 4; i < nwords; i++) c += POPCNT64(a[i] & b[i]);
    return c;
}
#endif

uint64_t bm_bvec_popcount(const bm_mem_t *mem, const bm_bvec_t *v) {
    size_t nw = bvec_words(v->dim);
    if (!nw) return 0;
    uint64_t c = 0;
    for (size_t i = 0; i + 1 < nw; i++)
        c += POPCNT64(bvec_word(mem, v->bit_off, i));
    c += POPCNT64(bvec_word(mem, v->bit_off, nw - 1) & bvec_tail_mask(v->dim));
    return c;
}

uint64_t bm_bvec_dot(const bm_mem_t *mem, const bm_bvec_t *a, const bm_bvec_t *b) {
    uint64_t dim = a->dim < b->dim ? a->dim : b->dim;
    if (!dim) return 0;

#ifdef __AVX2__
    if ((a->bit_off % BM_WORD_BITS) == 0 && (b->bit_off % BM_WORD_BITS) == 0 &&
        a->bit_off + dim <= mem->total_bits &&
        b->bit_off + dim <= mem->total_bits) {
        const uint64_t *pa = mem->base + a->bit_off / BM_WORD_BITS;
        const uint64_t *pb = mem->base + b->bit_off / BM_WORD_BITS;
        size_t full = (size_t)(dim / BM_WORD_BITS);
        uint64_t c = and_popcount_avx2(pa, pb, full);
        uint64_t r = dim & (BM_WORD_BITS - 1);
        if (r) c += POPCNT64(pa[full] & pb[full] & ((1ULL << r) - 1));
        return c;
    }
#endif

    size_t nw = bvec_words(dim);
    uint64_t c = 0;
    for (size_t i = 0; i + 1 < nw; i++)
        c += POPCNT64(bvec_word(mem, a->bit_off, i) & bvec_word(mem, b->bit_off, i));
    c += POPCNT64(bvec_word(mem, a->bit_off, nw - 1) &
                  bvec_word(mem, b->bit_off, nw - 1) & bvec_tail_mask(dim));
    return c;
}

uint64_t bm_bvec_hamming(const bm_mem_t *mem, const bm_bvec_t *a, const bm_bvec_t *b) {
    uint64_t pa = bm_bvec_popcount(mem, a);
    uint64_t pb = bm_bvec_popcount(mem, b);
    uint64_t d  = bm_bvec_dot(mem, a, b);
    return pa + pb - 2 * d;
}

double bm_bvec_cosine(const bm_mem_t *mem, const bm_bvec_t *a, const bm_bvec_t *b) {
    uint64_t pa = bm_bvec_popcount(mem, a);
    uint64_t pb = bm_bvec_popcount(mem, b);
    if (!pa || !pb) return 0.0;
    uint64_t d = bm_bvec_dot(mem, a, b);
    return (double)d / sqrt((double)pa * (double)pb);
}

double bm_bvec_jaccard(const bm_mem_t *mem, const bm_bvec_t *a, const bm_bvec_t *b) {
    uint64_t d  = bm_bvec_dot(mem, a, b);
    uint64_t pa = bm_bvec_popcount(mem, a);
    uint64_t pb = bm_bvec_popcount(mem, b);
    uint64_t u  = pa + pb - d;
    return u ? (double)d / (double)u : 0.0;
}

// ── Multipurpose Segment Directory ──────────────────────────────────
// page 0 = { header, segment table }; segments start at page 1.

#define BM_DIR_MAGIC 0x31564D42u  // 'BMV1'

typedef struct {
    uint32_t magic;
    uint32_t count;
    uint64_t bump;    // next free byte offset for a new segment
} bm_dir_hdr_t;

#define BM_DIR_MAX ((BM_PAGE_SIZE - sizeof(bm_dir_hdr_t)) / sizeof(bm_seg_t))

static bm_dir_hdr_t *dir_hdr(const bm_mem_t *mem) {
    if (mem->n_pages < 1 || !mem->base) return NULL;
    bm_dir_hdr_t *h = (bm_dir_hdr_t *)mem->base;
    return h->magic == BM_DIR_MAGIC ? h : NULL;
}

static bm_seg_t *dir_segs(const bm_mem_t *mem) {
    return (bm_seg_t *)((uint8_t *)mem->base + sizeof(bm_dir_hdr_t));
}

bm_status_t bm_dir_init(bm_mem_t *mem) {
    if (!mem->writable || mem->n_pages < 1) return BM_ERR_OOB;
    bm_dir_hdr_t *h = (bm_dir_hdr_t *)mem->base;
    h->magic = BM_DIR_MAGIC;
    h->count = 0;
    h->bump  = BM_PAGE_SIZE;   // segments live past the directory page
    return BM_OK;
}

size_t bm_dir_count(const bm_mem_t *mem) {
    bm_dir_hdr_t *h = dir_hdr(mem);
    return h ? h->count : 0;
}

const bm_seg_t *bm_dir_get(const bm_mem_t *mem, size_t i) {
    bm_dir_hdr_t *h = dir_hdr(mem);
    if (!h || i >= h->count) return NULL;
    return &dir_segs(mem)[i];
}

const bm_seg_t *bm_dir_find(const bm_mem_t *mem, const char *name) {
    bm_dir_hdr_t *h = dir_hdr(mem);
    if (!h || !name) return NULL;
    bm_seg_t *segs = dir_segs(mem);
    for (uint32_t i = 0; i < h->count; i++)
        if (strncmp(segs[i].name, name, sizeof(segs[i].name)) == 0)
            return &segs[i];
    return NULL;
}

bm_status_t bm_seg_add(bm_mem_t *mem, const char *name, bm_seg_kind_t kind,
                       uint32_t elem_bits, uint64_t byte_len, uint64_t *out_off) {
    if (!mem->writable) return BM_ERR_OOB;
    bm_dir_hdr_t *h = dir_hdr(mem);
    if (!h) return BM_ERR_PATTERN;          // directory not initialized
    if (h->count >= BM_DIR_MAX) return BM_ERR_FULL;

    uint64_t off = (h->bump + BM_PAGE_SIZE - 1) & ~((uint64_t)BM_PAGE_SIZE - 1);
    if (off + byte_len > bm_size_bytes(mem)) return BM_ERR_OOB;

    bm_seg_t *s = &dir_segs(mem)[h->count];
    memset(s, 0, sizeof(*s));
    s->kind      = (uint32_t)kind;
    s->elem_bits = elem_bits;
    s->byte_off  = off;
    s->byte_len  = byte_len;
    if (name) {
        size_t k = 0;
        for (; k < sizeof(s->name) - 1 && name[k]; k++) s->name[k] = name[k];
        s->name[k] = '\0';
    }

    h->bump = off + byte_len;
    h->count++;
    if (out_off) *out_off = off;
    return BM_OK;
}
