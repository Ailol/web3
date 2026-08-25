#ifndef BITMEMORY_BACKEND_H
#define BITMEMORY_BACKEND_H

#include "bitmap_backend.h"

bmp_backend_t *bitmemory_backend_create(size_t n_pages);
bmp_backend_t *bitmemory_backend_open(const char *path, int writable);

#endif
