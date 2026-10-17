#include "../../Sources/COSDIPrecision/Arena/Arena.h"
#include "../../Sources/COSDIPrecision/Arithmetic/Arithmetic.h"
#include "../../Sources/COSDIPrecision/ProgramEvaluation/Identity.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
static int reference(unsigned op, mpfr_ptr d, mpfr_srcptr a, mpfr_srcptr b,
                     mpfr_rnd_t r) {
  switch (op) {
#define U(code, fn)                                                            \
  case code:                                                                   \
    return fn(d, a, r)
    U(13, mpfr_sqrt);
    U(14, mpfr_exp);
    U(15, mpfr_expm1);
    U(16, mpfr_log);
    U(17, mpfr_log1p);
    U(18, mpfr_log10);
    U(22, mpfr_sin);
    U(23, mpfr_cos);
    U(24, mpfr_tan);
    U(27, mpfr_atan);
    U(28, mpfr_sinh);
    U(29, mpfr_cosh);
    U(30, mpfr_tanh);
    U(31, mpfr_asinh);
#undef U
  case 20:
    return mpfr_floor(d, a);
  case 21:
    return mpfr_ceil(d, a);
  case 44:
    return mpfr_add(d, a, b, r);
  case 45:
    return mpfr_sub(d, a, b, r);
  case 46:
    return mpfr_mul(d, a, b, r);
  case 47:
    return mpfr_div(d, a, b, r);
  case 67:
    return mpfr_pow(d, a, b, r);
  }
  abort();
}
static void interval(cs_precision_interval *v, double lo, double hi) {
  mpfr_set_d(v->low, lo, MPFR_RNDD);
  mpfr_set_d(v->high, hi, MPFR_RNDU);
}
int main(void) {
  cs_precision_arena arena;
  assert(cs_precision_arena_open(&arena, 8u << 20));
  cs_precision_math math;
  cs_precision_math_init(&math, 256, UINT64_C(100000000));
  cs_precision_value a = {.type = 1, .initialized = 1},
                     b = {.type = 1, .initialized = 1}, out = {.type = 1};
  size_t n = mpfr_custom_get_size(256);
  cs_precision_interval_init(&a.real, 256, cs_precision_arena_allocate(n * 2));
  cs_precision_interval_init(&b.real, 256, cs_precision_arena_allocate(n * 2));
  cs_precision_interval_init(&out.real, 256,
                             cs_precision_arena_allocate(n * 2));
  mpfr_t x, y, value, upper, step;
  mpfr_inits2(512, x, y, value, upper, step, (mpfr_ptr)0);
  const unsigned ops[] = {13, 14, 15, 16, 17, 18, 20, 21, 22, 23, 24,
                          27, 28, 29, 30, 31, 44, 45, 46, 47, 67};
  unsigned checks = 0;
  for (unsigned k = 0; k < sizeof(ops) / sizeof(ops[0]); k++) {
    unsigned op = ops[k];
    double lo = -1, hi = 1;
    if (op == 13 || op == 16 || op == 18 || op == 67) {
      lo = .125;
      hi = 2;
    }
    if (op == 17)
      lo = -.5;
    if (op == 22 || op == 23) {
      lo = -10;
      hi = 10;
    }
    if (op == 29) {
      lo = -3;
      hi = 2;
    }
    interval(&a.real, lo, hi);
    interval(&b.real, .5, 2);
    assert(cs_precision_apply(&math, op, &out, &a, &b) == 0);
    for (unsigned i = 0; i <= 32; i++)
      for (unsigned j = 0; j <= 8; j++) {
        mpfr_set_d(x, hi - lo, MPFR_RNDN);
        mpfr_mul_ui(x, x, i, MPFR_RNDN);
        mpfr_div_ui(x, x, 32, MPFR_RNDN);
        mpfr_add_d(x, x, lo, MPFR_RNDN);
        mpfr_set_ui(y, j, MPFR_RNDN);
        mpfr_mul_d(y, y, 1.5, MPFR_RNDN);
        mpfr_div_ui(y, y, 8, MPFR_RNDN);
        mpfr_add_d(y, y, .5, MPFR_RNDN);
        reference(op, value, x, y, MPFR_RNDD);
        reference(op, upper, x, y, MPFR_RNDU);
        if (mpfr_cmp(out.real.low, value) > 0 ||
            mpfr_cmp(out.real.high, upper) < 0) {
          fprintf(stderr, "Containment failure opcode %u sample %u/%u\n", op, i,
                  j);
          abort();
        }
        checks++;
      }
  }
  interval(&a.real, 1, 2);
  assert(cs_precision_apply(&math, 24, &out, &a, NULL) ==
         CS_OSDI_PRECISION_DOMAIN);
  interval(&a.real, -1, 1);
  assert(cs_precision_apply(&math, 16, &out, &a, NULL) ==
         CS_OSDI_PRECISION_DOMAIN);
  interval(&b.real, -1, 1);
  assert(cs_precision_apply(&math, 47, &out, &a, &b) ==
         CS_OSDI_PRECISION_DOMAIN);
  interval(&a.real, -2, 3);
  interval(&b.real, 2, 2);
  assert(cs_precision_apply(&math, 67, &out, &a, &b) == 0);
  assert(mpfr_zero_p(out.real.low) && mpfr_cmp_ui(out.real.high, 9) == 0);
  interval(&b.real, 3, 3);
  assert(cs_precision_apply(&math, 67, &out, &a, &b) == 0);
  assert(mpfr_cmp_si(out.real.low, -8) == 0 &&
         mpfr_cmp_ui(out.real.high, 27) == 0);
  interval(&a.real, 0, 2);
  interval(&b.real, .5, 2);
  assert(cs_precision_apply(&math, 67, &out, &a, &b) == 0);
  assert(mpfr_zero_p(out.real.low) && mpfr_cmp_ui(out.real.high, 4) == 0);
  interval(&a.real, -1, 1);
  cs_precision_value predicate = {.type = 3};
  assert(cs_precision_apply(&math, 58, &predicate, &a, &a) == 0 &&
         predicate.integer == 1);
  cs_precision_value integer = {.type = 2};
  interval(&a.real, -1.5, -1.5);
  assert(cs_precision_apply(&math, 5, &integer, &a, NULL) == 0 &&
         integer.integer == (uint32_t)-2);
  assert(cs_precision_apply(&math, 6, &integer, &a, NULL) == 0 &&
         integer.integer == (uint32_t)-1);
  cs_precision_value ia = {.type = 2, .initialized = 1, .integer = UINT32_MAX},
                     ib = {.type = 2, .initialized = 1, .integer = 1};
  assert(cs_precision_apply(&math, 40, &integer, &ia, &ib) == 0 &&
         integer.integer == UINT32_C(0x7fffffff));
  assert(cs_precision_apply(&math, 34, &integer, &ia, &ib) == 0 &&
         integer.integer == 0);
  ia.integer = 0;
  assert(cs_precision_apply(&math, 19, &integer, &ia, NULL) ==
         CS_OSDI_PRECISION_DOMAIN);
  /* A rounded low component can reach a midpoint that changes an odd high. */
  mpfr_set_d(a.real.low, 0x1.0000000000001p0, MPFR_RNDN);
  mpfr_add_d(a.real.low, a.real.low, 0x1p-53, MPFR_RNDN);
  mpfr_sub_d(a.real.low, a.real.low, 0x1p-160, MPFR_RNDN);
  mpfr_set(a.real.high, a.real.low, MPFR_RNDN);
  double high, low, radius;
  assert(cs_precision_project(&math, &a.real, &high, &low, &radius) == 0);
  assert(high + low == high);
  mpfr_set_d(x, high, MPFR_RNDN);
  mpfr_add_d(x, x, low, MPFR_RNDN);
  mpfr_set_d(y, radius, MPFR_RNDN);
  mpfr_sub(value, x, y, MPFR_RNDD);
  mpfr_add(upper, x, y, MPFR_RNDU);
  assert(mpfr_cmp(value, a.real.low) <= 0 && mpfr_cmp(upper, a.real.high) >= 0);
  cs_precision_identity identities;
  cs_precision_identity_open(&identities, 8, 256);
  assert(cs_precision_identity_reset(&identities, &math) == 0);
  cs_precision_value leaf = {.type = 2, .initialized = 1, .integer = 0};
  assert(cs_precision_identify(&identities, &math, &leaf, 0, 0, 0) == 0);
  uint32_t zero_id = leaf.identity;
  leaf.integer = 128;
  assert(cs_precision_identify(&identities, &math, &leaf, 0, 0, 0) == 0);
  assert(leaf.identity != zero_id);
  /* These integer keys have identical low seven FNV hash bits. */
  unsigned occupied = 0, consecutive = 0;
  for (uint32_t j = 0; j < identities.slot_count; j++) {
    if (identities.slots[j]) {
      occupied++;
      if (identities.slots[(j + 1) & (identities.slot_count - 1)])
        consecutive++;
    }
  }
  assert(identities.slot_count == 128 && occupied == 2 && consecutive == 1);
  leaf.integer = 0;
  assert(cs_precision_identify(&identities, &math, &leaf, 0, 0, 0) == 0 &&
         leaf.identity == zero_id);
  math.limit = math.work;
  assert(cs_precision_identify(&identities, &math, &leaf, 0, 0, 0) ==
         CS_OSDI_PRECISION_WORK_EXHAUSTED);
  pid_t child = fork();
  assert(child >= 0);
  if (child == 0) {
    math.limit = UINT64_C(100000000);
    math.status = 0;
    assert(cs_precision_identity_reset(&identities, &math) == 0);
    for (uint32_t j = 0; j <= identities.capacity; j++) {
      leaf.integer = j;
      assert(cs_precision_identify(&identities, &math, &leaf, 0, 0, 0) == 0);
    }
    _exit(99);
  }
  int child_status;
  assert(waitpid(child, &child_status, 0) == child && WIFEXITED(child_status) &&
         WEXITSTATUS(child_status) == CS_OSDI_PRECISION_EXIT_ARENA);
  mpfr_clears(x, y, value, upper, step, (mpfr_ptr)0);
  mpfr_free_cache();
  printf("{\"intervalSamples\":%u,\"peakRequestedArenaBytes\":%llu,"
         "\"allocationRequests\":%llu,\"reallocationRequests\":%llu,"
         "\"copiedBytes\":%llu}\n",
         checks, (unsigned long long)arena.peak_bytes,
         (unsigned long long)arena.allocations,
         (unsigned long long)arena.reallocations,
         (unsigned long long)arena.copied_bytes);
  cs_precision_arena_close(&arena);
  return 0;
}
