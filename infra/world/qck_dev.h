#ifndef QCK_DEV_H
#define QCK_DEV_H

#include <stdint.h>
#include <stddef.h>

// qck.dev  =  Ω + P + R + V
//
// Ω  address space — every addressable object/relation/value gets an ID
// P  bitplanes     — Pₙ(Ω)   1-bit predicates over Ω
// R  bitrelations  — Rₙ(Ω²)  bitmap adjacency matrices over Ω
// V  bit-values    — Vₙ(Ω)   multi-bit indexed values over Ω
//
// composition of P/R/V defines a world; Δ(current, desired) drives materialization

typedef uint64_t qck_id_t;

typedef enum {
    QCK_OK = 0,
    QCK_ERR_ALLOC,
    QCK_ERR_OOB,
    QCK_ERR_NOT_FOUND,
    QCK_ERR_FULL,
} qck_status_t;

// ── Ω: address space ────────────────────────────────────────────────
// monotonic ID allocator; IDs are stable (no reuse)

typedef struct {
    qck_id_t next;
    qck_id_t cap;   // soft ceiling; planes size to this
} qck_omega_t;

void     omega_init (qck_omega_t *o, qck_id_t initial_cap);
qck_id_t omega_alloc(qck_omega_t *o);
uint64_t omega_size (const qck_omega_t *o);

// include world.h for the full Ω+P+R+V API
#endif
