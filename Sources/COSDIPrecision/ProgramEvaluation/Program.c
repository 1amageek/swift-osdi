#include "Program.h"
#include "Chart.h"
#include "Frontier.h"
#include "../Arena/Arena.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define NONE CS_OSDI_PRECISION_NONE
#define BAD CS_OSDI_PRECISION_INVALID_PROGRAM
struct cs_precision_chart_execution {
  const cs_precision_chart_request *request;
  const cs_precision_chart_scratch *scratch;
  size_t guard_count;
  uint64_t steps;
};
struct cs_precision_frontier_execution {
  const cs_precision_frontier_request *request;
  const cs_precision_frontier_scratch *scratch;
  size_t count;
};
static int source_frontier_event(cs_precision_program *, uint32_t, uint32_t,
    uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t *);
static int source_cell_operation(cs_precision_program *, uint32_t, uint32_t,
    uint32_t, const CSOsdiPrecisionInstruction *, cs_precision_value *);
#define REQUIRE(test)                                                          \
  do {                                                                         \
    if (!(test))                                                               \
      return BAD;                                                              \
  } while (0)
static int section(const void *base, size_t length, CSOsdiPrecisionSection s,
                   size_t stride, const void **out, size_t header) {
  if (s.stride != stride || s.offset < header ||
      s.offset % _Alignof(uint64_t) || s.offset > length ||
      s.count > (length - (size_t)s.offset) / stride)
    return 0;
  *out = (const unsigned char *)base + s.offset;
  return 1;
}
static int disjoint(const CSOsdiPrecisionSection *sections, size_t count) {
  for (size_t i = 0; i < count; i++)
    for (size_t j = 0; j < i; j++) {
      uint64_t a = sections[i].offset, b = sections[j].offset;
      uint64_t an = (uint64_t)sections[i].count * sections[i].stride,
               bn = (uint64_t)sections[j].count * sections[j].stride;
      if (an && bn && a < b + bn && b < a + an)
        return 0;
    }
  return 1;
}
static int range(uint32_t first, uint32_t count, uint32_t total) {
  return first <= total && count <= total - first;
}
static int text(uint32_t off, uint32_t len, uint32_t total) {
  return off <= total && len <= total - off;
}
static int string_equal(const unsigned char *a, uint32_t an,
                        const unsigned char *b, uint32_t bn) {
  return an == bn && (!an || !memcmp(a, b, an));
}
static void *array(size_t count, size_t size) {
  if (size && count > SIZE_MAX / size)
    _Exit(CS_OSDI_PRECISION_EXIT_ARENA);
  void *p = cs_precision_arena_allocate(count * size);
  memset(p, 0, count * size);
  return p;
}
static uint32_t type(cs_precision_program *p, uint32_t r) {
  return r < p->header->registers.count ? p->registers[r].type : 0;
}
static int owned(cs_precision_program *p, uint32_t r, uint32_t f) {
  return r < p->header->registers.count &&
         (p->owners[r] == f || p->owners[r] == NONE - 1);
}
static int claim(cs_precision_program *p, uint32_t r, uint32_t f) {
  if (r >= p->header->registers.count || p->owners[r] != NONE)
    return 0;
  p->owners[r] = f;
  return 1;
}
static int instruction_types(cs_precision_program *p,
                             const CSOsdiPrecisionInstruction *i) {
  uint32_t op = i->opcode, a = type(p, i->a), b = type(p, i->b),
           d = type(p, i->destination);
  if (op == 73)
    return i->destination == NONE && i->b == NONE && i->aux == NONE;
  if (op == 74)
    return a == 3 && b == d && type(p, i->aux) == d && d;
  if (!a || !d)
    return 0;
  if (op == 12)
    return a == d && i->b == NONE;
  if (op <= 4)
    return i->b == NONE &&
           a == (op == 1 || op == 4 ? 2
                 : op == 2          ? 3
                                    : 1) &&
           d == a;
  if (op >= 5 && op <= 11) {
    static const uint32_t at[] = {1, 1, 2, 3, 2, 1, 3},
                          dt[] = {2, 2, 1, 2, 3, 3, 1};
    return i->b == NONE && a == at[op - 5] && d == dt[op - 5];
  }
  if (op == 19)
    return i->b == NONE && a == 2 && d == 2;
  if (op >= 13 && op <= 33)
    return i->b == NONE && a == 1 && d == 1;
  if (op >= 34 && op <= 43)
    return a == 2 && b == 2 && d == 2;
  if ((op >= 44 && op <= 48) || (op >= 65 && op <= 67))
    return a == 1 && b == 1 && d == 1;
  if (op >= 49 && op <= 64) {
    uint32_t required = op <= 52 || op == 57 || op == 61   ? 2
                        : op <= 56 || op == 58 || op == 62 ? 1
                        : op == 59 || op == 63             ? 4
                                                           : 3;
    return a == required && b == required && d == 3;
  }
  return 0;
}
static void initialize_values(cs_precision_value *v, size_t n,
                              const CSOsdiPrecisionRegister *types,
                              mpfr_prec_t precision) {
  size_t limb = mpfr_custom_get_size(precision), real_count = 0;
  for (size_t i = 0; i < n; i++)
    if (types[i].type == 1)
      real_count++;
  unsigned char *storage = array(real_count, 2 * limb);
  for (size_t i = 0; i < n; i++) {
    v[i].type = types[i].type;
    if (v[i].type == 1) {
      cs_precision_interval_init(&v[i].real, precision, storage);
      storage += 2 * limb;
    }
  }
}
int cs_precision_program_open(cs_precision_program *p, const void *bytes,
                              size_t length, const void *setup,
                              size_t setup_length,
                              const cs_osdi_precision_control_v1 *control) {
  memset(p, 0, sizeof(*p));
  p->detail = NONE;
  p->header = bytes;
  p->setup = setup;
  p->range_mode = control->protocol_version == CS_OSDI_RANGE_PROTOCOL_VERSION;
  REQUIRE(length >= sizeof(*p->header) && setup_length >= sizeof(*p->setup));
  const CSOsdiPrecisionProgramHeader *h = p->header;
  const CSOsdiPrecisionSetupHeader *s = p->setup;
  REQUIRE(!memcmp(h->magic, CS_OSDI_PRECISION_PROGRAM_MAGIC, 8) &&
          h->version == 1 && h->header_bytes == sizeof(*h) &&
          h->program_bytes == length &&
          h->flags == CS_OSDI_PRECISION_STATELESS &&
          h->native_state_count == 0 &&
          h->raw_node_count == control->raw_node_count &&
          h->descriptor_index == control->descriptor_index);
  REQUIRE(s->version == 1 && s->header_bytes == sizeof(*s) &&
          s->setup_bytes == setup_length &&
          s->active_unknown_count == control->active_count &&
          s->raw_to_active.count == h->raw_node_count &&
          s->connected_terminal_count <= h->raw_node_count);
  double temperature;
  memcpy(&temperature, &s->temperature_binary64, 8);
  REQUIRE(isfinite(temperature) && temperature > 0);
#define SEC(field, Type)                                                       \
  REQUIRE(section(bytes, length, h->field, sizeof(Type),                       \
                  (const void **)&p->field, sizeof(*h)))
  SEC(registers, CSOsdiPrecisionRegister);
  SEC(constants, CSOsdiPrecisionConstant);
  SEC(inputs, CSOsdiPrecisionInput);
  SEC(functions, CSOsdiPrecisionFunction);
  SEC(blocks, CSOsdiPrecisionBlock);
  SEC(instructions, CSOsdiPrecisionInstruction);
  REQUIRE(section(bytes, length, h->phi_edges, sizeof(CSOsdiPrecisionPhiEdge),
                  (const void **)&p->phi, sizeof(*h)));
  SEC(roots, CSOsdiPrecisionRoot);
  SEC(parameters, CSOsdiPrecisionParameter);
  SEC(nodes, CSOsdiPrecisionNode);
  SEC(strings, unsigned char);
#undef SEC
  const CSOsdiPrecisionSection program_sections[] = {
      h->registers,  h->constants,    h->inputs,    h->functions,
      h->blocks,     h->instructions, h->phi_edges, h->roots,
      h->parameters, h->nodes,        h->strings};
  REQUIRE(disjoint(program_sections, 11));
  REQUIRE(section(setup, setup_length, s->parameter_facts,
                  sizeof(CSOsdiPrecisionParameterFact),
                  (const void **)&p->facts, sizeof(*s)));
  REQUIRE(section(setup, setup_length, s->simulation_facts,
                  sizeof(CSOsdiPrecisionSimulationFact),
                  (const void **)&p->simulation, sizeof(*s)));
  REQUIRE(section(setup, setup_length, s->raw_to_active, 4,
                  (const void **)&p->map, sizeof(*s)));
  REQUIRE(section(setup, setup_length, s->strings, 1,
                  (const void **)&p->setup_strings, sizeof(*s)));
  const CSOsdiPrecisionSection setup_sections[] = {
      s->parameter_facts, s->simulation_facts, s->raw_to_active, s->strings};
  REQUIRE(disjoint(setup_sections, 4));
  REQUIRE(h->functions.count == 3 && h->nodes.count == h->raw_node_count &&
          h->registers.count > 0);
  p->active_count = control->active_count;
  p->maximum_precision = control->maximum_precision_bits;
  p->owners = array(h->registers.count, 4);
  memset(p->owners, 255, (size_t)h->registers.count * 4);
  p->phi_blocks = array(h->registers.count, 4);
  memset(p->phi_blocks, 255, (size_t)h->registers.count * 4);
  for (uint32_t j = 0; j < h->registers.count; j++)
    REQUIRE(p->registers[j].type >= 1 && p->registers[j].type <= 4 &&
            !p->registers[j].reserved);
  for (uint32_t j = 0; j < h->nodes.count; j++)
    REQUIRE(p->nodes[j].osdi_index == j && p->nodes[j].is_flow <= 1 &&
            text(p->nodes[j].name_offset, p->nodes[j].name_bytes,
                 h->strings.count));
  p->fact_index = array(h->parameters.count, 4);
  memset(p->fact_index, 255, (size_t)h->parameters.count * 4);
  for (uint32_t j = 0; j < h->parameters.count; j++) {
    const CSOsdiPrecisionParameter *q = &p->parameters[j];
    REQUIRE(q->scope >= 1 && q->scope <= 2 && q->type >= 1 && q->type <= 4 &&
            !(q->flags & ~CS_OSDI_PRECISION_HAS_PRIMITIVE_DEFAULT) &&
            text(q->name_offset, q->name_bytes, h->strings.count));
    for (uint32_t k = 0; k < j; k++) {
      REQUIRE(q->osdi_index != p->parameters[k].osdi_index ||
              q->scope != p->parameters[k].scope);
      if (q->osdi_index == p->parameters[k].osdi_index)
        REQUIRE(q->type == p->parameters[k].type);
    }
  }
  p->model_peers = array(h->parameters.count, 4);
  for (uint32_t j = 0; j < h->parameters.count; j++) {
    const CSOsdiPrecisionParameter *q = &p->parameters[j];
    p->model_peers[j] = NONE;
    for (uint32_t k = 0; k < h->parameters.count; k++)
      if (p->parameters[k].scope == 1 &&
          p->parameters[k].osdi_index == q->osdi_index)
        p->model_peers[j] = k;
    if (!(q->flags & CS_OSDI_PRECISION_HAS_PRIMITIVE_DEFAULT)) {
      REQUIRE(!q->primitive_default_bits);
      continue;
    }
    if (q->type == 1) {
      double d;
      memcpy(&d, &q->primitive_default_bits, 8);
      REQUIRE(isfinite(d));
    }
    if (q->type == 2)
      REQUIRE((uint64_t)(int64_t)(int32_t)q->primitive_default_bits ==
              q->primitive_default_bits);
    if (q->type == 3)
      REQUIRE(q->primitive_default_bits <= 1);
    if (q->type == 4)
      REQUIRE(text((uint32_t)q->primitive_default_bits,
                   (uint32_t)(q->primitive_default_bits >> 32),
                   h->strings.count));
  }
  for (uint32_t j = 0; j < s->parameter_facts.count; j++) {
    const CSOsdiPrecisionParameterFact *q = &p->facts[j];
    REQUIRE(q->parameter_index < h->parameters.count && q->given <= 1 &&
            p->fact_index[q->parameter_index] == NONE);
    REQUIRE(q->given || q->bits == 0);
    p->fact_index[q->parameter_index] = j;
    uint32_t t = p->parameters[q->parameter_index].type;
    if (t == 1) {
      double d;
      memcpy(&d, &q->bits, 8);
      REQUIRE(isfinite(d));
    }
    if (t == 2)
      REQUIRE((uint64_t)(int64_t)(int32_t)q->bits == q->bits);
    if (t == 3)
      REQUIRE(q->bits <= 1);
    if (t == 4)
      REQUIRE(
          text((uint32_t)q->bits, (uint32_t)(q->bits >> 32), s->strings.count));
  }
  for (uint32_t j = 0; j < s->simulation_facts.count; j++) {
    const CSOsdiPrecisionSimulationFact *q = &p->simulation[j];
    REQUIRE(q->type >= 1 && q->type <= 4 && q->given <= 1 &&
            text(q->name_offset, q->name_bytes, s->strings.count) &&
            (!q->given ? q->bits == 0 : 1));
    if (q->type == 1) {
      double d;
      memcpy(&d, &q->bits, 8);
      REQUIRE(isfinite(d));
    }
    if (q->type == 2)
      REQUIRE((uint64_t)(int64_t)(int32_t)q->bits == q->bits);
    if (q->type == 3)
      REQUIRE(q->bits <= 1);
    if (q->type == 4)
      REQUIRE(
          text((uint32_t)q->bits, (uint32_t)(q->bits >> 32), s->strings.count));
    for (uint32_t k = 0; k < j; k++)
      REQUIRE(!string_equal(p->setup_strings + q->name_offset, q->name_bytes,
                            p->setup_strings + p->simulation[k].name_offset,
                            p->simulation[k].name_bytes));
  }
  p->dense = array(h->raw_node_count + 1, 4);
  p->topology = array(h->raw_node_count + 1, 4);
  for (uint32_t j = 0; j < h->raw_node_count; j++)
    REQUIRE(p->map[j] == NONE || p->map[j] < p->active_count);
  for (uint64_t j = 0; j < p->active_count; j++) {
    int seen = 0;
    for (uint32_t k = 0; k < h->raw_node_count; k++)
      if (p->map[k] == j)
        seen = 1;
    REQUIRE(seen);
  }
  for (uint32_t j = 0; j < h->constants.count; j++) {
    const CSOsdiPrecisionConstant *q = &p->constants[j];
    REQUIRE(type(p, q->destination) == q->type &&
            claim(p, q->destination, NONE - 1));
    if (q->type == 1) {
      double d;
      memcpy(&d, &q->bits, 8);
      REQUIRE(!isnan(d));
    }
    if (q->type == 2)
      REQUIRE((uint64_t)(int64_t)(int32_t)q->bits == q->bits);
    if (q->type == 3)
      REQUIRE(q->bits <= 1);
    if (q->type == 4)
      REQUIRE(
          text((uint32_t)q->bits, (uint32_t)(q->bits >> 32), h->strings.count));
  }
  for (uint32_t f = 0; f < 3; f++) {
    const CSOsdiPrecisionFunction *q = &p->functions[f];
    REQUIRE(q->stage == f + 1 && !q->reserved0 && !q->reserved1 &&
            q->block_count &&
            range(q->first_block, q->block_count, h->blocks.count) &&
            q->entry_block >= q->first_block &&
            q->entry_block - q->first_block < q->block_count);
    if (f)
      REQUIRE(q->first_block >= p->functions[f - 1].first_block +
                                    p->functions[f - 1].block_count);
    for (uint32_t b = q->first_block; b < q->first_block + q->block_count;
         b++) {
      const CSOsdiPrecisionBlock *block = &p->blocks[b];
      REQUIRE(block->function_index == f && !block->reserved &&
              range(block->first_instruction, block->instruction_count,
                    h->instructions.count) &&
              range(block->first_phi_edge, block->phi_edge_count,
                    h->phi_edges.count));
      REQUIRE(block->terminator <= 2);
      if (block->terminator) {
        REQUIRE(block->then_block >= q->first_block &&
                block->then_block - q->first_block < q->block_count);
      }
      if (block->terminator == 2)
        REQUIRE(type(p, block->condition) == 3 &&
                block->else_block >= q->first_block &&
                block->else_block - q->first_block < q->block_count);
      if (block->phi_edge_count > p->stage_capacity)
        p->stage_capacity = block->phi_edge_count;
      for (uint32_t k = 0; k < block->instruction_count; k++) {
        uint32_t ordinal = block->first_instruction + k;
        const CSOsdiPrecisionInstruction *i = &p->instructions[ordinal];
        p->detail = ordinal;
        if (!cs_precision_supported(i->opcode))
          return CS_OSDI_PRECISION_UNSUPPORTED;
        REQUIRE(!i->reserved && (i->opcode == 74 || i->aux == NONE) &&
                instruction_types(p, i));
        if (i->opcode != 73)
          REQUIRE(claim(p, i->destination, f));
      }
      for (uint32_t k = 0; k < block->phi_edge_count; k++) {
        const CSOsdiPrecisionPhiEdge *edge = &p->phi[block->first_phi_edge + k];
        REQUIRE(!edge->reserved && edge->predecessor_block >= q->first_block &&
                edge->predecessor_block - q->first_block < q->block_count &&
                type(p, edge->source) == type(p, edge->destination) &&
                type(p, edge->source));
        if (p->owners[edge->destination] == NONE) {
          REQUIRE(claim(p, edge->destination, f));
          p->phi_blocks[edge->destination] = b;
        } else
          REQUIRE(p->owners[edge->destination] == f &&
                  p->phi_blocks[edge->destination] == b);
        for (uint32_t x = 0; x < k; x++) {
          const CSOsdiPrecisionPhiEdge *previous =
              &p->phi[block->first_phi_edge + x];
          REQUIRE(previous->destination != edge->destination ||
                  previous->predecessor_block != edge->predecessor_block);
        }
      }
    }
  }
  p->input_simulation = array(h->inputs.count, 4);
  memset(p->input_simulation, 255, (size_t)h->inputs.count * 4);
  for (uint32_t j = 0; j < h->inputs.count; j++) {
    const CSOsdiPrecisionInput *q = &p->inputs[j];
    uint32_t t = type(p, q->destination);
    REQUIRE(q->function_index < 3 && !q->reserved && q->kind >= 1 &&
            q->kind <= 12 && claim(p, q->destination, q->function_index));
    switch (q->kind) {
    case 1:
    case 2:
      REQUIRE(q->index < h->parameters.count &&
              t == (q->kind == 2 ? 3 : p->parameters[q->index].type));
      REQUIRE(q->function_index || p->parameters[q->index].scope == 1);
      if (q->kind == 1 && q->aux != NONE)
        REQUIRE(q->aux < h->roots.count &&
                p->roots[q->aux].function_index < q->function_index &&
                type(p, p->roots[q->aux].value) == t);
      break;
    case 3:
    case 4:
      REQUIRE(t == 1 && !q->index && !q->aux);
      break;
    case 5:
    case 6:
      REQUIRE(t == 3 && !q->index && !q->aux);
      break;
    case 7:
      REQUIRE(t == 3 && !q->aux);
      break;
    case 8:
    case 9:
    case 12:
      REQUIRE(text(q->index, q->aux, h->strings.count) &&
              (q->kind == 8 || t == 3));
      for (uint32_t k = 0; k < s->simulation_facts.count; k++) {
        const CSOsdiPrecisionSimulationFact *fact = &p->simulation[k];
        if (string_equal(p->strings + q->index, q->aux,
                         p->setup_strings + fact->name_offset,
                         fact->name_bytes)) {
          p->input_simulation[j] = k;
          if (q->kind == 8)
            REQUIRE(t == fact->type);
        }
      }
      break;
    case 10:
      REQUIRE(t == 1 && (q->index == NONE || q->index < h->raw_node_count) &&
              (q->aux == NONE || q->aux < h->raw_node_count));
      break;
    case 11:
      REQUIRE(q->index < h->roots.count &&
              p->roots[q->index].function_index < q->function_index &&
              type(p, p->roots[q->index].value) == t);
      break;
    }
  }
  for (uint32_t b = 0; b < h->blocks.count; b++) {
    const CSOsdiPrecisionBlock *block = &p->blocks[b];
    uint32_t f = block->function_index;
    REQUIRE(f < 3);
    REQUIRE(b >= p->functions[f].first_block &&
            b - p->functions[f].first_block < p->functions[f].block_count);
    if (block->terminator == 2)
      REQUIRE(owned(p, block->condition, f));
    for (uint32_t k = 0; k < block->instruction_count; k++) {
      const CSOsdiPrecisionInstruction *i =
          &p->instructions[block->first_instruction + k];
      if (i->opcode != 73)
        REQUIRE(owned(p, i->a, f));
      if (i->b != NONE)
        REQUIRE(owned(p, i->b, f));
      if (i->opcode == 74)
        REQUIRE(owned(p, i->aux, f));
    }
    for (uint32_t k = 0; k < block->phi_edge_count; k++)
      REQUIRE(owned(p, p->phi[block->first_phi_edge + k].source, f));
  }
  for (uint32_t j = 0; j < h->roots.count; j++) {
    const CSOsdiPrecisionRoot *q = &p->roots[j];
    REQUIRE(q->function_index < 3 && !q->reserved &&
            owned(p, q->value, q->function_index) && q->kind >= 1 &&
            q->kind <= 7);
    if (q->kind <= 4)
      REQUIRE(q->function_index == 2 && type(p, q->value) == 1 &&
              q->index < h->raw_node_count &&
              (q->kind <= 2 ? q->aux == NONE : q->aux < h->raw_node_count));
    if (q->kind == 5)
      REQUIRE(q->index < h->parameters.count &&
              q->aux == p->parameters[q->index].scope &&
              type(p, q->value) == p->parameters[q->index].type);
    if (q->kind == 6)
      REQUIRE(type(p, q->value) == 3 && q->index < h->raw_node_count &&
              (q->aux == NONE || q->aux < h->raw_node_count));
    if (q->kind == 7)
      REQUIRE(type(p, q->value) == 1 && q->index == NONE && q->aux == NONE);
    for (uint32_t k = 0; k < j; k++) {
      const CSOsdiPrecisionRoot *r = &p->roots[k];
      REQUIRE(q->function_index != r->function_index || q->kind != r->kind ||
              q->index != r->index || q->aux != r->aux);
    }
  }
  p->predecessors = array(h->blocks.count, sizeof(*p->predecessors));
  p->control_colors = array(h->blocks.count, sizeof(*p->control_colors));
  p->control_nodes = array(h->blocks.count, sizeof(*p->control_nodes));
  p->control_stack = array(h->blocks.count, sizeof(*p->control_stack));
  for (uint32_t j = 0; j < h->blocks.count; j++) {
    const CSOsdiPrecisionBlock *b = &p->blocks[j];
    if (b->terminator)
      p->predecessors[b->then_block]++;
    if (b->terminator == 2)
      p->predecessors[b->else_block]++;
  }
  p->values = array(h->registers.count, sizeof(*p->values));
  initialize_values(p->values, h->registers.count, p->registers,
                    p->maximum_precision);
  p->root_values = array(h->roots.count, sizeof(*p->root_values));
  size_t limb = mpfr_custom_get_size(p->maximum_precision);
  unsigned char *root_limbs = array(h->roots.count, 2 * limb);
  for (uint32_t j = 0; j < h->roots.count; j++) {
    p->root_values[j].type = type(p, p->roots[j].value);
    if (p->root_values[j].type == 1)
      cs_precision_interval_init(&p->root_values[j].real, p->maximum_precision,
                                 root_limbs + 2 * limb * j);
  }
  p->stage = array(p->stage_capacity, sizeof(*p->stage));
  unsigned char *stage_limbs = array(p->stage_capacity, 2 * limb);
  for (uint32_t j = 0; j < p->stage_capacity; j++)
    cs_precision_interval_init(&p->stage[j].real, p->maximum_precision,
                               stage_limbs + 2 * limb * j);
  p->candidate_q = array(h->raw_node_count, sizeof(*p->candidate_q));
  unsigned char *q_limbs = array(h->raw_node_count, 2 * limb);
  for (uint32_t j = 0; j < h->raw_node_count; j++)
    cs_precision_interval_init(&p->candidate_q[j], p->maximum_precision,
                               q_limbs + 2 * limb * j);
  cs_precision_math_init(&p->math, p->maximum_precision,
                         control->maximum_work_units);
  cs_precision_identity_open(&p->identity, h->registers.count,
                             p->maximum_precision);
  if (p->range_mode) {
    p->range_values = array(h->registers.count, sizeof(*p->range_values));
    p->range_roots = array(h->roots.count, sizeof(*p->range_roots));
    p->range_stage = array(p->stage_capacity, sizeof(*p->range_stage));
    p->range_coordinates = array((size_t)p->active_count + 1,
                                  sizeof(*p->range_coordinates));
    unsigned char *range_limbs = array((size_t)p->active_count + 1, 2 * limb);
    for (uint64_t j = 0; j <= p->active_count; j++)
      cs_precision_interval_init(&p->range_coordinates[j], p->maximum_precision,
                                 range_limbs + 2 * limb * j);
  }
  p->detail = NONE;
  return 0;
}
/* The fixed arena owns every region for the worker lifetime. Integer address
   ranges avoid subtracting pointers from independent allocations. No lookup
   accepts an interior/non-value address or lets metadata escape its owner. */
static cs_precision_range_fact *range_fact(cs_precision_program *p,
                                          const cs_precision_value *v) {
  uintptr_t address = (uintptr_t)v;
  const cs_precision_value *bases[] = {p->values, p->root_values, p->stage};
  cs_precision_range_fact *metadata[] = {p->range_values, p->range_roots,
                                        p->range_stage};
  size_t counts[] = {p->header->registers.count, p->header->roots.count,
                     p->stage_capacity};
  for (unsigned j = 0; j < 3; j++) {
    uintptr_t base = (uintptr_t)bases[j];
    if (address >= base && (address - base) % sizeof(*v) == 0 &&
        (address - base) / sizeof(*v) < counts[j])
      return metadata[j] + (address - base) / sizeof(*v);
  }
  _Exit(CS_OSDI_PRECISION_EXIT_INVARIANT);
}
static unsigned char *activity(cs_precision_program *p,
                               const cs_precision_value *v) {
  return &range_fact(p, v)->active;
}
static int range_new_value(cs_precision_program *p, cs_precision_value *v) {
  if (!cs_precision_charge(&p->math, 8))
    return p->math.status;
  if (p->range_next_token == UINT64_MAX)
    return CS_OSDI_PRECISION_NUMERIC_INVARIANT;
  cs_precision_range_fact *fact = range_fact(p, v);
  memset(fact, 0, sizeof(*fact));
  fact->token = ++p->range_next_token;
  return 0;
}
static int copy_value(cs_precision_program *p, cs_precision_value *d,
                      const cs_precision_value *a) {
  if (!a->initialized)
    return BAD;
  if (!cs_precision_charge(&p->math, a->type == 1 ? 3 : 1))
    return p->math.status;
  if (p->range_mode) {
    if (!cs_precision_charge(&p->math, 8))
      return p->math.status;
    *range_fact(p, d) = *range_fact(p, a);
  }
  d->type = a->type;
  d->initialized = 1;
  d->identity = a->identity;
  d->failure = a->failure;
  d->failure_detail = a->failure_detail;
  if (a->failure)
    return 0;
  if (a->type == 1)
    cs_precision_interval_copy(&d->real, &a->real);
  else {
    d->integer = a->integer;
    d->string = a->string;
    d->length = a->length;
  }
  return 0;
}
static int bits_value(cs_precision_program *p, cs_precision_value *v,
                      uint64_t bits, const unsigned char *strings) {
  if (!cs_precision_charge(&p->math, v->type == 1 ? 3 : 1))
    return p->math.status;
  int status = 0;
  if (v->type == 1)
    status = cs_precision_real_bits(&v->real, bits);
  else if (v->type == 4) {
    v->string = strings + (uint32_t)bits;
    v->length = (uint32_t)(bits >> 32);
  } else
    v->integer = (uint32_t)bits;
  if (!status && p->range_mode)
    status = range_new_value(p, v);
  if (!status)
    v->initialized = 1;
  return status;
}
static void reprecision(cs_precision_interval *v, uint32_t precision) {
  void *a = mpfr_custom_get_significand(v->low),
       *b = mpfr_custom_get_significand(v->high);
  mpfr_custom_init_set(v->low, MPFR_NAN_KIND, 0, precision, a);
  mpfr_custom_init_set(v->high, MPFR_NAN_KIND, 0, precision, b);
}
static int pair(cs_precision_program *p, cs_precision_interval *v, double high,
                double low) {
  if (!isfinite(high) || !isfinite(low))
    return CS_OSDI_PRECISION_INVALID_INPUT;
  if (!cs_precision_charge(&p->math, 6))
    return p->math.status;
  mpfr_set_d(v->low, high, MPFR_RNDD);
  mpfr_set_d(p->math.temporary[0], low, MPFR_RNDD);
  mpfr_add(v->low, v->low, p->math.temporary[0], MPFR_RNDD);
  mpfr_set_d(v->high, high, MPFR_RNDU);
  mpfr_set_d(p->math.temporary[0], low, MPFR_RNDU);
  mpfr_add(v->high, v->high, p->math.temporary[0], MPFR_RNDU);
  return 0;
}
static int fact_given(cs_precision_program *p, uint32_t index) {
  uint32_t f = p->fact_index[index];
  return f != NONE && p->facts[f].given;
}
static int name(cs_precision_program *p, const CSOsdiPrecisionInput *input,
                const char *s) {
  return string_equal(p->strings + input->index, input->aux,
                      (const unsigned char *)s, (uint32_t)strlen(s));
}
static int bind_input(cs_precision_program *p, uint32_t ordinal,
                      const double *high, const double *low,
                      const cs_osdi_precision_context_v1 *context) {
  const CSOsdiPrecisionInput *q = &p->inputs[ordinal];
  cs_precision_value *v = &p->values[q->destination];
  int init = context->mode == CS_OSDI_PRECISION_TRANSIENT_INITIALIZATION;
  if (q->kind == 11)
    return copy_value(p, v, &p->root_values[q->index]);
  if (q->kind == 1) {
    if (fact_given(p, q->index))
      return bits_value(p, v, p->facts[p->fact_index[q->index]].bits,
                        p->setup_strings);
    const CSOsdiPrecisionParameter *parameter = &p->parameters[q->index];
    if (parameter->flags & CS_OSDI_PRECISION_HAS_PRIMITIVE_DEFAULT) {
      uint32_t peer = p->model_peers[q->index];
      if (parameter->scope == 2 && peer != NONE && fact_given(p, peer))
        return bits_value(p, v, p->facts[p->fact_index[peer]].bits,
                          p->setup_strings);
      return bits_value(p, v, parameter->primitive_default_bits, p->strings);
    }
    if (q->aux != NONE)
      return copy_value(p, v, &p->root_values[q->aux]);
    return bits_value(p, v, 0, p->setup_strings);
  }
  if (q->kind == 2) {
    v->integer = fact_given(p, q->index);
    if (q->function_index && p->parameters[q->index].scope == 2) {
      uint32_t peer = p->model_peers[q->index];
      if (peer != NONE)
        v->integer |= fact_given(p, peer);
    }
  } else if (q->kind == 3)
    return bits_value(p, v, p->setup->temperature_binary64, p->setup_strings);
  else if (q->kind == 4) {
    v->initialized = 1;
    if (!p->range_mode)
      return pair(p, &v->real, context->time_high, context->time_low);
    int status = range_new_value(p, v);
    if (status)
      return status;
    if (!cs_precision_charge(&p->math, 2))
      return p->math.status;
    cs_precision_interval_copy(&v->real, &p->range_coordinates[p->active_count]);
    *activity(p, v) = 1;
    v->identity = 0;
    return 0;
  } else if (q->kind == 5)
    v->integer = !init;
  else if (q->kind == 6)
    v->integer = 0;
  else if (q->kind == 7)
    v->integer = q->index < p->setup->connected_terminal_count;
  else if (q->kind == 8 || q->kind == 12) {
    uint32_t index = p->input_simulation[ordinal];
    if (q->kind == 12)
      v->integer = index != NONE && p->simulation[index].given;
    else
      return bits_value(p, v,
                        index != NONE && p->simulation[index].given
                            ? p->simulation[index].bits
                            : 0,
                        p->setup_strings);
  } else if (q->kind == 9)
    v->integer = name(p, q, "tran") ||
                 (init && (name(p, q, "ic") || name(p, q, "static")));
  else if (q->kind == 10) {
    cs_precision_interval *a = &p->math.scratch[4], *b = &p->math.scratch[5];
    uint32_t ai = q->index == NONE ? NONE : p->map[q->index],
             bi = q->aux == NONE ? NONE : p->map[q->aux];
    if (p->range_mode) {
      if (ai == bi) {
        int status = bits_value(p, v, 0, p->setup_strings);
        if (!status && q->index != q->aux) {
          /* Raw partials require source regularity even at prepared aliases. */
          *activity(p, v) = 1;
          v->identity = 0;
        }
        return status;
      }
      int status = range_new_value(p, v);
      if (status)
        return status;
      if (!cs_precision_charge(&p->math, 6))
        return p->math.status;
      if (ai == NONE) {
        mpfr_set_zero(a->low, 0);
        mpfr_set_zero(a->high, 0);
      } else {
        cs_precision_interval_copy(a, &p->range_coordinates[ai]);
      }
      if (bi == NONE) {
        mpfr_set_zero(b->low, 0);
        mpfr_set_zero(b->high, 0);
      } else {
        cs_precision_interval_copy(b, &p->range_coordinates[bi]);
      }
      mpfr_sub(v->real.low, a->low, b->high, MPFR_RNDD);
      mpfr_sub(v->real.high, a->high, b->low, MPFR_RNDU);
      v->initialized = 1;
      v->identity = 0;
      *activity(p, v) = 1;
      return 0;
    }
    int status =
        pair(p, a, ai == NONE ? 0 : high[ai], ai == NONE ? 0 : low[ai]);
    if (status)
      return status;
    status = pair(p, b, bi == NONE ? 0 : high[bi], bi == NONE ? 0 : low[bi]);
    if (status)
      return status;
    if (!cs_precision_charge(&p->math, 2))
      return p->math.status;
    mpfr_sub(v->real.low, a->low, b->high, MPFR_RNDD);
    mpfr_sub(v->real.high, a->high, b->low, MPFR_RNDU);
  }
  v->initialized = 1;
  if (p->range_mode) {
    int status = range_new_value(p, v);
    if (status)
      return status;
  }
  return 0;
}
static uint32_t representative(cs_precision_program *p, uint32_t node) {
  while (p->topology[node] != node)
    node = p->topology[node];
  return node;
}
static int check_topology(cs_precision_program *p) {
  uint32_t n = p->header->raw_node_count;
  for (uint32_t j = 0; j <= n; j++)
    p->topology[j] = j;
  for (uint32_t j = 0; j < p->header->roots.count; j++) {
    const CSOsdiPrecisionRoot *r = &p->roots[j];
    if (r->kind != 6 || !p->root_values[j].initialized ||
        !p->root_values[j].integer)
      continue;
    uint32_t a = representative(p, r->index),
             b = representative(p, r->aux == NONE ? n : r->aux);
    if (a == n)
      p->topology[b] = n;
    else if (b == n)
      p->topology[a] = n;
    else if (a < b)
      p->topology[b] = a;
    else
      p->topology[a] = b;
  }
  for (uint32_t j = 0; j <= n; j++)
    p->dense[j] = NONE;
  uint32_t active = 0;
  for (uint32_t j = 0; j < n; j++) {
    uint32_t r = representative(p, j), mapped = NONE;
    if (r != n) {
      if (p->dense[r] == NONE)
        p->dense[r] = active++;
      mapped = p->dense[r];
    }
    if (mapped != p->map[j])
      return CS_OSDI_PRECISION_INVALID_INPUT;
  }
  return active == p->active_count ? 0 : CS_OSDI_PRECISION_INVALID_INPUT;
}
static int poison(cs_precision_program *p, cs_precision_value *value,
                  uint32_t failure, uint32_t detail) {
  if (!cs_precision_charge(&p->math, 1))
    return p->math.status;
  value->initialized = 1;
  value->identity = 0;
  value->failure = failure;
  value->failure_detail = detail;
  if (p->range_mode) {
    if (!cs_precision_charge(&p->math, 8))
      return p->math.status;
    cs_precision_range_fact *fact = range_fact(p, value);
    memset(fact, 0, sizeof(*fact));
    fact->active = 1;
  }
  return 0;
}
enum {
  RANGE_SHAPE_NONE = 0,
  RANGE_SHAPE_SQUARE = 1,
  RANGE_SHAPE_SQUARE_PLUS_POSITIVE = 2,
  RANGE_SHAPE_POSITIVE_ROOT = 3
};
static int finite_real(const cs_precision_value *v) {
  return v->initialized && !v->failure && v->type == 1 &&
         mpfr_number_p(v->real.low) && mpfr_number_p(v->real.high);
}
static int fixed_positive(const cs_precision_value *v,
                           const cs_precision_range_fact *fact) {
  return !fact->active && finite_real(v) &&
         mpfr_equal_p(v->real.low, v->real.high) &&
         mpfr_sgn(v->real.low) > 0;
}
static void copy_shape(cs_precision_range_fact *d,
                       const cs_precision_range_fact *a) {
  d->shape = a->shape;
  d->x_token = a->x_token;
  d->c_token = a->c_token;
  d->x_register = a->x_register;
  d->c_register = a->c_register;
}
static int current_token(cs_precision_program *p, uint32_t index,
                          uint64_t token) {
  return token && index < p->header->registers.count &&
         finite_real(&p->values[index]) &&
         range_fact(p, &p->values[index])->token == token;
}
static int range_prove(cs_precision_program *p,
                        const CSOsdiPrecisionInstruction *i,
                        const cs_precision_value *a,
                        const cs_precision_value *b,
                        const cs_precision_range_fact *af,
                        const cs_precision_range_fact *bf) {
  if (!cs_precision_charge(&p->math, 16))
    return p->math.status;
  cs_precision_value *destination = &p->values[i->destination];
  cs_precision_range_fact *df = range_fact(p, destination);
  if (i->opcode == 12) {
    *df = *af;
    return 0;
  }
  if (i->opcode == 46 && finite_real(a) && finite_real(b)) {
    /* Only an inactive source zero annihilates dependence. Domain/poison and
       nonfinite checks have already succeeded; active alias zeros do not qualify. */
    if ((!af->active && mpfr_zero_p(a->real.low) && mpfr_zero_p(a->real.high)) ||
        (!bf->active && mpfr_zero_p(b->real.low) && mpfr_zero_p(b->real.high)))
      df->active = 0;
    if (af->token && af->token == bf->token) {
      df->shape = RANGE_SHAPE_SQUARE;
      df->x_token = af->token;
      df->x_register = i->a;
    }
  } else if (i->opcode == 13 &&
             af->shape == RANGE_SHAPE_SQUARE_PLUS_POSITIVE) {
    copy_shape(df, af);
    df->shape = RANGE_SHAPE_POSITIVE_ROOT;
  } else if (i->opcode == 44) {
    const cs_precision_range_fact *square = NULL;
    uint32_t constant = NONE;
    if (af->shape == RANGE_SHAPE_SQUARE && fixed_positive(b, bf)) {
      square = af;
      constant = i->b;
    } else if (bf->shape == RANGE_SHAPE_SQUARE && fixed_positive(a, af)) {
      square = bf;
      constant = i->a;
    }
    if (square) {
      copy_shape(df, square);
      df->shape = RANGE_SHAPE_SQUARE_PLUS_POSITIVE;
      df->c_register = constant;
      df->c_token = range_fact(p, &p->values[constant])->token;
    }
    const cs_precision_range_fact *root = NULL;
    const cs_precision_value *x = NULL;
    if (af->shape == RANGE_SHAPE_POSITIVE_ROOT && bf->token == af->x_token) {
      root = af;
      x = b;
    } else if (bf->shape == RANGE_SHAPE_POSITIVE_ROOT &&
               af->token == bf->x_token) {
      root = bf;
      x = a;
    }
    if (root && current_token(p, root->x_register, root->x_token) &&
        current_token(p, root->c_register, root->c_token)) {
      const cs_precision_value *c = &p->values[root->c_register];
      if (fixed_positive(c, range_fact(p, c)))
        return cs_precision_positive_sum_refine(&p->math, &destination->real,
                                                &x->real, &c->real);
    }
  }
  return 0;
}
/* Return one only after proving every path reaches join. Scratch is bounded by
   the validated function's block count; no recursive C stack is used. */
static int pure_paths(cs_precision_program *p, uint32_t function_index,
                      uint32_t start, uint32_t join, uint32_t *count) {
  uint32_t depth = 1;
  p->control_stack[0] = (cs_precision_control_frame){start, 0};
  while (depth) {
    if (!cs_precision_charge(&p->math, 1))
      return 0;
    cs_precision_control_frame *frame = &p->control_stack[depth - 1];
    uint32_t node = frame->block;
    if (node == join || (frame->phase == 0 && p->control_colors[node] == 2)) {
      depth--;
      continue;
    }
    const CSOsdiPrecisionBlock *b = &p->blocks[node];
    if (frame->phase == 0) {
      if (p->control_colors[node] == 1 || b->function_index != function_index ||
          b->terminator == 0)
        return 0;
      p->control_colors[node] = 1;
      p->control_nodes[(*count)++] = node;
      for (uint32_t j = 0; j < b->instruction_count; j++) {
        if (!cs_precision_charge(&p->math, 1))
          return 0;
        if (p->instructions[b->first_instruction + j].opcode == 73)
          return 0;
      }
      frame->phase = 1;
      if (depth == p->header->blocks.count)
        return 0;
      p->control_stack[depth++] =
          (cs_precision_control_frame){b->then_block, 0};
    } else if (frame->phase == 1 && b->terminator == 2) {
      frame->phase = 2;
      if (depth == p->header->blocks.count)
        return 0;
      p->control_stack[depth++] =
          (cs_precision_control_frame){b->else_block, 0};
    } else {
      p->control_colors[node] = 2;
      depth--;
    }
  }
  return 1;
}
static int prove_region(cs_precision_program *p, uint32_t function_index,
                        uint32_t branch, uint32_t join, uint32_t *count) {
  const CSOsdiPrecisionFunction *f = &p->functions[function_index];
  if (join < f->first_block || join - f->first_block >= f->block_count ||
      join == branch)
    return 0;
  if (!cs_precision_charge(&p->math, f->block_count))
    return 0;
  memset(p->control_colors + f->first_block, 0,
         f->block_count * sizeof(*p->control_colors));
  *count = 0;
  const CSOsdiPrecisionBlock *root = &p->blocks[branch];
  if (!pure_paths(p, function_index, root->then_block, join, count) ||
      !pure_paths(p, function_index, root->else_block, join, count))
    return 0;
  if (p->control_colors[branch])
    return 0;
  /* A foreign predecessor would make this more than a local pure region. */
  for (uint32_t j = f->first_block; j < f->first_block + f->block_count; j++) {
    const CSOsdiPrecisionBlock *b = &p->blocks[j];
    if (!cs_precision_charge(&p->math, 1 + b->terminator))
      return 0;
    if (j == branch || p->control_colors[j])
      continue;
    if ((b->terminator && p->control_colors[b->then_block]) ||
        (b->terminator == 2 && p->control_colors[b->else_block]))
      return 0;
  }
  return 1;
}
static int defer_control(cs_precision_program *p, uint32_t function_index,
                         uint32_t branch, const cs_precision_value *condition,
                         uint32_t *join_out) {
  const CSOsdiPrecisionBlock *b = &p->blocks[branch];
  uint32_t arms[2] = {b->then_block, b->else_block};
  for (unsigned arm = 0; arm < 2; arm++) {
    uint32_t candidate = arms[arm];
    for (uint32_t steps = 0; steps < p->functions[function_index].block_count;
         steps++) {
      if (!cs_precision_charge(&p->math, 1))
        return p->math.status;
      uint32_t count = 0;
      if (p->predecessors[candidate] >= 2 &&
          prove_region(p, function_index, branch, candidate, &count)) {
        for (uint32_t j = 0; j <= count; j++) {
          const CSOsdiPrecisionBlock *region =
              &p->blocks[j == count ? candidate : p->control_nodes[j]];
          if (j != count)
            for (uint32_t k = 0; k < region->instruction_count; k++) {
              const CSOsdiPrecisionInstruction *i =
                  &p->instructions[region->first_instruction + k];
              int status =
                  poison(p, &p->values[i->destination], condition->failure,
                         condition->failure_detail);
              if (status)
                return status;
            }
          for (uint32_t k = 0; k < region->phi_edge_count; k++) {
            const CSOsdiPrecisionPhiEdge *edge =
                &p->phi[region->first_phi_edge + k];
            int status = poison(p, &p->values[edge->destination],
                                condition->failure, condition->failure_detail);
            if (status)
              return status;
          }
        }
        *join_out = candidate;
        return 0;
      }
      if (p->math.status)
        return p->math.status;
      const CSOsdiPrecisionBlock *next = &p->blocks[candidate];
      if (next->terminator != 1)
        break;
      candidate = next->then_block;
    }
  }
  p->detail = condition->failure_detail;
  return condition->failure;
}
static int chart_step(cs_precision_program *p) {
  if (!p->chart) return 0;
  if (p->chart->steps >= p->chart->request->maximum_steps)
    return CS_PRECISION_CHART_STEP_LIMIT;
  p->chart->steps++;
  return 0;
}
static int chart_predicate(uint32_t op) {
  return (op >= 49 && op <= 64) || op == CS_OSDI_PRECISION_OP_FBCAST;
}
static void chart_operand(cs_precision_program *p, const cs_precision_value *v, uint32_t *type,
                          uint32_t *integer, double *lower, double *upper) {
  if (!v) return;
  *type = v->type;
  if (v->type == 1) {
    *lower = mpfr_get_d(v->real.low, MPFR_RNDD);
    *upper = mpfr_get_d(v->real.high, MPFR_RNDU);
    uint32_t source = range_fact(p, v)->primitive_infinity;
    if (source && mpfr_inf_p(v->real.low) && mpfr_inf_p(v->real.high) &&
        mpfr_sgn(v->real.low) == mpfr_sgn(v->real.high)) {
      *type = mpfr_sgn(v->real.low) < 0 ? CS_OSDI_CHART_NEGATIVE_PRIMITIVE_INFINITY :
                                       CS_OSDI_CHART_POSITIVE_PRIMITIVE_INFINITY;
      *integer = source - 1;
    }
  } else if (v->type == 2 || v->type == 3) *integer = v->integer;
}
static int chart_closure_possible(cs_precision_program *p, uint32_t opcode,
                                 uint32_t ar, uint32_t br, uint32_t choice) {
  const cs_precision_value *a = &p->values[ar], *b = br == NONE ? NULL : &p->values[br];
  /* Infinity sentinels are fixed source primitives, not finite faces that a
     neighboring chart can approach by changing the coordinate box. */
  if ((a->type == 1 && !finite_real(a)) || (b && b->type == 1 && !finite_real(b))) return 0;
  if (opcode == CS_OSDI_PRECISION_OP_FBCAST)
    return choice || (mpfr_cmp_ui(a->real.low, 0) <= 0 && mpfr_cmp_ui(a->real.high, 0) >= 0);
  if (!b || a->type != 1 || b->type != 1) return 0;
  if (opcode == 53 || opcode == 56)
    return choice ? mpfr_cmp(a->real.low, b->real.high) <= 0 : mpfr_cmp(a->real.high, b->real.low) >= 0;
  if (opcode == 54 || opcode == 55)
    return choice ? mpfr_cmp(a->real.high, b->real.low) >= 0 : mpfr_cmp(a->real.low, b->real.high) <= 0;
  if ((opcode == 58 && !choice) || (opcode == 62 && choice)) return 1;
  if (opcode == 58 || opcode == 62)
    return mpfr_cmp(a->real.low, b->real.high) <= 0 && mpfr_cmp(a->real.high, b->real.low) >= 0;
  return 0;
}
static int chart_guard(cs_precision_program *p, uint32_t stage, uint32_t block,
                       uint32_t instruction, uint32_t kind, uint32_t opcode,
                       uint32_t a, uint32_t b, int known, uint32_t *choice) {
  struct cs_precision_chart_execution *chart = p->chart;
  if (!chart) return 0;
  if (p->frontier)
    return source_frontier_event(p, stage, block, instruction, kind, opcode, a, b, choice);
  if (!cs_precision_charge(&p->math, 16)) return p->math.status;
  if (chart->guard_count >= chart->scratch->guard_capacity)
    return CS_PRECISION_CHART_CAPACITY;
  cs_precision_chart_decision decision = {stage, block, instruction, kind, *choice, chart->guard_count};
  uint32_t boundary_extension = 0;
  if (chart->request->mode == CS_PRECISION_CHART_RECORD) {
    if (!known) return CS_OSDI_PRECISION_UNRESOLVED_BRANCH;
  } else {
    if (chart->guard_count >= chart->request->tape_count)
      return CS_PRECISION_CHART_TAPE_MISMATCH;
    const cs_precision_chart_decision *candidate = &chart->request->tape[chart->guard_count];
    if (candidate->stage != stage || candidate->block != block || candidate->instruction != instruction ||
        candidate->kind != kind || candidate->ordinal != chart->guard_count || candidate->choice > 1)
      return CS_PRECISION_CHART_TAPE_MISMATCH;
    if (known && candidate->choice != *choice) {
      if (chart->request->mode != CS_PRECISION_CHART_FORMULA_CLOSURE ||
          kind != CS_PRECISION_CHART_PREDICATE || !chart_closure_possible(p, opcode, a, b, candidate->choice))
        return CS_PRECISION_CHART_TAPE_MISMATCH;
      boundary_extension = 1;
    }
    decision.choice = *choice = candidate->choice;
  }
  cs_precision_chart_guard guard = {0};
  guard.decision = decision; guard.opcode = opcode; guard.a = a; guard.b = b;
  guard.formula_truth_known = (uint32_t)known;
  guard.boundary_extension = boundary_extension;
  chart_operand(p, &p->values[a], &guard.a_type, &guard.a_integer, &guard.a_lower, &guard.a_upper);
  if (b != NONE)
    chart_operand(p, &p->values[b], &guard.b_type, &guard.b_integer, &guard.b_lower, &guard.b_upper);
  chart->scratch->guards[chart->guard_count++] = guard;
  return 0;
}
int cs_precision_cell_valid(const cs_precision_cell_integer *v, uint32_t bits) {
  if (!v || !bits || bits > CS_PRECISION_CELL_BITS || v->negative > 1 || v->count > CS_PRECISION_CELL_LIMBS) return 0;
  if ((!v->count && v->negative) || (v->count && !v->limbs[v->count - 1])) return 0;
  for (uint32_t i = v->count; i < CS_PRECISION_CELL_LIMBS; ++i) if (v->limbs[i]) return 0;
  return !v->count || 64u * (v->count - 1) + 64u - (uint32_t)__builtin_clzll(v->limbs[v->count - 1]) <= bits;
}
int cs_precision_cell_compare(const cs_precision_cell_integer *a, const cs_precision_cell_integer *b) {
  if (a->negative != b->negative) return a->negative ? -1 : 1;
  int result = a->count < b->count ? -1 : a->count > b->count ? 1 : 0;
  for (uint32_t i = a->count; !result && i; --i)
    result = a->limbs[i-1] < b->limbs[i-1] ? -1 : a->limbs[i-1] > b->limbs[i-1] ? 1 : 0;
  return a->negative ? -result : result;
}
static void source_cell_import(mpz_t out, const cs_precision_cell_integer *value) {
  mpz_import(out, value->count, -1, sizeof(uint64_t), 0, 0, value->limbs);
  if (value->negative) mpz_neg(out, out);
}
static void source_cell_export(cs_precision_cell_integer *out, const mpz_t value) {
  memset(out, 0, sizeof(*out)); size_t count = 0;
  out->negative = mpz_sgn(value) < 0;
  mpz_export(out->limbs, &count, -1, sizeof(uint64_t), 0, 0, value);
  out->count = (uint32_t)count;
}
static int source_site_equal(const cs_precision_source_decision *a,
                             const cs_precision_source_decision *b) {
  return a->site.stage == b->site.stage && a->site.block == b->site.block &&
    a->site.instruction == b->site.instruction && a->site.kind == b->site.kind &&
    a->site.ordinal == b->site.ordinal && a->opcode == b->opcode;
}
static int source_event_accept(cs_precision_program *p, cs_precision_source_event *event) {
  struct cs_precision_frontier_execution *f = p->frontier;
  if (!cs_precision_charge(&p->math, sizeof(*event))) return p->math.status;
  if (f->count >= f->scratch->event_capacity) return CS_PRECISION_FRONTIER_CAPACITY;
  event->decision.site.ordinal = f->count;
  int status = 0;
  if (f->count < f->request->prefix_count) {
    const cs_precision_source_decision *given = &f->request->prefix[f->count];
    if (!source_site_equal(given, &event->decision) ||
        !cs_precision_cell_valid(&given->cell, f->request->maximum_integer_bits))
      return CS_PRECISION_PREFIX_MALFORMED;
    if (event->decision.site.kind == CS_PRECISION_SOURCE_CELL) {
      if (given->site.choice) return CS_PRECISION_PREFIX_MALFORMED;
      event->decision.cell = given->cell;
      if (cs_precision_cell_compare(&given->cell, &event->first_cell) < 0 ||
          cs_precision_cell_compare(&given->cell, &event->last_cell) > 0) status = CS_PRECISION_PREFIX_EXCLUDED;
    } else {
      if (given->cell.count || given->cell.negative || given->site.choice > 3 ||
          ((event->decision.site.kind == CS_PRECISION_SOURCE_RELATION ||
            event->decision.site.kind == CS_PRECISION_SOURCE_SIGN) && !given->site.choice) ||
          (event->decision.site.kind <= CS_PRECISION_CHART_SELECT && given->site.choice > 1))
        return CS_PRECISION_PREFIX_MALFORMED;
      event->decision.site.choice = given->site.choice;
      if (!(event->allowed_mask & (1u << given->site.choice))) status = CS_PRECISION_PREFIX_EXCLUDED;
    }
  } else if (event->decision.site.kind == CS_PRECISION_SOURCE_CELL) {
    if (event->cell_count == 1) event->decision.cell = event->first_cell;
    else status = CS_PRECISION_FRONTIER_STOP;
  } else if (event->allowed_mask && !(event->allowed_mask & (event->allowed_mask - 1))) {
    event->decision.site.choice = (uint32_t)__builtin_ctz(event->allowed_mask);
  } else status = CS_PRECISION_FRONTIER_STOP;
  if (status == CS_PRECISION_FRONTIER_STOP) {
    event->decision.site.choice = 0;
    memset(&event->decision.cell, 0, sizeof(event->decision.cell));
  }
  event->operands.decision = event->decision.site;
  f->scratch->events[f->count++] = *event;
  return status;
}
static int source_frontier_event(cs_precision_program *p, uint32_t stage, uint32_t block,
    uint32_t instruction, uint32_t kind, uint32_t opcode, uint32_t ar, uint32_t br, uint32_t *choice) {
  cs_precision_source_event event = {0};
  event.decision.site = (cs_precision_chart_decision){stage, block, instruction, kind, *choice, 0};
  event.decision.opcode = opcode;
  event.operands.opcode = opcode; event.operands.a = ar; event.operands.b = br;
  const cs_precision_value *a = &p->values[ar], *b = br == NONE ? NULL : &p->values[br];
  chart_operand(p, a, &event.operands.a_type, &event.operands.a_integer, &event.operands.a_lower, &event.operands.a_upper);
  chart_operand(p, b, &event.operands.b_type, &event.operands.b_integer, &event.operands.b_lower, &event.operands.b_upper);
  if ((event.operands.a_type == 1 && (!isfinite(event.operands.a_lower) || !isfinite(event.operands.a_upper))) ||
      (event.operands.b_type == 1 && (!isfinite(event.operands.b_lower) || !isfinite(event.operands.b_upper))))
    return CS_OSDI_PRECISION_NONFINITE_PROJECTION;
  if (kind == CS_PRECISION_CHART_PREDICATE && b && a->type == 1 && b->type == 1) {
    event.decision.site.kind = CS_PRECISION_SOURCE_RELATION;
    int same = a == b || (a->identity && a->identity == b->identity) ||
      (range_fact(p, a)->token && range_fact(p, a)->token == range_fact(p, b)->token);
    if (same) event.allowed_mask = 1u << CS_PRECISION_SOURCE_EQUAL;
    else {
      if (mpfr_cmp(a->real.low, b->real.high) < 0) event.allowed_mask |= 1u << CS_PRECISION_SOURCE_LESS;
      if (mpfr_cmp(a->real.low, b->real.high) <= 0 && mpfr_cmp(a->real.high, b->real.low) >= 0)
        event.allowed_mask |= 1u << CS_PRECISION_SOURCE_EQUAL;
      if (mpfr_cmp(a->real.high, b->real.low) > 0) event.allowed_mask |= 1u << CS_PRECISION_SOURCE_GREATER;
    }
  } else if (kind == CS_PRECISION_CHART_PREDICATE && opcode == CS_OSDI_PRECISION_OP_FBCAST) {
    event.decision.site.kind = CS_PRECISION_SOURCE_SIGN;
    if (mpfr_cmp_ui(a->real.low, 0) < 0) event.allowed_mask |= 1u << CS_PRECISION_SOURCE_LESS;
    if (mpfr_cmp_ui(a->real.low, 0) <= 0 && mpfr_cmp_ui(a->real.high, 0) >= 0)
      event.allowed_mask |= 1u << CS_PRECISION_SOURCE_EQUAL;
    if (mpfr_cmp_ui(a->real.high, 0) > 0) event.allowed_mask |= 1u << CS_PRECISION_SOURCE_GREATER;
  } else event.allowed_mask = 1u << *choice;
  if (!event.allowed_mask) return CS_OSDI_PRECISION_NUMERIC_INVARIANT;
  int status = source_event_accept(p, &event);
  if (status) return status;
  uint32_t selected = event.decision.site.choice;
  if (event.decision.site.kind == CS_PRECISION_SOURCE_SIGN) *choice = selected != CS_PRECISION_SOURCE_EQUAL;
  else if (event.decision.site.kind == CS_PRECISION_SOURCE_RELATION) {
    switch (opcode) {
    case 53: *choice = selected == CS_PRECISION_SOURCE_LESS; break;
    case 54: *choice = selected == CS_PRECISION_SOURCE_GREATER; break;
    case 55: *choice = selected != CS_PRECISION_SOURCE_LESS; break;
    case 56: *choice = selected != CS_PRECISION_SOURCE_GREATER; break;
    case 58: *choice = selected == CS_PRECISION_SOURCE_EQUAL; break;
    case 62: *choice = selected != CS_PRECISION_SOURCE_EQUAL; break;
    default: return CS_OSDI_PRECISION_NUMERIC_INVARIANT;
    }
  } else *choice = selected;
  return 0;
}
static int source_cell_operation(cs_precision_program *p, uint32_t stage, uint32_t block,
    uint32_t instruction, const CSOsdiPrecisionInstruction *op, cs_precision_value *destination) {
  const cs_precision_interval *a = &p->values[op->a].real;
  const cs_precision_frontier_request *request = p->frontier->request;
  if (!mpfr_number_p(a->low) || !mpfr_number_p(a->high)) return CS_OSDI_PRECISION_DOMAIN;
  if ((!mpfr_zero_p(a->low) && mpfr_get_exp(a->low) > (mpfr_exp_t)request->maximum_integer_bits) ||
      (!mpfr_zero_p(a->high) && mpfr_get_exp(a->high) > (mpfr_exp_t)request->maximum_integer_bits))
    return CS_PRECISION_CELL_PRECISION;
  if (!cs_precision_charge(&p->math, 2048 + 16 * (uint64_t)request->maximum_integer_bits)) return p->math.status;
  mpz_t first, last, count, chosen;
  mpz_inits(first, last, count, chosen, NULL);
  if (op->opcode == 20) {
    mpfr_get_z(first, a->low, MPFR_RNDD); mpfr_get_z(last, a->high, MPFR_RNDD);
  } else if (op->opcode == 21) {
    mpfr_get_z(first, a->low, MPFR_RNDU); mpfr_get_z(last, a->high, MPFR_RNDU);
  } else {
    if (op->opcode == 5) {
      mpfr_round(p->math.temporary[8], a->low); mpfr_round(p->math.temporary[9], a->high);
    } else {
      mpfr_trunc(p->math.temporary[8], a->low); mpfr_trunc(p->math.temporary[9], a->high);
    }
    mpfr_get_z(first, p->math.temporary[8], MPFR_RNDN); mpfr_get_z(last, p->math.temporary[9], MPFR_RNDN);
  }
  int status = 0;
  if (mpz_sizeinbase(first, 2) > request->maximum_integer_bits || mpz_sizeinbase(last, 2) > request->maximum_integer_bits)
    status = CS_PRECISION_CELL_PRECISION;
  else if ((op->opcode == 5 || op->opcode == 6) &&
      (mpz_cmp_si(first, INT32_MIN) < 0 || mpz_cmp_si(last, INT32_MAX) > 0)) status = CS_OSDI_PRECISION_DOMAIN;
  mpz_sub(count, last, first); mpz_add_ui(count, count, 1);
  if (!status && (!mpz_fits_ulong_p(count) || mpz_cmp_ui(count, request->maximum_cells) > 0)) status = CS_PRECISION_FRONTIER_CAPACITY;
  cs_precision_source_event event = {0};
  if (!status) {
    event.decision.site = (cs_precision_chart_decision){stage, block, instruction, CS_PRECISION_SOURCE_CELL, 0, 0};
    event.decision.opcode = op->opcode; event.cell_count = mpz_get_ui(count);
    source_cell_export(&event.first_cell, first); source_cell_export(&event.last_cell, last);
    event.operands.opcode = op->opcode; event.operands.a = op->a; event.operands.b = NONE;
    chart_operand(p, &p->values[op->a], &event.operands.a_type, &event.operands.a_integer, &event.operands.a_lower, &event.operands.a_upper);
    if (!isfinite(event.operands.a_lower) || !isfinite(event.operands.a_upper)) status = CS_OSDI_PRECISION_NONFINITE_PROJECTION;
    if (!status) status = source_event_accept(p, &event);
    if (!status) {
      source_cell_import(chosen, &event.decision.cell);
      if (destination->type == 2) destination->integer = (uint32_t)mpz_get_si(chosen);
      else {
        mpfr_set_z(destination->real.low, chosen, MPFR_RNDD);
        mpfr_set_z(destination->real.high, chosen, MPFR_RNDU);
      }
      destination->initialized = 1;
    }
  }
  mpz_clears(first, last, count, chosen, NULL);
  return status;
}
static int observe_source(cs_precision_program *p, uint32_t kind,
                          uint32_t index, uint32_t source) {
  return p->source_observer ? p->source_observer->observe(
      p->source_observer->context, p, kind, index, source) : 0;
}
static int function(cs_precision_program *p, uint32_t index, const double *high,
                    const double *low,
                    const cs_osdi_precision_context_v1 *context) {
  const CSOsdiPrecisionFunction *f = &p->functions[index];
  p->detail = NONE;
  for (uint32_t j = 0; j < p->header->inputs.count; j++)
    if (p->inputs[j].function_index == index) {
      if (p->range_mode && !cs_precision_charge(&p->math, 8))
        return p->math.status;
      int status = bind_input(p, j, high, low, context);
      if (status)
        return status;
      status = observe_source(p, CS_SOURCE_INPUT, j, 0);
      if (status) return status;
      cs_precision_value *v = &p->values[p->inputs[j].destination];
      if (!v->identity && !v->failure &&
          !(p->range_mode && *activity(p, v))) {
        status = cs_precision_identify(&p->identity, &p->math, v, 0, 0, 0);
        if (status)
          return status;
      }
    }
  uint32_t block = f->entry_block, previous = NONE, poisoned_join = NONE;
  for (;;) {
    int chart_status = chart_step(p);
    if (chart_status) return chart_status;
    const CSOsdiPrecisionBlock *b = &p->blocks[block];
    if (!cs_precision_charge(&p->math, 1))
      return p->math.status;
    uint32_t staged = 0;
    uint32_t phi_count = poisoned_join == block ? 0 : b->phi_edge_count;
    poisoned_join = NONE;
    for (uint32_t j = 0; j < phi_count; j++) {
      const CSOsdiPrecisionPhiEdge *edge = &p->phi[b->first_phi_edge + j];
      if (edge->predecessor_block == previous) {
        int status = observe_source(p, CS_SOURCE_PHI_STAGE, staged, edge->source);
        if (status) return status;
        status = copy_value(p, &p->stage[staged++], &p->values[edge->source]);
        if (status)
          return status;
      }
    }
    for (uint32_t j = 0; j < phi_count; j++) {
      const CSOsdiPrecisionPhiEdge *edge = &p->phi[b->first_phi_edge + j];
      int first = 1;
      for (uint32_t k = 0; k < j; k++)
        if (p->phi[b->first_phi_edge + k].destination == edge->destination)
          first = 0;
      if (first) {
        int found = 0;
        for (uint32_t k = 0; k < b->phi_edge_count; k++)
          if (p->phi[b->first_phi_edge + k].destination == edge->destination &&
              p->phi[b->first_phi_edge + k].predecessor_block == previous)
            found = 1;
        if (!found)
          return BAD;
      }
    }
    staged = 0;
    for (uint32_t j = 0; j < phi_count; j++) {
      const CSOsdiPrecisionPhiEdge *edge = &p->phi[b->first_phi_edge + j];
      if (edge->predecessor_block == previous) {
        int status = observe_source(p, CS_SOURCE_PHI_ASSIGN, edge->destination, staged);
        if (status) return status;
        status = copy_value(p, &p->values[edge->destination], &p->stage[staged++]);
        if (status)
          return status;
      }
    }
    for (uint32_t j = 0; j < b->instruction_count; j++) {
      chart_status = chart_step(p);
      if (chart_status) return chart_status;
      if (p->range_mode && !cs_precision_charge(&p->math, 8))
        return p->math.status;
      p->detail = b->first_instruction + j;
      const CSOsdiPrecisionInstruction *i = &p->instructions[p->detail];
      if (i->opcode == 73) {
        p->flags = i->a;
        return CS_OSDI_PRECISION_DOMAIN;
      }
      if (!p->values[i->a].initialized)
        return BAD;
      cs_precision_value *destination = &p->values[i->destination];
      if (i->opcode == 74) {
        const cs_precision_value *condition = &p->values[i->a];
        int status;
        if (condition->failure)
          status = poison(p, destination, condition->failure,
                          condition->failure_detail);
        else {
          uint32_t choice = condition->integer;
          status = chart_guard(p, index + 1, block, p->detail, CS_PRECISION_CHART_SELECT,
                               i->opcode, i->a, NONE, 1, &choice);
          if (status) return status;
          status = copy_value(p, destination,
                              &p->values[condition->integer ? i->b : i->aux]);
        }
        if (status)
          return status;
        status = observe_source(p, CS_SOURCE_INSTRUCTION, p->detail, 0);
        if (status) return status;
        continue;
      }
      if (i->b != NONE && !p->values[i->b].initialized)
        return BAD;
      const cs_precision_value *invalid =
          p->values[i->a].failure                   ? &p->values[i->a]
          : i->b != NONE && p->values[i->b].failure ? &p->values[i->b]
                                                    : NULL;
      if (invalid) {
        int status =
            poison(p, destination, invalid->failure, invalid->failure_detail);
        if (status)
          return status;
        continue;
      }
      const cs_precision_value *a = &p->values[i->a];
      const cs_precision_value *operand_b =
          i->b == NONE ? NULL : &p->values[i->b];
      uint32_t first = a->identity,
               second = operand_b ? operand_b->identity : 0;
      int same = operand_b && first && first == second;
      int active_a = 0, active_b = 0;
      cs_precision_range_fact af, bf;
      if (p->range_mode) {
        af = *range_fact(p, a);
        memset(&bf, 0, sizeof(bf));
        if (operand_b)
          bf = *range_fact(p, operand_b);
        active_a = af.active;
        active_b = bf.active;
        if (active_a || active_b)
          same = operand_b && i->a == i->b;
        int status = range_new_value(p, destination);
        if (status)
          return status;
        *activity(p, destination) = active_a || active_b;
      }
      /* The identity authority is endpoint-local and independent of enclosure
         equality. Comparisons still execute their ordinary domain checks. */
      const cs_precision_value *comparison_b = operand_b;
      if (same && ((i->opcode >= 53 && i->opcode <= 56) || i->opcode == 58 ||
                   i->opcode == 62))
        comparison_b = a;
      destination->identity = 0;
      destination->failure = 0;
      destination->failure_detail = NONE;
      destination->initialized = 0;
      int cell = p->frontier && (i->opcode == 5 || i->opcode == 6 || i->opcode == 20 || i->opcode == 21);
      int status = cell ? source_cell_operation(p, index + 1, block, p->detail, i, destination) : 0;
      if (cell && status && status != CS_OSDI_PRECISION_DOMAIN &&
          status != CS_OSDI_PRECISION_NONSMOOTH && status != CS_OSDI_PRECISION_NONFINITE_PROJECTION) return status;
      if (!cell) status = p->range_mode && !(p->chart && chart_predicate(i->opcode))
                       ? cs_precision_regularity(&p->math, i->opcode, a,
                                                  comparison_b, active_a,
                                                  active_b, same)
                       : 0;
      if (!status && !cell)
        status = cs_precision_apply(&p->math, i->opcode, destination, a,
                                     comparison_b);
      if (p->chart && chart_predicate(i->opcode) &&
          (!status || status == CS_OSDI_PRECISION_UNRESOLVED_BRANCH)) {
        int known = status == 0;
        uint32_t choice = known ? destination->integer : 0;
        status = chart_guard(p, index + 1, block, p->detail, CS_PRECISION_CHART_PREDICATE,
                             i->opcode, i->a, i->b, known, &choice);
        if (status) return status;
        destination->integer = choice; destination->initialized = 1;
      }
      if (!status && destination->type == 1 && i->opcode != 12 &&
          (!mpfr_number_p(destination->real.low) ||
           !mpfr_number_p(destination->real.high)))
        status = CS_OSDI_PRECISION_NONFINITE_PROJECTION;
      if (status == CS_OSDI_PRECISION_DOMAIN ||
          status == CS_OSDI_PRECISION_UNRESOLVED_BRANCH ||
          status == CS_OSDI_PRECISION_NONSMOOTH ||
          status == CS_OSDI_PRECISION_NONFINITE_PROJECTION) {
        int failure = poison(p, destination, status, p->detail);
        if (failure)
          return failure;
        continue;
      }
      if (status)
        return status;
      if (p->range_mode) {
        status = range_prove(p, i, a, operand_b, &af, &bf);
        if (status)
          return status;
      }
      if (same && i->opcode == 45 && mpfr_number_p(a->real.low) &&
          mpfr_number_p(a->real.high)) {
        if (!cs_precision_charge(&p->math, 2))
          return p->math.status;
        mpfr_set_zero(destination->real.low, 0);
        mpfr_set_zero(destination->real.high, 0);
        if (p->range_mode)
          *activity(p, destination) = 0;
      }
      if (p->range_mode &&
          ((i->opcode >= 49 && i->opcode <= 64) || i->opcode == 5 ||
           i->opcode == 6 || i->opcode == 10 || i->opcode == 20 ||
           i->opcode == 21))
        *activity(p, destination) = 0;
      if (i->opcode == 12) {
        destination->identity = first;
      } else if (!(p->range_mode && *activity(p, destination))) {
        status = cs_precision_identify(&p->identity, &p->math, destination,
                                       i->opcode, first, second);
        if (status)
          return status;
      }
      int observation = observe_source(p, CS_SOURCE_INSTRUCTION, p->detail, 0);
      if (observation) return observation;
    }
    p->detail = NONE;
    if (b->terminator == 0)
      break;
    if (b->terminator == 2 && !p->values[b->condition].initialized)
      return BAD;
    if (b->terminator == 2 && p->values[b->condition].failure) {
      int status = defer_control(p, index, block, &p->values[b->condition],
                                 &poisoned_join);
      if (status)
        return status;
      previous = block;
      block = poisoned_join;
      continue;
    }
    if (p->chart && b->terminator == CS_OSDI_PRECISION_BRANCH) {
      uint32_t choice = p->values[b->condition].integer;
      int status = chart_guard(p, index + 1, block, NONE, CS_PRECISION_CHART_BRANCH,
                               CS_OSDI_PRECISION_BRANCH, b->condition, NONE, 1, &choice);
      if (status) return status;
    }
    previous = block;
    block = b->terminator == 1 || p->values[b->condition].integer
                ? b->then_block
                : b->else_block;
  }
  for (uint32_t j = 0; j < p->header->roots.count; j++)
    if (p->roots[j].function_index == index) {
      const cs_precision_value *value = &p->values[p->roots[j].value];
      if (value->failure && p->roots[j].kind != 5) {
        p->detail = value->failure_detail;
        return value->failure;
      }
      int status =
          copy_value(p, &p->root_values[j], &p->values[p->roots[j].value]);
      if (status)
        return status;
      status = observe_source(p, CS_SOURCE_ROOT, j, p->roots[j].value);
      if (status) return status;
    }
  return 0;
}
static int pipeline(cs_precision_program *p, const double *high,
                    const double *low,
                    const cs_osdi_precision_context_v1 *context) {
  int reset_status = observe_source(p, CS_SOURCE_RESET, 0, 0);
  if (reset_status) return reset_status;
  reset_status = cs_precision_identity_reset(&p->identity, &p->math);
  if (reset_status)
    return reset_status;
  if (p->range_mode) {
    uint64_t count = (uint64_t)p->header->registers.count +
                     p->header->roots.count + p->stage_capacity;
    if (!cs_precision_charge(&p->math, count * 8))
      return p->math.status;
    memset(p->range_values, 0,
           p->header->registers.count * sizeof(*p->range_values));
    memset(p->range_roots, 0,
           p->header->roots.count * sizeof(*p->range_roots));
    memset(p->range_stage, 0,
           p->stage_capacity * sizeof(*p->range_stage));
    p->range_next_token = 0;
  }
  for (uint32_t j = 0; j < p->header->registers.count; j++) {
    p->values[j].initialized = 0;
    p->values[j].identity = 0;
    p->values[j].failure = 0;
    p->values[j].failure_detail = NONE;
  }
  for (uint32_t j = 0; j < p->header->roots.count; j++)
    p->root_values[j].initialized = 0;
  for (uint32_t j = 0; j < p->header->constants.count; j++) {
    const CSOsdiPrecisionConstant *c = &p->constants[j];
    cs_precision_value *v = &p->values[c->destination];
    int status = bits_value(p, v, c->bits, p->strings);
    if (!status) status = observe_source(p, CS_SOURCE_CONSTANT, j, 0);
    if (!status && p->range_mode && c->type == 1 &&
        (c->bits == UINT64_C(0x7ff0000000000000) || c->bits == UINT64_C(0xfff0000000000000)))
      range_fact(p, v)->primitive_infinity = j + 1;
    if (!status)
      status = cs_precision_identify(&p->identity, &p->math, v, 0, 0, 0);
    if (status)
      return status;
  }
  for (uint32_t f = 0; f < 3; f++) {
    int status = function(p, f, high, low, context);
    if (status)
      return status;
  }
  /* Published guards are compiler-proved preparation-only, even when serialized
     in the evaluation function. Check all of them before exposing any result.
   */
  return check_topology(p);
}
static int normalized_pair(double high, double low) {
  return isfinite(high) && isfinite(low) && high + low == high;
}
static int ordered_pair(double lh, double ll, double uh, double ul) {
  /* Normalized twofolds have disjoint adjacent high-component cells. Comparing
     highs, then lows, orders their exact dyadic sums without an MPFR precision
     assumption or a rounded binary64 addition. */
  return lh < uh || (lh == uh && ll <= ul);
}
static int range_input(cs_precision_program *p, cs_precision_interval *out,
                       double lh, double ll, double uh, double ul) {
  reprecision(out, p->precision);
  int status = pair(p, out, lh, ll);
  if (status)
    return status;
  cs_precision_interval *upper = &p->math.scratch[7];
  status = pair(p, upper, uh, ul);
  if (status)
    return status;
  if (!cs_precision_charge(&p->math, 1))
    return p->math.status;
  mpfr_set(out->high, upper->high, MPFR_RNDU);
  return 0;
}
static int evaluate_range(cs_precision_program *p,
                          cs_osdi_precision_control_v1 *control,
                          const double *lh, const double *ll,
                          const double *uh, const double *ul,
                          double *outputs[8]) {
  if (!p->chart) { p->math.work = 0; p->math.status = 0; }
  else if (p->math.status) return p->math.status;
  p->precision = 0;
  p->detail = NONE;
  p->flags = 0;
  p->has_bound = 0;
  uint32_t bits = control->requested_precision_bits;
  if (control->protocol_version != CS_OSDI_RANGE_PROTOCOL_VERSION ||
      bits < control->minimum_precision_bits || bits > p->maximum_precision)
    return CS_OSDI_PRECISION_INVALID_INPUT;
  const cs_osdi_precision_context_v1 *lower = &control->candidate,
                                    *upper = &control->reference;
  if (lower->mode != upper->mode || (lower->mode != 2 && lower->mode != 3))
    return CS_OSDI_PRECISION_INVALID_INPUT;
  const cs_osdi_precision_context_v1 *contexts[2] = {lower, upper};
  for (unsigned j = 0; j < 2; j++) {
    const cs_osdi_precision_context_v1 *context = contexts[j];
    if (context->reserved || !normalized_pair(context->time_high, context->time_low) ||
        context->time_high < 0 || (context->time_high == 0 && context->time_low < 0) ||
        (context->mode == 3 && (context->time_high != 0 || context->time_low != 0)))
      return CS_OSDI_PRECISION_INVALID_INPUT;
  }
  if (!ordered_pair(lower->time_high, lower->time_low, upper->time_high,
                    upper->time_low))
    return CS_OSDI_PRECISION_INVALID_INPUT;
  for (uint64_t j = 0; j < p->active_count; j++)
    if (!normalized_pair(lh[j], ll[j]) || !normalized_pair(uh[j], ul[j]) ||
        !ordered_pair(lh[j], ll[j], uh[j], ul[j]))
      return CS_OSDI_PRECISION_INVALID_INPUT;
  p->precision = bits;
  p->math.precision = bits;
  for (unsigned j = 0; j < 8; j++)
    reprecision(&p->math.scratch[j], bits);
  for (unsigned j = 0; j < 12; j++) {
    void *storage = mpfr_custom_get_significand(p->math.temporary[j]);
    mpfr_custom_init_set(p->math.temporary[j], MPFR_NAN_KIND, 0, bits, storage);
  }
  for (uint32_t j = 0; j < p->header->registers.count; j++)
    if (p->values[j].type == 1)
      reprecision(&p->values[j].real, bits);
  for (uint32_t j = 0; j < p->header->roots.count; j++)
    if (p->root_values[j].type == 1)
      reprecision(&p->root_values[j].real, bits);
  for (uint32_t j = 0; j < p->stage_capacity; j++)
    reprecision(&p->stage[j].real, bits);
  for (uint64_t j = 0; j < p->active_count; j++) {
    int status = range_input(p, &p->range_coordinates[j], lh[j], ll[j],
                              uh[j], ul[j]);
    if (status)
      return status;
  }
  int status = range_input(p, &p->range_coordinates[p->active_count],
                            lower->time_high, lower->time_low,
                            upper->time_high, upper->time_low);
  if (status)
    return status;
  uint32_t n = p->header->raw_node_count;
  uint64_t matrix = (uint64_t)n * n;
  if (!cs_precision_charge(&p->math, 4 * (uint64_t)n + 4 * matrix))
    return p->math.status;
  for (unsigned j = 0; j < 8; j++)
    memset(outputs[j], 0, (size_t)(j < 4 ? n : matrix) * sizeof(double));
  status = pipeline(p, NULL, NULL, lower);
  if (status)
    return status;
  for (uint32_t j = 0; j < p->header->roots.count; j++) {
    const CSOsdiPrecisionRoot *root = &p->roots[j];
    const cs_precision_interval *value = &p->root_values[j].real;
    if (root->kind == 1 || root->kind == 2) {
      unsigned region = 2 * (root->kind - 1);
      status = cs_precision_bounds(&p->math, value, &outputs[region][root->index],
                                    &outputs[region + 1][root->index]);
    } else if (root->kind == 3 || root->kind == 4) {
      unsigned region = 4 + 2 * (root->kind - 3);
      uint64_t index = (uint64_t)root->index * n + root->aux;
      status = cs_precision_bounds(&p->math, value, &outputs[region][index],
                                    &outputs[region + 1][index]);
    } else if (root->kind == 7) {
      if (!cs_precision_charge(&p->math, 8))
        return p->math.status;
      if (mpfr_inf_p(value->low) && mpfr_sgn(value->low) > 0 &&
          mpfr_inf_p(value->high))
        continue;
      if (!mpfr_number_p(value->low) || !mpfr_number_p(value->high) ||
          mpfr_cmp_ui(value->low, 0) <= 0)
        return CS_OSDI_PRECISION_DOMAIN;
      double bound = mpfr_get_d(value->low, MPFR_RNDD);
      if (!isfinite(bound) || bound <= 0)
        return CS_OSDI_PRECISION_NONFINITE_PROJECTION;
      if (!p->has_bound || bound < p->bound) {
        p->has_bound = 1;
        p->bound = bound;
      }
    }
    if (status)
      return status;
  }
  return 0;
}
static int chart_product(size_t a, size_t b, size_t *product) {
  if (b && a > SIZE_MAX / b) return 0;
  *product = a * b;
  return 1;
}
int cs_precision_program_evaluate_chart(
    cs_precision_program *p, cs_osdi_precision_control_v1 *control,
    const double *lh, const double *ll, const double *uh, const double *ul,
    const cs_precision_chart_request *request, const cs_precision_chart_scratch *scratch,
    cs_precision_chart_result *result) {
  if (!p || !p->header || !control || !request || !scratch || !result ||
      !p->range_mode || p->chart || request->mode < CS_PRECISION_CHART_RECORD ||
      request->mode > CS_PRECISION_CHART_FORMULA_CLOSURE || !request->maximum_steps ||
      (request->tape_count && !request->tape) ||
      (request->mode == CS_PRECISION_CHART_RECORD && request->tape_count) ||
      (scratch->guard_capacity && !scratch->guards) ||
      (p->active_count && (!lh || !ll || !uh || !ul)) || p->math.work > p->math.limit)
    return CS_OSDI_PRECISION_INVALID_INPUT;
  size_t matrix, bytes;
  size_t n = p->header->raw_node_count;
  if (!chart_product(n, n, &matrix) || !chart_product(matrix, sizeof(double), &bytes))
    return CS_OSDI_PRECISION_INVALID_INPUT;
  if (!chart_product(scratch->guard_capacity, sizeof(cs_precision_chart_guard), &bytes) ||
      !chart_product(request->tape_count, sizeof(cs_precision_chart_decision), &bytes))
    return CS_OSDI_PRECISION_INVALID_INPUT;
  for (unsigned j = 0; j < 8; j++)
    if (!scratch->planes[j] || scratch->plane_capacities[j] < (j < 4 ? n : matrix))
      return CS_PRECISION_CHART_CAPACITY;
  double *planes[8];
  for (unsigned j = 0; j < 8; j++) planes[j] = scratch->planes[j];
  struct cs_precision_chart_execution execution = {request, scratch, 0, 0};
  p->chart = &execution;
  int status = evaluate_range(p, control, lh, ll, uh, ul, planes);
  p->chart = NULL;
  if (status) return status;
  if (request->mode != CS_PRECISION_CHART_RECORD && execution.guard_count != request->tape_count)
    return CS_PRECISION_CHART_TAPE_MISMATCH;
  cs_precision_chart_result completed = {0};
  completed.program = p; completed.guards = scratch->guards;
  completed.guard_count = execution.guard_count; completed.completed_steps = execution.steps;
  completed.semantics = request->mode;
  for (unsigned j = 0; j < 8; j++) completed.planes[j] = scratch->planes[j];
  *result = completed;
  return 0;
}
int cs_precision_program_explore_frontier(cs_precision_program *p,
    cs_osdi_precision_control_v1 *control, const double *lh, const double *ll,
    const double *uh, const double *ul, const cs_precision_frontier_request *request,
    const cs_precision_frontier_scratch *scratch, cs_precision_frontier_result *result) {
  if (!p || !p->header || !control || !request || !scratch || !result || !p->range_mode ||
      p->chart || p->frontier || !request->maximum_steps || !request->maximum_cells ||
      request->maximum_cells > ULONG_MAX || !request->maximum_integer_bits ||
      request->maximum_integer_bits > CS_PRECISION_CELL_BITS ||
      (request->prefix_count && !request->prefix) ||
      (scratch->event_capacity && !scratch->events) || request->prefix_count > scratch->event_capacity ||
      (p->active_count && (!lh || !ll || !uh || !ul)) || p->math.work > p->math.limit)
    return CS_OSDI_PRECISION_INVALID_INPUT;
  size_t matrix, bytes, n = p->header->raw_node_count;
  if (!chart_product(n, n, &matrix) || !chart_product(matrix, sizeof(double), &bytes) ||
      !chart_product(scratch->event_capacity, sizeof(*scratch->events), &bytes) ||
      !chart_product(request->prefix_count, sizeof(*request->prefix), &bytes))
    return CS_OSDI_PRECISION_INVALID_INPUT;
  double *planes[8];
  for (unsigned i = 0; i < 8; ++i) {
    if (!scratch->formulas.planes[i] || scratch->formulas.plane_capacities[i] < (i < 4 ? n : matrix))
      return CS_PRECISION_FRONTIER_CAPACITY;
    planes[i] = scratch->formulas.planes[i];
  }
  cs_precision_chart_request chart_request = {CS_PRECISION_CHART_RECORD, NULL, 0, request->maximum_steps};
  struct cs_precision_chart_execution chart = {&chart_request, &scratch->formulas, 0, 0};
  struct cs_precision_frontier_execution frontier = {request, scratch, 0};
  p->chart = &chart; p->frontier = &frontier;
  int status = evaluate_range(p, control, lh, ll, uh, ul, planes);
  p->frontier = NULL; p->chart = NULL;
  if (scratch->usage) *scratch->usage = (cs_precision_frontier_usage){chart.steps, frontier.count};
  if ((!status && frontier.count < request->prefix_count) ||
      (status == CS_PRECISION_PREFIX_EXCLUDED && frontier.count != request->prefix_count))
    return CS_PRECISION_PREFIX_MALFORMED;
  if (status && status != CS_PRECISION_FRONTIER_STOP && status != CS_PRECISION_PREFIX_EXCLUDED) return status;
  cs_precision_frontier_result completed = {0};
  completed.program = p; completed.events = scratch->events; completed.event_count = frontier.count;
  completed.completed_steps = chart.steps;
  completed.state = status == CS_PRECISION_FRONTIER_STOP ? CS_PRECISION_FRONTIER :
                    status == CS_PRECISION_PREFIX_EXCLUDED ? CS_PRECISION_EXCLUDED : CS_PRECISION_TERMINAL;
  if (!status) for (unsigned i = 0; i < 8; ++i) completed.planes[i] = planes[i];
  *result = completed; return 0;
}
int cs_precision_program_evaluate(cs_precision_program *p,
                                  cs_osdi_precision_control_v1 *control,
                                  const double *ch, const double *cl,
                                  const double *rh, const double *rl,
                                  double *outputs[8]) {
  if (p->range_mode)
    return evaluate_range(p, control, ch, cl, rh, rl, outputs);
  p->math.work = 0;
  p->math.status = 0;
  p->precision = 0;
  p->detail = NONE;
  p->flags = 0;
  p->has_bound = 0;
  uint32_t bits = control->requested_precision_bits;
  if (bits < control->minimum_precision_bits || bits > p->maximum_precision)
    return CS_OSDI_PRECISION_INVALID_INPUT;
  const cs_osdi_precision_context_v1 *contexts[2] = {&control->candidate,
                                                     &control->reference};
  for (unsigned j = 0; j < 2; j++)
    if (contexts[j]->reserved ||
        (contexts[j]->mode != 2 && contexts[j]->mode != 3) ||
        !isfinite(contexts[j]->time_high) || !isfinite(contexts[j]->time_low) ||
        contexts[j]->time_high + contexts[j]->time_low !=
            contexts[j]->time_high)
      return CS_OSDI_PRECISION_INVALID_INPUT;
  for (uint64_t j = 0; j < p->active_count; j++)
    if (!isfinite(ch[j]) || !isfinite(cl[j]) || !isfinite(rh[j]) ||
        !isfinite(rl[j]) || ch[j] + cl[j] != ch[j] || rh[j] + rl[j] != rh[j])
      return CS_OSDI_PRECISION_INVALID_INPUT;
  p->math.precision = bits;
  p->precision = bits;
  for (unsigned j = 0; j < 8; j++)
    reprecision(&p->math.scratch[j], bits);
  for (unsigned j = 0; j < 12; j++) {
    void *storage = mpfr_custom_get_significand(p->math.temporary[j]);
    mpfr_custom_init_set(p->math.temporary[j], MPFR_NAN_KIND, 0, bits, storage);
  }
  for (uint32_t j = 0; j < p->header->registers.count; j++)
    if (p->values[j].type == 1)
      reprecision(&p->values[j].real, bits);
  for (uint32_t j = 0; j < p->header->roots.count; j++)
    if (p->root_values[j].type == 1)
      reprecision(&p->root_values[j].real, bits);
  for (uint32_t j = 0; j < p->stage_capacity; j++)
    reprecision(&p->stage[j].real, bits);
  uint32_t n = p->header->raw_node_count;
  for (uint32_t j = 0; j < n; j++) {
    reprecision(&p->candidate_q[j], bits);
    mpfr_set_zero(p->candidate_q[j].low, 0);
    mpfr_set_zero(p->candidate_q[j].high, 0);
  }
  for (unsigned j = 0; j < 8; j++)
    memset(outputs[j], 0,
           (size_t)(j >= 6 ? (uint64_t)n * n : n) * sizeof(double));
  int status = pipeline(p, ch, cl, &control->candidate);
  if (status)
    return status;
  for (uint32_t j = 0; j < p->header->roots.count; j++) {
    const CSOsdiPrecisionRoot *r = &p->roots[j];
    const cs_precision_interval *v = &p->root_values[j].real;
    if (r->kind == 1) {
      status =
          cs_precision_project(&p->math, v, &outputs[0][r->index],
                               &outputs[1][r->index], &outputs[2][r->index]);
    } else if (r->kind == 2) {
      if (!cs_precision_charge(&p->math, 2))
        return p->math.status;
      cs_precision_interval_copy(&p->candidate_q[r->index], v);
    } else if (r->kind == 3 || r->kind == 4)
      status = cs_precision_midpoint(
          &p->math, v,
          &outputs[r->kind == 3 ? 6 : 7][(uint64_t)r->index * n + r->aux]);
    else if (r->kind == 7) {
      if (!cs_precision_charge(&p->math, 8))
        return p->math.status;
      if (mpfr_inf_p(v->low) && mpfr_sgn(v->low) > 0 && mpfr_inf_p(v->high))
        continue;
      if (!mpfr_number_p(v->low) || !mpfr_number_p(v->high) ||
          mpfr_cmp_ui(v->low, 0) <= 0)
        return CS_OSDI_PRECISION_DOMAIN;
      double bound = mpfr_get_d(v->low, MPFR_RNDD);
      if (!isfinite(bound) || bound <= 0)
        return CS_OSDI_PRECISION_NONFINITE_PROJECTION;
      if (!p->has_bound || bound < p->bound) {
        p->has_bound = 1;
        p->bound = bound;
      }
    }
    if (status)
      return status;
  }
  status = pipeline(p, rh, rl, &control->reference);
  if (status)
    return status;
  for (uint32_t j = 0; j < p->header->roots.count; j++)
    if (p->roots[j].kind == 2) {
      uint32_t row = p->roots[j].index;
      const cs_precision_interval *v = &p->root_values[j].real;
      if (!cs_precision_charge(&p->math, 2))
        return p->math.status;
      mpfr_sub(p->math.scratch[6].low, p->candidate_q[row].low, v->high,
               MPFR_RNDD);
      mpfr_sub(p->math.scratch[6].high, p->candidate_q[row].high, v->low,
               MPFR_RNDU);
      status =
          cs_precision_project(&p->math, &p->math.scratch[6], &outputs[3][row],
                               &outputs[4][row], &outputs[5][row]);
      if (status)
        return status;
    }
  p->detail = NONE;
  return 0;
}
