#include "IdentityInternal.h"
#include <math.h>

static uint64_t bits(mpz_srcptr z) { return mpz_sgn(z) ? (uint64_t)mpz_sizeinbase(z, 2) : 0; }
static int bound(cs_precision_constitutive_workspace *w, uint64_t a, uint64_t b, uint64_t extra) {
  uint64_t cap = w->limits.maximum_coefficient_bits;
  if (a > cap || b > cap || extra > cap || a > cap - b || a + b > cap - extra)
    return CS_IDENTITY_COEFFICIENT_LIMIT;
  /* Includes operand inspection and a conservative bit-product arithmetic cost. */
  uint64_t n = a + b + extra;
  if (n && n > UINT64_MAX / n) return CS_IDENTITY_COEFFICIENT_LIMIT;
  return cs_precision_charge(&w->program->math, n * n + 1) ? 0 : w->program->math.status;
}
int cs_constitutive_scalar_valid(cs_precision_constitutive_workspace *w, mpq_srcptr q) {
  uint64_t numerator=bits(mpq_numref(q)), denominator=bits(mpq_denref(q));
  if (numerator > w->limits.maximum_coefficient_bits || denominator > w->limits.maximum_coefficient_bits)
    return CS_IDENTITY_COEFFICIENT_LIMIT;
  if(numerator>=UINT64_MAX-denominator || !cs_precision_charge(&w->program->math,numerator+denominator+1))
    return w->program->math.status?w->program->math.status:CS_IDENTITY_COEFFICIENT_LIMIT;
  if (mpz_sgn(mpq_denref(q)) <= 0 || mpz_popcount(mpq_denref(q)) != 1 ||
      (mpz_cmp_ui(mpq_denref(q),1)!=0 && mpz_even_p(mpq_numref(q))))
    return CS_IDENTITY_INVALID;
  return 0;
}
int cs_constitutive_set_bits(cs_precision_constitutive_workspace *w, mpq_ptr q, uint64_t raw) {
  int touched=cs_constitutive_touch(w,q);if(touched)return touched;
  double value; memcpy(&value, &raw, sizeof(value));
  if (!isfinite(value)) return CS_IDENTITY_INVALID;
  uint64_t exponent = (raw >> 52) & 2047;
  uint64_t fraction = raw & UINT64_C(0x000fffffffffffff);
  uint64_t numerator = exponent ? fraction | UINT64_C(0x0010000000000000) : fraction;
  int shift = exponent ? (int)exponent - 1023 - 52 : -1074;
  if (numerator) { unsigned trailing = (unsigned)__builtin_ctzll(numerator); numerator >>= trailing; shift += (int)trailing; }
  uint64_t nb = numerator ? 64u - (uint64_t)__builtin_clzll(numerator) : 0;
  uint64_t db = numerator && shift < 0 ? (uint64_t)(-shift) + 1 : 1;
  if (numerator && shift > 0) nb += (uint64_t)shift;
  int status = bound(w, nb, 0, 0); if (status) return status;
  status = bound(w, db, 0, 0); if (status) return status;
  mpq_set_d(q, value);
  return cs_constitutive_scalar_valid(w, q);
}
int cs_constitutive_add(cs_precision_constitutive_workspace *w, mpq_ptr out,
    mpq_srcptr a, mpq_srcptr b, int subtract) {
  int status=cs_constitutive_touch(w,out);if(status)return status;
  status = cs_constitutive_scalar_valid(w, a); if (status) return status;
  status = cs_constitutive_scalar_valid(w, b); if (status) return status;
  if (!mpq_sgn(a)) { mpq_set(out, b); if (subtract) mpq_neg(out, out); return 0; }
  if (!mpq_sgn(b)) { mpq_set(out, a); return 0; }
  uint64_t an=bits(mpq_numref(a)), ad=bits(mpq_denref(a));
  uint64_t bn=bits(mpq_numref(b)), bd=bits(mpq_denref(b));
  status=bound(w,an,bd,1); if(status)return status;
  status=bound(w,bn,ad,1); if(status)return status;
  status=bound(w,ad,bd,0); if(status)return status;
  if(subtract)mpq_sub(out,a,b);else mpq_add(out,a,b);
  return cs_constitutive_scalar_valid(w,out);
}
int cs_constitutive_mul(cs_precision_constitutive_workspace *w, mpq_ptr out,
    mpq_srcptr a, mpq_srcptr b) {
  int status=cs_constitutive_touch(w,out);if(status)return status;
  status=cs_constitutive_scalar_valid(w,a);if(status)return status;
  status=cs_constitutive_scalar_valid(w,b);if(status)return status;
  if(!mpq_sgn(a)||!mpq_sgn(b)){mpq_set_ui(out,0,1);return 0;}
  status=bound(w,bits(mpq_numref(a)),bits(mpq_numref(b)),0);if(status)return status;
  status=bound(w,bits(mpq_denref(a)),bits(mpq_denref(b)),0);if(status)return status;
  mpq_mul(out,a,b);return cs_constitutive_scalar_valid(w,out);
}
