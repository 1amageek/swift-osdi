#include "Arena.h"
#include <OSDIPrecisionProtocol.h>
#include <gmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
struct cs_precision_block {
  size_t capacity, requested;
  struct cs_precision_block *previous, *next;
  uint64_t marker;
  max_align_t alignment;
};
static cs_precision_arena *owner;
#define BLOCK_MARK UINT64_C(0x435350524543424c)
static void invariant(void) { _exit(CS_OSDI_PRECISION_EXIT_INVARIANT); }
static size_t aligned(size_t n) {
  size_t a = _Alignof(max_align_t);
  if (n > SIZE_MAX - a + 1)
    _exit(CS_OSDI_PRECISION_EXIT_ARENA);
  return (n + a - 1) / a * a;
}
static void count(uint64_t *value, uint64_t add) {
  if (*value > UINT64_MAX - add)
    invariant();
  *value += add;
}
static void merge(cs_precision_block *b);
static void split(cs_precision_block *b, size_t n) {
  if (b->capacity - n < sizeof(*b) + _Alignof(max_align_t))
    return;
  cs_precision_block *next = (void *)((unsigned char *)(b + 1) + n);
  *next = (cs_precision_block){.capacity = b->capacity - n - sizeof(*b),
                               .previous = b,
                               .next = b->next,
                               .marker = BLOCK_MARK};
  if (next->next)
    next->next->previous = next;
  b->next = next;
  b->capacity = n;
  merge(next);
}
void *cs_precision_arena_allocate(size_t bytes) {
  if (!owner)
    invariant();
  size_t n = aligned(bytes ? bytes : 1);
  for (cs_precision_block *b = owner->first; b; b = b->next) {
    if (b->marker != BLOCK_MARK)
      invariant();
    if (!b->requested && b->capacity >= n) {
      split(b, n);
      b->requested = bytes ? bytes : 1;
      count(&owner->allocations, 1);
      count(&owner->live_bytes, b->requested);
      if (owner->live_bytes > owner->peak_bytes)
        owner->peak_bytes = owner->live_bytes;
      return b + 1;
    }
  }
  _exit(CS_OSDI_PRECISION_EXIT_ARENA);
}
static cs_precision_block *checked(void *p, size_t n) {
  uintptr_t base = (uintptr_t)owner->bytes, value = (uintptr_t)p;
  if (value < base + sizeof(cs_precision_block) ||
      value >= base + owner->capacity || (value - base) % _Alignof(max_align_t))
    invariant();
  cs_precision_block *b = (cs_precision_block *)p - 1;
  if (b->marker != BLOCK_MARK || !b->requested || b->requested != (n ? n : 1))
    invariant();
  return b;
}
static void merge(cs_precision_block *b) {
  if (b->next && !b->next->requested) {
    cs_precision_block *next = b->next;
    if (next->marker != BLOCK_MARK)
      invariant();
    b->capacity += sizeof(*b) + next->capacity;
    b->next = next->next;
    if (b->next)
      b->next->previous = b;
    next->marker = 0;
  }
}
static void release(void *p, size_t n) {
  cs_precision_block *b = checked(p, n);
  if (owner->live_bytes < b->requested)
    invariant();
  owner->live_bytes -= b->requested;
  b->requested = 0;
  count(&owner->frees, 1);
  merge(b);
  if (b->previous && !b->previous->requested)
    merge(b->previous);
}
static void *resize(void *p, size_t old, size_t bytes) {
  cs_precision_block *b = checked(p, old);
  size_t n = aligned(bytes ? bytes : 1);
  count(&owner->reallocations, 1);
  if (b->next && !b->next->requested &&
      b->capacity + sizeof(*b) + b->next->capacity >= n)
    merge(b);
  if (b->capacity >= n) {
    owner->live_bytes -= b->requested;
    b->requested = bytes ? bytes : 1;
    count(&owner->live_bytes, b->requested);
    split(b, n);
    if (owner->live_bytes > owner->peak_bytes)
      owner->peak_bytes = owner->live_bytes;
    return p;
  }
  void *q = cs_precision_arena_allocate(bytes);
  size_t copy = old < bytes ? old : bytes;
  memcpy(q, p, copy);
  count(&owner->copied_bytes, copy);
  release(p, old);
  return q;
}
int cs_precision_arena_open(cs_precision_arena *arena, uint64_t bytes) {
  if (owner || bytes > SIZE_MAX ||
      bytes < sizeof(cs_precision_block) + _Alignof(max_align_t))
    return 0;
  memset(arena, 0, sizeof(*arena));
  arena->bytes = malloc((size_t)bytes);
  if (!arena->bytes)
    return 0;
  arena->capacity = (size_t)bytes;
  /* Touch every reserved byte before publication, rather than claiming lazy VM
   * as resident. */
  memset(arena->bytes, 0, arena->capacity);
  long page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0) {
    free(arena->bytes);
    arena->bytes = NULL;
    return 0;
  }
  volatile unsigned char *touched = arena->bytes;
  for (size_t offset = 0; offset < arena->capacity; offset += (size_t)page_size)
    touched[offset] = 0;
  touched[arena->capacity - 1] = 0;
  arena->first = (void *)arena->bytes;
  *arena->first = (cs_precision_block){.capacity = arena->capacity -
                                                   sizeof(cs_precision_block),
                                       .marker = BLOCK_MARK};
  owner = arena;
  mp_set_memory_functions(cs_precision_arena_allocate, resize, release);
  return 1;
}
void cs_precision_arena_close(cs_precision_arena *arena) {
  if (owner != arena)
    invariant();
  free(arena->bytes);
  arena->bytes = NULL;
  owner = NULL;
  /* No GMP/MPFR operation is legal after this process-owned arena is closed. */
}
