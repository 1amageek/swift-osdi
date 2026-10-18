#include "Arithmetic.h"
#include "../Arena/Arena.h"
#include <limits.h>
#include <math.h>
#include <string.h>
int cs_precision_charge(cs_precision_math *m, uint64_t units) {
  if (units > m->limit - m->work) {
    m->status = CS_OSDI_PRECISION_WORK_EXHAUSTED;
    return 0;
  }
  m->work += units;
  return 1;
}
void cs_precision_interval_init(cs_precision_interval *v, mpfr_prec_t p,
                                void *limbs) {
  size_t n = mpfr_custom_get_size(p);
  mpfr_custom_init_set(v->low, MPFR_NAN_KIND, 0, p, limbs);
  mpfr_custom_init_set(v->high, MPFR_NAN_KIND, 0, p,
                       (unsigned char *)limbs + n);
}
void cs_precision_math_init(cs_precision_math *m, uint32_t p, uint64_t limit) {
  memset(m, 0, sizeof(*m));
  m->precision = p;
  m->limit = limit;
  /* Materialize platform MPFR TLS during cold preparation, before ready
   * publication. */
  mpfr_clear_flags();
  size_t n = mpfr_custom_get_size(p);
  unsigned char *limbs = cs_precision_arena_allocate(n * 28);
  for (unsigned i = 0; i < 8; i++) {
    cs_precision_interval_init(&m->scratch[i], p, limbs);
    limbs += 2 * n;
  }
  for (unsigned i = 0; i < 12; i++) {
    mpfr_custom_init_set(m->temporary[i], MPFR_NAN_KIND, 0, p, limbs);
    limbs += n;
  }
}
int cs_precision_real_bits(cs_precision_interval *v, uint64_t bits) {
  double d;
  memcpy(&d, &bits, 8);
  if (isnan(d))
    return CS_OSDI_PRECISION_DOMAIN;
  mpfr_set_d(v->low, d, MPFR_RNDD);
  mpfr_set_d(v->high, d, MPFR_RNDU);
  return 0;
}
void cs_precision_interval_copy(cs_precision_interval *d,
                                const cs_precision_interval *a) {
  mpfr_set(d->low, a->low, MPFR_RNDD);
  mpfr_set(d->high, a->high, MPFR_RNDU);
}
static int valid(const cs_precision_interval *v) {
  return !mpfr_nan_p(v->low) && !mpfr_nan_p(v->high) &&
         mpfr_cmp(v->low, v->high) <= 0;
}
static int zero(const cs_precision_interval *a) {
  return mpfr_cmp_ui(a->low, 0) <= 0 && mpfr_cmp_ui(a->high, 0) >= 0;
}
static int singleton(const cs_precision_interval *a) {
  return mpfr_equal_p(a->low, a->high);
}
static void binary(cs_precision_math *m, unsigned op, cs_precision_interval *d,
                   const cs_precision_interval *a,
                   const cs_precision_interval *b) {
  mpfr_ptr lo = m->temporary[0], hi = m->temporary[1], t = m->temporary[2];
  if (op == 44) {
    mpfr_add(lo, a->low, b->low, MPFR_RNDD);
    mpfr_add(hi, a->high, b->high, MPFR_RNDU);
  } else if (op == 45) {
    mpfr_sub(lo, a->low, b->high, MPFR_RNDD);
    mpfr_sub(hi, a->high, b->low, MPFR_RNDU);
  } else if (mpfr_number_p(a->low) && mpfr_number_p(a->high) &&
             mpfr_number_p(b->low) && mpfr_number_p(b->high) &&
             mpfr_sgn(a->low) > 0 && mpfr_sgn(b->low) > 0 &&
             mpfr_cmp(a->low, a->high) <= 0 &&
             mpfr_cmp(b->low, b->high) <= 0) {
    /* Positive ordered intervals have monotone extrema; retain temporary-first
       writes so either operand may also own the destination. */
    if (op == 46) {
      mpfr_mul(lo, a->low, b->low, MPFR_RNDD);
      mpfr_mul(hi, a->high, b->high, MPFR_RNDU);
    } else {
      mpfr_div(lo, a->low, b->high, MPFR_RNDD);
      mpfr_div(hi, a->high, b->low, MPFR_RNDU);
    }
  } else {
    mpfr_srcptr av[2] = {a->low, a->high}, bv[2] = {b->low, b->high};
    for (unsigned i = 0; i < 2; i++)
      for (unsigned j = 0; j < 2; j++) {
        if (op == 46)
          mpfr_mul(t, av[i], bv[j], MPFR_RNDD);
        else
          mpfr_div(t, av[i], bv[j], MPFR_RNDD);
        if ((!i && !j) || mpfr_cmp(t, lo) < 0)
          mpfr_set(lo, t, MPFR_RNDD);
        if (op == 46)
          mpfr_mul(t, av[i], bv[j], MPFR_RNDU);
        else
          mpfr_div(t, av[i], bv[j], MPFR_RNDU);
        if ((!i && !j) || mpfr_cmp(t, hi) > 0)
          mpfr_set(hi, t, MPFR_RNDU);
      }
  }
  mpfr_set(d->low, lo, MPFR_RNDD);
  mpfr_set(d->high, hi, MPFR_RNDU);
}
static int trig(cs_precision_math *m, unsigned op, cs_precision_interval *d,
                const cs_precision_interval *a) {
  if (!mpfr_number_p(a->low) || !mpfr_number_p(a->high))
    return CS_OSDI_PRECISION_DOMAIN;
  cs_precision_interval *pi = &m->scratch[0], *offset = &m->scratch[1],
                        *t = &m->scratch[2];
  mpfr_const_pi(pi->low, MPFR_RNDD);
  mpfr_const_pi(pi->high, MPFR_RNDU);
  if (op == 23) {
    mpfr_set_zero(offset->low, 0);
    mpfr_set_zero(offset->high, 0);
  } else {
    mpfr_div_2ui(offset->low, pi->low, 1, MPFR_RNDD);
    mpfr_div_2ui(offset->high, pi->high, 1, MPFR_RNDU);
  }
  binary(m, 45, t, a, offset);
  binary(m, 47, t, t, pi);
  if ((!mpfr_zero_p(t->low) && mpfr_get_exp(t->low) >= m->precision - 2) ||
      (!mpfr_zero_p(t->high) && mpfr_get_exp(t->high) >= m->precision - 2)) {
    if (op == 24)
      return CS_OSDI_PRECISION_DOMAIN;
    mpfr_set_si(d->low, -1, MPFR_RNDD);
    mpfr_set_ui(d->high, 1, MPFR_RNDU);
    return 0;
  }
  mpfr_ptr first = m->temporary[4], last = m->temporary[5],
           span = m->temporary[6];
  mpfr_ceil(first, t->low);
  mpfr_floor(last, t->high);
  int critical = mpfr_cmp(first, last) <= 0;
  if (op == 24) {
    if (critical)
      return CS_OSDI_PRECISION_DOMAIN;
    mpfr_tan(d->low, a->low, MPFR_RNDD);
    mpfr_tan(d->high, a->high, MPFR_RNDU);
    return 0;
  }
  if (op == 22) {
    mpfr_sin(m->temporary[7], a->low, MPFR_RNDD);
    mpfr_sin(m->temporary[8], a->high, MPFR_RNDD);
    mpfr_sin(m->temporary[9], a->low, MPFR_RNDU);
    mpfr_sin(m->temporary[10], a->high, MPFR_RNDU);
  } else {
    mpfr_cos(m->temporary[7], a->low, MPFR_RNDD);
    mpfr_cos(m->temporary[8], a->high, MPFR_RNDD);
    mpfr_cos(m->temporary[9], a->low, MPFR_RNDU);
    mpfr_cos(m->temporary[10], a->high, MPFR_RNDU);
  }
  mpfr_min(d->low, m->temporary[7], m->temporary[8], MPFR_RNDD);
  mpfr_max(d->high, m->temporary[9], m->temporary[10], MPFR_RNDU);
  if (critical) {
    mpfr_sub(span, last, first, MPFR_RNDU);
    if (mpfr_cmp_ui(span, 1) >= 0 ||
        (!mpfr_zero_p(first) && mpfr_get_exp(first) >= m->precision - 2)) {
      mpfr_set_si(d->low, -1, MPFR_RNDD);
      mpfr_set_ui(d->high, 1, MPFR_RNDU);
    } else {
      mpfr_fmod_ui(span, first, 2, MPFR_RNDN);
      if (mpfr_zero_p(span))
        mpfr_set_ui(d->high, 1, MPFR_RNDU);
      else
        mpfr_set_si(d->low, -1, MPFR_RNDD);
    }
  }
  return 0;
}
static int power(cs_precision_math *m, cs_precision_interval *d,
                 const cs_precision_interval *a,
                 const cs_precision_interval *b) {
  if (singleton(b) && mpfr_zero_p(b->low)) {
    mpfr_set_ui(d->low, 1, MPFR_RNDD);
    mpfr_set_ui(d->high, 1, MPFR_RNDU);
    return 0;
  }
  if (mpfr_cmp_ui(a->low, 0) > 0) {
    mpfr_srcptr av[2] = {a->low, a->high}, bv[2] = {b->low, b->high};
    for (unsigned i = 0; i < 2; i++)
      for (unsigned j = 0; j < 2; j++) {
        mpfr_pow(m->temporary[0], av[i], bv[j], MPFR_RNDD);
        mpfr_pow(m->temporary[1], av[i], bv[j], MPFR_RNDU);
        if ((!i && !j) || mpfr_cmp(m->temporary[0], d->low) < 0)
          mpfr_set(d->low, m->temporary[0], MPFR_RNDD);
        if ((!i && !j) || mpfr_cmp(m->temporary[1], d->high) > 0)
          mpfr_set(d->high, m->temporary[1], MPFR_RNDU);
      }
    return 0;
  }
  if (singleton(b) && mpfr_integer_p(b->low) &&
      mpfr_fits_slong_p(b->low, MPFR_RNDN)) {
    long exponent = mpfr_get_si(b->low, MPFR_RNDN);
    if (exponent < 0 && zero(a))
      return CS_OSDI_PRECISION_DOMAIN;
    mpfr_pow_si(m->temporary[0], a->low, exponent, MPFR_RNDD);
    mpfr_pow_si(m->temporary[1], a->high, exponent, MPFR_RNDD);
    mpfr_min(d->low, m->temporary[0], m->temporary[1], MPFR_RNDD);
    if (exponent > 0 && !(exponent & 1) && zero(a))
      mpfr_set_zero(d->low, 0);
    mpfr_pow_si(m->temporary[0], a->low, exponent, MPFR_RNDU);
    mpfr_pow_si(m->temporary[1], a->high, exponent, MPFR_RNDU);
    mpfr_max(d->high, m->temporary[0], m->temporary[1], MPFR_RNDU);
    return 0;
  }
  if (mpfr_zero_p(a->low) && mpfr_cmp_ui(a->high, 0) >= 0 &&
      mpfr_cmp_ui(b->low, 0) > 0) {
    mpfr_set_zero(d->low, 0);
    mpfr_pow(m->temporary[0], a->high, b->low, MPFR_RNDU);
    mpfr_pow(m->temporary[1], a->high, b->high, MPFR_RNDU);
    mpfr_max(d->high, m->temporary[0], m->temporary[1], MPFR_RNDU);
    return 0;
  }
  return CS_OSDI_PRECISION_DOMAIN;
}
static int real_operation(cs_precision_math *m, unsigned op,
                          cs_precision_interval *d,
                          const cs_precision_interval *a,
                          const cs_precision_interval *b) {
  if (op == 3) {
    mpfr_neg(m->temporary[0], a->high, MPFR_RNDD);
    mpfr_neg(d->high, a->low, MPFR_RNDU);
    mpfr_set(d->low, m->temporary[0], MPFR_RNDD);
  } else if (op >= 44 && op <= 47) {
    if (op == 47 && zero(b))
      return CS_OSDI_PRECISION_DOMAIN;
    binary(m, op, d, a, b);
  } else if (op >= 22 && op <= 24)
    return trig(m, op, d, a);
  else if (op == 67)
    return power(m, d, a, b);
  else if (op == 29) {
    mpfr_cosh(m->temporary[0], a->low, MPFR_RNDD);
    mpfr_cosh(m->temporary[1], a->high, MPFR_RNDD);
    mpfr_min(d->low, m->temporary[0], m->temporary[1], MPFR_RNDD);
    if (zero(a))
      mpfr_set_ui(d->low, 1, MPFR_RNDD);
    mpfr_cosh(m->temporary[0], a->low, MPFR_RNDU);
    mpfr_cosh(m->temporary[1], a->high, MPFR_RNDU);
    mpfr_max(d->high, m->temporary[0], m->temporary[1], MPFR_RNDU);
  } else {
    if ((op == 13 && mpfr_cmp_ui(a->low, 0) < 0) ||
        ((op == 16 || op == 18) && mpfr_cmp_ui(a->low, 0) <= 0) ||
        (op == 17 && mpfr_cmp_si(a->low, -1) <= 0))
      return CS_OSDI_PRECISION_DOMAIN;
    switch (op) {
#define MONO(code, fn)                                                         \
  case code:                                                                   \
    fn(d->low, a->low, MPFR_RNDD);                                             \
    fn(d->high, a->high, MPFR_RNDU);                                           \
    break
      MONO(13, mpfr_sqrt);
      MONO(14, mpfr_exp);
      MONO(15, mpfr_expm1);
      MONO(16, mpfr_log);
      MONO(17, mpfr_log1p);
      MONO(18, mpfr_log10);
      MONO(27, mpfr_atan);
      MONO(28, mpfr_sinh);
      MONO(30, mpfr_tanh);
      MONO(31, mpfr_asinh);
#undef MONO
    case 20:
      mpfr_floor(d->low, a->low);
      mpfr_floor(d->high, a->high);
      break;
    case 21:
      mpfr_ceil(d->low, a->low);
      mpfr_ceil(d->high, a->high);
      break;
    default:
      return CS_OSDI_PRECISION_UNSUPPORTED;
    }
  }
  return 0;
}
int cs_precision_supported(uint32_t op) {
  return (op >= 1 && op <= 24) || (op >= 27 && op <= 31) ||
         (op >= 34 && op <= 47) || (op >= 49 && op <= 64) || op == 67 ||
         op == 73 || op == 74;
}
int cs_precision_regularity(cs_precision_math *m, uint32_t op,
                            const cs_precision_value *a,
                            const cs_precision_value *b,
                            int active_a, int active_b, int same_source) {
  if (!active_a && !active_b)
    return 0;
  if (!cs_precision_charge(m, 16))
    return m->status;
  if ((op >= 53 && op <= 56) || op == 58 || op == 62) {
    if (!same_source && mpfr_cmp(a->real.high, b->real.low) >= 0 &&
        mpfr_cmp(a->real.low, b->real.high) <= 0)
      return CS_OSDI_PRECISION_NONSMOOTH;
  } else if (active_a && (op == 5 || op == 6 || op == 20 || op == 21)) {
    mpfr_ptr first = m->temporary[8], last = m->temporary[9];
    if (op == 5) {
      mpfr_sub_d(first, a->real.low, 0.5, MPFR_RNDD);
      mpfr_sub_d(last, a->real.high, 0.5, MPFR_RNDU);
      mpfr_ceil(first, first);
      mpfr_floor(last, last);
    } else {
      mpfr_ceil(first, a->real.low);
      mpfr_floor(last, a->real.high);
    }
    if (mpfr_cmp(first, last) <= 0 &&
        !(op == 6 && mpfr_zero_p(first) && mpfr_zero_p(last)))
      return CS_OSDI_PRECISION_NONSMOOTH;
  } else if (active_a && op == 10 && zero(&a->real)) {
    return CS_OSDI_PRECISION_NONSMOOTH;
  } else if (active_a && (op == 13 || op == 16 || op == 18)) {
    if (mpfr_zero_p(a->real.low) || mpfr_zero_p(a->real.high))
      return CS_OSDI_PRECISION_NONSMOOTH;
  } else if (active_a && op == 17) {
    if (mpfr_cmp_si(a->real.low, -1) == 0 ||
        mpfr_cmp_si(a->real.high, -1) == 0)
      return CS_OSDI_PRECISION_NONSMOOTH;
  } else if (op == 47 && active_b && zero(&b->real)) {
    return CS_OSDI_PRECISION_NONSMOOTH;
  } else if (op == 67 && mpfr_cmp_ui(a->real.low, 0) <= 0) {
    /* Fixed integer powers are smooth away from negative-power poles, and
       nonnegative integer powers are polynomial across a zero base. */
    if (active_b || !singleton(&b->real) || !mpfr_integer_p(b->real.low) ||
        (mpfr_cmp_ui(b->real.low, 0) < 0 && zero(&a->real)))
      return CS_OSDI_PRECISION_NONSMOOTH;
  }
  return 0;
}
static int positive_sum_endpoint(cs_precision_math *m, mpfr_ptr out,
                                 mpfr_srcptr x, mpfr_srcptr c,
                                 mpfr_rnd_t direction) {
  int negative = mpfr_sgn(x) < 0;
  mpfr_rnd_t inner = negative
                         ? (direction == MPFR_RNDD ? MPFR_RNDU : MPFR_RNDD)
                         : direction;
  mpfr_ptr square = m->temporary[0], sum = m->temporary[1],
           root = m->temporary[2], denominator = m->temporary[3];
  mpfr_mul(square, x, x, inner);
  mpfr_add(sum, square, c, inner);
  mpfr_sqrt(root, sum, inner);
  if (negative) {
    /* c/(sqrt(x*x+c)-x) is positive and decreases with its denominator. */
    mpfr_sub(denominator, root, x, inner);
    if (!mpfr_number_p(denominator) || mpfr_sgn(denominator) <= 0)
      return 0;
    mpfr_div(out, c, denominator, direction);
  } else {
    mpfr_add(out, x, root, direction);
  }
  return mpfr_number_p(out) && mpfr_sgn(out) >= 0;
}
int cs_precision_positive_sum_refine(cs_precision_math *m,
                                    cs_precision_interval *result,
                                    const cs_precision_interval *x,
                                    const cs_precision_interval *c) {
  if (!cs_precision_charge(m, 256))
    return m->status;
  if (!valid(result) || !valid(x) || !singleton(c) ||
      !mpfr_number_p(c->low) || mpfr_sgn(c->low) <= 0 ||
      !mpfr_number_p(x->low) || !mpfr_number_p(x->high))
    return CS_OSDI_PRECISION_NUMERIC_INVARIANT;
  mpfr_ptr lower = m->temporary[4], upper = m->temporary[5];
  /* f(x,c)=x+sqrt(x*x+c) is increasing in x for fixed positive c. These
     temporary-first writes also preserve input/output aliasing. Arithmetic
     range limits merely leave the already valid ordinary enclosure intact. */
  if (!positive_sum_endpoint(m, lower, x->low, c->low, MPFR_RNDD) ||
      !positive_sum_endpoint(m, upper, x->high, c->low, MPFR_RNDU))
    return 0;
  mpfr_max(lower, lower, result->low, MPFR_RNDD);
  mpfr_min(upper, upper, result->high, MPFR_RNDU);
  if (mpfr_cmp(lower, upper) > 0)
    return CS_OSDI_PRECISION_NUMERIC_INVARIANT;
  mpfr_set(result->low, lower, MPFR_RNDD);
  mpfr_set(result->high, upper, MPFR_RNDU);
  return 0;
}
int cs_precision_apply(cs_precision_math *m, uint32_t op, cs_precision_value *d,
                       const cs_precision_value *a,
                       const cs_precision_value *b) {
  /* Conservative fixed charges bound every explicit primitive in this closed
   * opcode implementation. */
  if (!cs_precision_charge(m, 1 + (d->type == 1 || a->type == 1 ? 64 : 1)))
    return m->status;
  int status = 0;
  uint32_t x = a->integer, y = b ? b->integer : 0;
  int32_t sx, sy;
  memcpy(&sx, &x, 4);
  memcpy(&sy, &y, 4);
  if (op == 12) {
    if (d->type == 1)
      cs_precision_interval_copy(&d->real, &a->real);
    else {
      d->integer = a->integer;
      d->string = a->string;
      d->length = a->length;
    }
  } else if (op == 1)
    d->integer = ~x;
  else if (op == 2)
    d->integer = !x;
  else if (op == 4)
    d->integer = 0u - x;
  else if (op == 5 || op == 6) {
    if (op == 5) {
      mpfr_round(m->temporary[0], a->real.low);
      mpfr_round(m->temporary[1], a->real.high);
    } else {
      mpfr_trunc(m->temporary[0], a->real.low);
      mpfr_trunc(m->temporary[1], a->real.high);
    }
    if (mpfr_cmp_si(m->temporary[0], INT32_MIN) < 0 ||
        mpfr_cmp_si(m->temporary[1], INT32_MAX) > 0)
      status = CS_OSDI_PRECISION_DOMAIN;
    else if (!mpfr_equal_p(m->temporary[0], m->temporary[1]))
      status = CS_OSDI_PRECISION_UNRESOLVED_BRANCH;
    else
      d->integer = (uint32_t)mpfr_get_si(m->temporary[0], MPFR_RNDN);
  } else if (op == 7 || op == 11) {
    mpfr_set_si(d->real.low, op == 11 ? (long)x : (long)sx, MPFR_RNDD);
    mpfr_set_si(d->real.high, op == 11 ? (long)x : (long)sx, MPFR_RNDU);
  } else if (op == 8)
    d->integer = x;
  else if (op == 9)
    d->integer = x != 0;
  else if (op == 10) {
    if (!zero(&a->real))
      d->integer = 1;
    else if (singleton(&a->real))
      d->integer = 0;
    else
      status = CS_OSDI_PRECISION_UNRESOLVED_BRANCH;
  } else if (op == 19) {
    if (!x)
      status = CS_OSDI_PRECISION_DOMAIN;
    else
      d->integer = 32u - (uint32_t)__builtin_clz(x);
  } else if (op >= 34 && op <= 43) {
    switch (op) {
    case 34:
      d->integer = x + y;
      break;
    case 35:
      d->integer = x - y;
      break;
    case 36:
      d->integer = x * y;
      break;
    case 37:
    case 38:
      if (!sy || (sx == INT32_MIN && sy == -1))
        status = CS_OSDI_PRECISION_DOMAIN;
      else
        d->integer = (uint32_t)(op == 37 ? sx / sy : sx % sy);
      break;
    case 39:
    case 40:
      if (y > 31)
        status = CS_OSDI_PRECISION_DOMAIN;
      else
        d->integer = op == 39 ? x << y : x >> y;
      break;
    case 41:
      d->integer = x ^ y;
      break;
    case 42:
      d->integer = x & y;
      break;
    case 43:
      d->integer = x | y;
      break;
    }
  } else if (op >= 49 && op <= 64) {
    int less = 0, greater = 0, equal = 0;
    if ((op >= 53 && op <= 56) || op == 58 || op == 62) {
      if (a == b) {
        equal = 1;
      } else {
        less = mpfr_cmp(a->real.high, b->real.low) < 0;
        greater = mpfr_cmp(a->real.low, b->real.high) > 0;
        equal = singleton(&a->real) && singleton(&b->real) &&
                mpfr_equal_p(a->real.low, b->real.low);
      }
      if (!less && !greater && !equal) {
        if (op == 53 && mpfr_cmp(a->real.low, b->real.high) >= 0)
          d->integer = 0;
        else if (op == 54 && mpfr_cmp(a->real.high, b->real.low) <= 0)
          d->integer = 0;
        else if (op == 55 && mpfr_cmp(a->real.low, b->real.high) >= 0)
          d->integer = 1;
        else if (op == 56 && mpfr_cmp(a->real.high, b->real.low) <= 0)
          d->integer = 1;
        else
          status = CS_OSDI_PRECISION_UNRESOLVED_BRANCH;
        if (status)
          return status;
        d->initialized = 1;
        return 0;
      }
    } else if (op == 59 || op == 63) {
      equal = a->length == b->length &&
              (!a->length || !memcmp(a->string, b->string, a->length));
    } else {
      less = sx < sy;
      greater = sx > sy;
      equal = x == y;
    }
    switch (op) {
    case 49:
    case 53:
      d->integer = less;
      break;
    case 50:
    case 54:
      d->integer = greater;
      break;
    case 51:
    case 55:
      d->integer = greater || equal;
      break;
    case 52:
    case 56:
      d->integer = less || equal;
      break;
    case 57:
    case 58:
    case 59:
    case 60:
      d->integer = equal;
      break;
    default:
      d->integer = !equal;
      break;
    }
  } else
    status = real_operation(m, op, &d->real, &a->real, b ? &b->real : NULL);
  if (!status && d->type == 1 && !valid(&d->real))
    status = CS_OSDI_PRECISION_DOMAIN;
  if (!status)
    d->initialized = 1;
  return status;
}
int cs_precision_midpoint(cs_precision_math *m, const cs_precision_interval *v,
                          double *out) {
  if (!cs_precision_charge(m, 8))
    return m->status;
  if (!valid(v) || !mpfr_number_p(v->low) || !mpfr_number_p(v->high))
    return CS_OSDI_PRECISION_NONFINITE_PROJECTION;
  mpfr_add(m->temporary[0], v->low, v->high, MPFR_RNDN);
  mpfr_div_2ui(m->temporary[0], m->temporary[0], 1, MPFR_RNDN);
  *out = mpfr_get_d(m->temporary[0], MPFR_RNDN);
  return isfinite(*out) ? 0 : CS_OSDI_PRECISION_NONFINITE_PROJECTION;
}
int cs_precision_bounds(cs_precision_math *m, const cs_precision_interval *v,
                        double *lower, double *upper) {
  if (!cs_precision_charge(m, 8))
    return m->status;
  if (!valid(v) || !mpfr_number_p(v->low) || !mpfr_number_p(v->high))
    return CS_OSDI_PRECISION_NONFINITE_PROJECTION;
  double lo = mpfr_get_d(v->low, MPFR_RNDD);
  double hi = mpfr_get_d(v->high, MPFR_RNDU);
  if (!isfinite(lo) || !isfinite(hi) || lo > hi)
    return CS_OSDI_PRECISION_NONFINITE_PROJECTION;
  *lower = lo;
  *upper = hi;
  return 0;
}
int cs_precision_project(cs_precision_math *m, const cs_precision_interval *v,
                         double *high, double *low, double *radius) {
  int s = cs_precision_midpoint(m, v, high);
  if (s)
    return s;
  if (!cs_precision_charge(m, 16))
    return m->status;
  mpfr_set_d(m->temporary[1], *high, MPFR_RNDN);
  mpfr_sub(m->temporary[2], m->temporary[0], m->temporary[1], MPFR_RNDN);
  *low = mpfr_get_d(m->temporary[2], MPFR_RNDN);
  if (!isfinite(*low))
    return CS_OSDI_PRECISION_NONFINITE_PROJECTION;
  /* TwoSum renormalizes the rounded pair before the enclosure is measured.
     The residual can round to an exact half-ULP even when the true midpoint
     lies just below it, so independent high/low rounding is insufficient. */
  double sum = *high + *low;
  double transferred = sum - *high;
  double error = (*high - (sum - transferred)) + (*low - transferred);
  if (!isfinite(sum) || !isfinite(error))
    return CS_OSDI_PRECISION_NONFINITE_PROJECTION;
  *high = sum;
  *low = error;
  mpfr_set_d(m->temporary[1], *high, MPFR_RNDN);
  mpfr_set_d(m->temporary[2], *low, MPFR_RNDN);
  mpfr_add(m->temporary[3], m->temporary[1], m->temporary[2], MPFR_RNDD);
  mpfr_add(m->temporary[4], m->temporary[1], m->temporary[2], MPFR_RNDU);
  mpfr_sub(m->temporary[5], m->temporary[4], v->low, MPFR_RNDU);
  mpfr_sub(m->temporary[6], v->high, m->temporary[3], MPFR_RNDU);
  mpfr_max(m->temporary[5], m->temporary[5], m->temporary[6], MPFR_RNDU);
  *radius = mpfr_get_d(m->temporary[5], MPFR_RNDU);
  return isfinite(*radius) && *radius >= 0
             ? 0
             : CS_OSDI_PRECISION_NONFINITE_PROJECTION;
}
