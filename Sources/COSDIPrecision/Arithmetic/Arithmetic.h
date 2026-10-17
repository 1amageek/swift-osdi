#ifndef CS_PRECISION_ARITHMETIC_H
#define CS_PRECISION_ARITHMETIC_H
#include <OSDICompilerPrecision.h>
#include <OSDIPrecisionProtocol.h>
#include <mpfr.h>
#include <stddef.h>
typedef struct {
  mpfr_t low, high;
} cs_precision_interval;
typedef struct {
  uint32_t type, initialized, identity, failure, failure_detail;
  cs_precision_interval real;
  uint32_t integer;
  const unsigned char *string;
  uint32_t length;
} cs_precision_value;
typedef struct {
  mpfr_prec_t precision;
  uint64_t work, limit;
  int status;
  cs_precision_interval scratch[8];
  mpfr_t temporary[12];
} cs_precision_math;
void cs_precision_interval_init(cs_precision_interval *value,
                                mpfr_prec_t precision, void *limbs);
void cs_precision_math_init(cs_precision_math *math, uint32_t precision,
                            uint64_t limit);
int cs_precision_charge(cs_precision_math *math, uint64_t units);
int cs_precision_real_bits(cs_precision_interval *value, uint64_t bits);
void cs_precision_interval_copy(cs_precision_interval *destination,
                                const cs_precision_interval *source);
int cs_precision_apply(cs_precision_math *math, uint32_t opcode,
                       cs_precision_value *destination,
                       const cs_precision_value *a,
                       const cs_precision_value *b);
int cs_precision_project(cs_precision_math *math,
                         const cs_precision_interval *value, double *high,
                         double *low, double *radius);
int cs_precision_midpoint(cs_precision_math *math,
                          const cs_precision_interval *value, double *output);
int cs_precision_supported(uint32_t opcode);
#endif
