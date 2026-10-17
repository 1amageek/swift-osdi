#include "Identity.h"
#include "../Arena/Arena.h"
#include <limits.h>
#include <string.h>
#include <unistd.h>
struct cs_precision_identity_entry {
  uint64_t hash;
  mpfr_exp_t exponent;
  const unsigned char *text;
  uint32_t type, opcode, first, second, integer, length;
  int sign;
};
static void resource(void) { _exit(CS_OSDI_PRECISION_EXIT_ARENA); }
static void *storage(size_t count, size_t stride) {
  if (stride && count > SIZE_MAX / stride)
    resource();
  return cs_precision_arena_allocate(count * stride);
}
void cs_precision_identity_open(cs_precision_identity *t, uint32_t registers,
                                uint32_t precision) {
  memset(t, 0, sizeof(*t));
  if (registers > (UINT32_MAX - 32) / 2)
    resource();
  t->capacity = 2 * registers + 32;
  t->slot_count = 1;
  while (t->slot_count < t->capacity) {
    if (t->slot_count > UINT32_MAX / 4)
      resource();
    t->slot_count *= 2;
  }
  t->slot_count *= 2;
  t->limb_bytes = mpfr_custom_get_size(precision);
  t->slots = storage(t->slot_count, sizeof(*t->slots));
  t->entries = storage((size_t)t->capacity + 1, sizeof(*t->entries));
  t->limbs = storage((size_t)t->capacity + 1, t->limb_bytes);
}
int cs_precision_identity_reset(cs_precision_identity *t,
                                cs_precision_math *m) {
  if (!cs_precision_charge(m, t->slot_count))
    return m->status;
  memset(t->slots, 0, t->slot_count * sizeof(*t->slots));
  t->count = 0;
  return 0;
}
static uint64_t hash_bytes(uint64_t h, const void *bytes, size_t length) {
  const unsigned char *p = bytes;
  for (size_t j = 0; j < length; j++) {
    h ^= p[j];
    h *= UINT64_C(1099511628211);
  }
  return h;
}
int cs_precision_identify(cs_precision_identity *t, cs_precision_math *m,
                          cs_precision_value *v, uint32_t op, uint32_t a,
                          uint32_t b) {
  struct cs_precision_identity_entry key = {0};
  key.type = v->type;
  const void *bytes = NULL;
  size_t length = 0;
  if (v->type == 1 && mpfr_equal_p(v->real.low, v->real.high)) {
    /* MPFR values at one request precision have canonical normalized limbs.
       Zero and infinity have no meaningful significand storage. */
    key.sign = mpfr_signbit(v->real.low) ? -1 : 1;
    if (mpfr_zero_p(v->real.low)) {
      key.integer = 1;
      key.sign = 1;
    } else if (mpfr_inf_p(v->real.low)) {
      key.integer = 2;
    } else {
      key.exponent = mpfr_get_exp(v->real.low);
      bytes = mpfr_custom_get_significand(v->real.low);
      length = mpfr_custom_get_size(m->precision);
    }
  } else if (v->type == 1) {
    if (!op || !a || (b == 0 && op >= 44 && op <= 47)) {
      /* A caller-supplied interval with no expression authority is unique. */
      if (!cs_precision_charge(m, 1))
        return m->status;
      if (t->count == t->capacity)
        resource();
      v->identity = ++t->count;
      return 0;
    }
    key.opcode = op;
    key.first = a;
    key.second = b;
  } else if (v->type == 4) {
    key.length = v->length;
    key.text = v->string;
    bytes = v->string;
    length = v->length;
  } else {
    key.integer = v->integer;
  }
  if (!cs_precision_charge(m, 8 + (length + 7) / 8))
    return m->status;
  uint64_t h = UINT64_C(1469598103934665603);
  h = hash_bytes(h, &key.type, sizeof(key.type));
  h = hash_bytes(h, &key.opcode, sizeof(key.opcode));
  h = hash_bytes(h, &key.first, sizeof(key.first));
  h = hash_bytes(h, &key.second, sizeof(key.second));
  h = hash_bytes(h, &key.integer, sizeof(key.integer));
  h = hash_bytes(h, &key.exponent, sizeof(key.exponent));
  h = hash_bytes(h, &key.sign, sizeof(key.sign));
  h = hash_bytes(h, &key.length, sizeof(key.length));
  h = hash_bytes(h, bytes, length);
  key.hash = h;
  uint32_t slot = (uint32_t)h & (t->slot_count - 1);
  while (t->slots[slot]) {
    if (!cs_precision_charge(m, 8 + (length + 7) / 8))
      return m->status;
    uint32_t id = t->slots[slot];
    const struct cs_precision_identity_entry *e = &t->entries[id];
    if (e->hash == h && e->type == key.type && e->opcode == key.opcode &&
        e->first == key.first && e->second == key.second &&
        e->integer == key.integer && e->exponent == key.exponent &&
        e->sign == key.sign && e->length == key.length &&
        (!length ||
         !memcmp(bytes,
                 v->type == 4 ? (const void *)e->text
                              : t->limbs + (size_t)id * t->limb_bytes,
                 length))) {
      v->identity = id;
      return 0;
    }
    slot = (slot + 1) & (t->slot_count - 1);
  }
  if (t->count == t->capacity)
    resource();
  uint32_t id = ++t->count;
  t->entries[id] = key;
  if (length && v->type == 1)
    memcpy(t->limbs + (size_t)id * t->limb_bytes, bytes, length);
  t->slots[slot] = id;
  v->identity = id;
  return 0;
}
