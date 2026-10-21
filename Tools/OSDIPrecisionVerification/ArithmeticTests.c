#include "../../Sources/OSDIPrecisionWorker/Arena/Arena.h"
#include "../../Sources/OSDIPrecisionWorker/Arithmetic/Arithmetic.h"
#include "../../Sources/OSDIPrecisionWorker/ProgramEvaluation/Identity.h"
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
static int same_endpoint(mpfr_srcptr actual, mpfr_srcptr expected) {
  if (mpfr_nan_p(actual) || mpfr_nan_p(expected))
    return mpfr_nan_p(actual) && mpfr_nan_p(expected);
  return mpfr_equal_p(actual, expected) &&
         (!mpfr_zero_p(actual) ||
          mpfr_signbit(actual) == mpfr_signbit(expected));
}
static int reference_binary(unsigned op, mpfr_prec_t precision,
                            cs_precision_interval *out,
                            const cs_precision_interval *a,
                            const cs_precision_interval *b) {
  if (op == 47 && mpfr_cmp_ui(b->low, 0) <= 0 &&
      mpfr_cmp_ui(b->high, 0) >= 0)
    return CS_OSDI_PRECISION_DOMAIN;
  mpfr_t down[4], up[4];
  mpfr_srcptr av[] = {a->low, a->high}, bv[] = {b->low, b->high};
  for (unsigned i = 0; i < 4; i++) {
    mpfr_init2(down[i], precision);
    mpfr_init2(up[i], precision);
    reference(op, down[i], av[i / 2], bv[i % 2], MPFR_RNDD);
    reference(op, up[i], av[i / 2], bv[i % 2], MPFR_RNDU);
  }
  unsigned minimum = 0, maximum = 0;
  for (unsigned i = 1; i < 4; i++) {
    if (mpfr_cmp(down[i], down[minimum]) < 0)
      minimum = i;
    if (mpfr_cmp(up[i], up[maximum]) > 0)
      maximum = i;
  }
  mpfr_set(out->low, down[minimum], MPFR_RNDD);
  mpfr_set(out->high, up[maximum], MPFR_RNDU);
  for (unsigned i = 0; i < 4; i++) {
    mpfr_clear(down[i]);
    mpfr_clear(up[i]);
  }
  return mpfr_nan_p(out->low) || mpfr_nan_p(out->high) ||
                 mpfr_cmp(out->low, out->high) > 0
             ? CS_OSDI_PRECISION_DOMAIN
             : 0;
}
static void binary_fixture(cs_precision_interval *value, unsigned index) {
  static const char *bounds[][2] = {
      {"0", "0"}, {"-0", "-0"}, {"-0", "0"}, {"0", "-0"},
      {"0", "1"}, {"-1", "0"}, {"-1", "1"}, {"1", "2"},
      {"0.1", "0.3"},
      {"1.00000000000000000000000000000000000000000000000000001",
       "1.00000000000000000000000000000000000000000000000000009"},
      {"-3", "-2"}, {"-3", "2"}, {"2", "1"},
      {"-inf", "-1"}, {"1", "inf"}, {"-inf", "inf"},
      {"inf", "inf"}, {"-inf", "-inf"}, {"nan", "nan"},
      {"nan", "1"}, {"1", "nan"}};
  if (index < sizeof(bounds) / sizeof(bounds[0])) {
    assert(mpfr_set_str(value->low, bounds[index][0], 10, MPFR_RNDD) == 0);
    assert(mpfr_set_str(value->high, bounds[index][1], 10, MPFR_RNDU) == 0);
  } else {
    mpfr_exp_t exponent = index == 21 ? mpfr_get_emin() / 2 - 2
                                     : mpfr_get_emax() / 2 + 2;
    mpfr_set_ui_2exp(value->low, 1, exponent, MPFR_RNDN);
    mpfr_set_ui_2exp(value->high, 3, exponent, MPFR_RNDN);
  }
}
static unsigned binary_differential(void) {
  const uint32_t precisions[] = {53, 80, 128, 256, 512};
  unsigned checks = 0;
  for (unsigned p = 0; p < 2 * sizeof(precisions) / sizeof(precisions[0]); p++) {
    uint32_t precision = precisions[p / 2];
    cs_precision_math math;
    cs_precision_math_init(&math, precision, 65);
    cs_precision_value a = {.type = 1, .initialized = 1},
                       b = {.type = 1, .initialized = 1}, out = {.type = 1};
    mpfr_init2(a.real.low, precision + (p % 2 ? 13 : 0));
    mpfr_init2(a.real.high, precision + (p % 2 ? 13 : 0));
    mpfr_init2(b.real.low, precision + (p % 2 ? 29 : 0));
    mpfr_init2(b.real.high, precision + (p % 2 ? 29 : 0));
    mpfr_init2(out.real.low, precision - (p % 2 ? 7 : 0));
    mpfr_init2(out.real.high, precision - (p % 2 ? 7 : 0));
    cs_precision_interval expected;
    mpfr_init2(expected.low, precision);
    mpfr_init2(expected.high, precision);
    for (unsigned ai = 0; ai < 23; ai++)
      for (unsigned bi = 0; bi < 23; bi++)
        for (unsigned op = 46; op <= 47; op++)
          for (unsigned alias = 0; alias < 3; alias++) {
            binary_fixture(&a.real, ai);
            binary_fixture(&b.real, bi);
            interval(&out.real, -7, 11);
            out.initialized = 0;
            cs_precision_value *destination =
                alias == 1 ? &a : alias == 2 ? &b : &out;
            mpfr_set_prec(expected.low, mpfr_get_prec(destination->real.low));
            mpfr_set_prec(expected.high, mpfr_get_prec(destination->real.high));
            cs_precision_interval_copy(&expected, &destination->real);
            int expected_status = reference_binary(op, precision, &expected,
                                                   &a.real, &b.real);
            math.work = 0;
            math.status = 0;
            unsigned initialized = destination->initialized;
            int status = cs_precision_apply(&math, op, destination, &a, &b);
            if (status != expected_status ||
                !same_endpoint(destination->real.low, expected.low) ||
                !same_endpoint(destination->real.high, expected.high)) {
              fprintf(stderr,
                      "Corner differential failure p=%u op=%u a=%u b=%u "
                      "alias=%u status=%d expected=%d\n",
                      precision, op, ai, bi, alias, status, expected_status);
              abort();
            }
            assert(math.work == 65 && math.status == 0);
            assert(destination->initialized == (status ? initialized : 1));
            cs_precision_interval_copy(&expected, &destination->real);
            initialized = destination->initialized;
            assert(cs_precision_apply(&math, op, destination, &a, &b) ==
                   CS_OSDI_PRECISION_WORK_EXHAUSTED);
            assert(math.work == 65 &&
                   math.status == CS_OSDI_PRECISION_WORK_EXHAUSTED &&
                   destination->initialized == initialized &&
                   same_endpoint(destination->real.low, expected.low) &&
                   same_endpoint(destination->real.high, expected.high));
            checks++;
          }
    mpfr_clears(a.real.low, a.real.high, b.real.low, b.real.high, out.real.low,
                out.real.high, expected.low, expected.high, (mpfr_ptr)0);
  }
  return checks;
}
int main(void) {
  cs_precision_arena arena;
  assert(cs_precision_arena_open(&arena, 8u << 20));
  unsigned corner_checks = binary_differential();
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
  printf("{\"intervalSamples\":%u,\"intervalCornerComparisons\":%u,"
         "\"peakRequestedArenaBytes\":%llu,"
         "\"allocationRequests\":%llu,\"reallocationRequests\":%llu,"
         "\"copiedBytes\":%llu}\n",
         checks, corner_checks, (unsigned long long)arena.peak_bytes,
         (unsigned long long)arena.allocations,
         (unsigned long long)arena.reallocations,
         (unsigned long long)arena.copied_bytes);
  cs_precision_arena_close(&arena);
  return 0;
}
