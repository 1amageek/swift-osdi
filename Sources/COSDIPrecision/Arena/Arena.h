#ifndef CS_PRECISION_ARENA_H
#define CS_PRECISION_ARENA_H
#include <stddef.h>
#include <stdint.h>
typedef struct cs_precision_block cs_precision_block;
typedef struct {
  unsigned char *bytes;
  size_t capacity;
  cs_precision_block *first;
  uint64_t allocations, reallocations, frees, copied_bytes, live_bytes,
      peak_bytes;
} cs_precision_arena;
/* The sole GMP user in this isolated process installs one arena before MPFR. */
int cs_precision_arena_open(cs_precision_arena *arena, uint64_t bytes);
void *cs_precision_arena_allocate(size_t bytes);
void cs_precision_arena_close(cs_precision_arena *arena);
#endif
