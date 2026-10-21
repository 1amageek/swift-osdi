#ifndef CS_PRECISION_CONSTITUTIVE_INTERNAL_H
#define CS_PRECISION_CONSTITUTIVE_INTERNAL_H
#include "ConstitutiveIdentities.h"
#include "../DecisionCover/CoverInternal.h"
#include "../Arena/Arena.h"
#include <limits.h>
#include <string.h>
#define CS_ID_NONE UINT32_MAX
enum { CS_ID_ATOM = 1, CS_ID_LITERAL, CS_ID_ADD, CS_ID_SUB, CS_ID_NEG, CS_ID_MUL, CS_ID_COORDINATE };
int cs_constitutive_touch(cs_precision_constitutive_workspace *, mpq_ptr);
int cs_constitutive_observe(void *, cs_precision_program *, uint32_t, uint32_t, uint32_t);
int cs_constitutive_add(cs_precision_constitutive_workspace *, mpq_ptr, mpq_srcptr, mpq_srcptr, int);
int cs_constitutive_mul(cs_precision_constitutive_workspace *, mpq_ptr, mpq_srcptr, mpq_srcptr);
int cs_constitutive_set_bits(cs_precision_constitutive_workspace *, mpq_ptr, uint64_t);
int cs_constitutive_scalar_valid(cs_precision_constitutive_workspace *, mpq_srcptr);
int cs_constitutive_prepare(cs_precision_constitutive_workspace *);
int cs_constitutive_expand(cs_precision_constitutive_workspace *, uint32_t, const mpq_t *, mpq_t *);
int cs_constitutive_intersect(cs_precision_constitutive_workspace *, uint32_t,
    mpq_t *, uint32_t, uint32_t *);
#endif
