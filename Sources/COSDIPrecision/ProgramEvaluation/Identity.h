#ifndef CS_PRECISION_IDENTITY_H
#define CS_PRECISION_IDENTITY_H
#include "../Arithmetic/Arithmetic.h"
typedef struct cs_precision_identity_entry cs_precision_identity_entry;
typedef struct {
  cs_precision_identity_entry *entries;
  unsigned char *limbs;
  uint32_t *slots;
  uint32_t count, capacity, slot_count;
  size_t limb_bytes;
} cs_precision_identity;
void cs_precision_identity_open(cs_precision_identity *identity,
                                uint32_t registers, uint32_t maximum_precision);
int cs_precision_identity_reset(cs_precision_identity *identity,
                                cs_precision_math *math);
int cs_precision_identify(cs_precision_identity *identity,
                          cs_precision_math *math, cs_precision_value *value,
                          uint32_t opcode, uint32_t first, uint32_t second);
#endif
