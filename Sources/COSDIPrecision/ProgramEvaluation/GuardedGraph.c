#include "GuardedGraph.h"
#include <stdint.h>

#define NONE CS_OSDI_PRECISION_NONE
#define BAD CS_OSDI_PRECISION_INVALID_PROGRAM
#define CHARGE(n) do { if (!cs_precision_charge(&p->math, (n))) return p->math.status; } while (0)
#define REQUIRE(c) do { if (!(c)) return BAD; } while (0)

static int define(cs_precision_program *p, cs_precision_graph_definition *d,
                  uint32_t reg, uint32_t kind, uint32_t index, uint32_t block) {
  if (reg >= p->header->registers.count) return 0;
  if (d[reg].kind)
    return kind == CS_PRECISION_GRAPH_PHI && d[reg].kind == kind &&
           d[reg].block == block;
  d[reg] = (cs_precision_graph_definition){kind, index, block, 0, 0, 0, NONE};
  return 1;
}
static int edge_to(const CSOsdiPrecisionBlock *b, uint32_t target) {
  return (b->terminator && b->then_block == target) ||
         (b->terminator == 2 && b->else_block == target);
}
static int propagate(cs_precision_program *p, cs_precision_graph_definition *d,
                     uint32_t reg, uint32_t mask, uint32_t *head) {
  if (reg >= p->header->registers.count || !d[reg].kind) return 0;
  uint32_t added = mask & ~d[reg].root_mask;
  if (added) {
    if (!d[reg].pending_mask) { d[reg].next = *head; *head = reg; }
    d[reg].root_mask |= added;
    d[reg].pending_mask |= added;
  }
  return 1;
}

int cs_precision_guarded_graph_extract(
    cs_precision_program *p,
    cs_precision_graph_definition *d, size_t dc,
    cs_precision_graph_block *s, size_t bc,
    cs_precision_guarded_graph *result) {
  if (!p || !p->header || !result || !d || !s ||
      dc < p->header->registers.count || bc < p->header->blocks.count)
    return CS_OSDI_PRECISION_INVALID_INPUT;
  if (p->math.status) return p->math.status;
  if (p->math.work > p->math.limit) return CS_OSDI_PRECISION_INVALID_INPUT;
  const CSOsdiPrecisionProgramHeader *h = p->header;
  CHARGE((uint64_t)h->registers.count + h->blocks.count);
  for (uint32_t r = 0; r < h->registers.count; r++)
    d[r] = (cs_precision_graph_definition){0, NONE, NONE, 0, 0, 0, NONE};
  for (uint32_t b = 0; b < h->blocks.count; b++)
    s[b] = (cs_precision_graph_block){0, 0, NONE, 0, NONE, 0};
  for (uint32_t b = 0; b < h->blocks.count; b++) {
    CHARGE(1);
    const CSOsdiPrecisionBlock *block = &p->blocks[b];
    if (block->terminator) {
      REQUIRE(block->then_block < h->blocks.count);
      s[block->then_block].incoming_predecessors++;
    }
    if (block->terminator == CS_OSDI_PRECISION_BRANCH && block->else_block != block->then_block) {
      REQUIRE(block->else_block < h->blocks.count);
      s[block->else_block].incoming_predecessors++;
    }
  }
  for (uint32_t i = 0; i < h->constants.count; i++) {
    CHARGE(1);
    REQUIRE(define(p, d, p->constants[i].destination,
                   CS_PRECISION_GRAPH_CONSTANT, i, NONE));
  }
  for (uint32_t i = 0; i < h->inputs.count; i++) {
    CHARGE(1);
    REQUIRE(define(p, d, p->inputs[i].destination,
                   CS_PRECISION_GRAPH_INPUT, i, NONE));
  }
  for (uint32_t b = 0; b < h->blocks.count; b++) {
    const CSOsdiPrecisionBlock *block = &p->blocks[b];
    CHARGE(1);
    for (uint32_t k = 0; k < block->instruction_count; k++) {
      CHARGE(1);
      uint32_t i = block->first_instruction + k;
      const CSOsdiPrecisionInstruction *op = &p->instructions[i];
      if (op->opcode == CS_OSDI_PRECISION_OP_MODEL_STATUS) {
        if (s[b].terminal_instruction == NONE) s[b].terminal_instruction = i;
      } else {
        REQUIRE(define(p, d, op->destination, CS_PRECISION_GRAPH_INSTRUCTION, i, b));
      }
    }
    for (uint32_t k = 0; k < block->phi_edge_count; k++) {
      CHARGE(1);
      uint32_t i = block->first_phi_edge + k;
      const CSOsdiPrecisionPhiEdge *phi = &p->phi[i];
      REQUIRE(phi->predecessor_block < h->blocks.count &&
              p->blocks[phi->predecessor_block].function_index == block->function_index &&
              edge_to(&p->blocks[phi->predecessor_block], b));
      REQUIRE(define(p, d, phi->destination, CS_PRECISION_GRAPH_PHI, i, b));
      for (uint32_t j = 0; j < k; j++) {
        CHARGE(1);
        const CSOsdiPrecisionPhiEdge *other = &p->phi[block->first_phi_edge + j];
        REQUIRE(other->destination != phi->destination ||
                other->predecessor_block != phi->predecessor_block);
      }
      d[phi->destination].phi_sources++;
    }
  }
  for (uint32_t r = 0; r < h->registers.count; r++) {
    CHARGE(1);
    if (d[r].kind == CS_PRECISION_GRAPH_PHI)
      REQUIRE(d[r].phi_sources == s[d[r].block].incoming_predecessors);
  }
  uint32_t reached = 0, cycles = 0, effects = 0;
  for (uint32_t f = 0; f < h->functions.count; f++) {
    uint32_t current = p->functions[f].entry_block;
    REQUIRE(current < h->blocks.count);
    for (;;) {
      CHARGE(1);
      cs_precision_graph_block *state = &s[current];
      const CSOsdiPrecisionBlock *block = &p->blocks[current];
      REQUIRE(block->function_index == f);
      if (!state->color) {
        state->color = 1; state->reachable = 1; reached++;
        if (state->terminal_instruction != NONE) effects++;
      }
      uint32_t count = state->terminal_instruction == NONE ? block->terminator : 0;
      if (state->next_edge < count) {
        uint32_t target = state->next_edge++ == 0 ? block->then_block : block->else_block;
        REQUIRE(target < h->blocks.count && p->blocks[target].function_index == f);
        if (s[target].color == 1) cycles = 1;
        else if (!s[target].color) { s[target].parent = current; current = target; }
      } else {
        state->color = 2;
        if (state->parent == NONE) break;
        current = state->parent;
      }
    }
  }
  uint32_t head = NONE, stage_masks[3] = {0}, stage_pending[3] = {0};
  for (uint32_t r = 0; r < h->roots.count; r++) {
    CHARGE(1);
    REQUIRE(p->roots[r].kind >= 1 && p->roots[r].kind <= 7 && p->roots[r].function_index < 3);
    uint32_t mask = 1u << p->roots[r].kind;
    REQUIRE(propagate(p, d, p->roots[r].value, mask, &head));
    stage_masks[p->roots[r].function_index] |= mask;
    stage_pending[p->roots[r].function_index] |= mask;
  }
  for (uint32_t b = 0; b < h->blocks.count; b++) {
    CHARGE(1);
    if (p->blocks[b].terminator == CS_OSDI_PRECISION_BRANCH)
      REQUIRE(propagate(p, d, p->blocks[b].condition,
                        CS_PRECISION_GRAPH_CONTROL_DEPENDENCY, &head));
  }
  while (head != NONE || stage_pending[0] || stage_pending[1] || stage_pending[2]) {
    /* Conservatively retain control dependence for every root of its stage.
       Prior-stage bindings can acquire new root roles during closure. */
    if (stage_pending[0] || stage_pending[1] || stage_pending[2]) {
      for (uint32_t b = 0; b < h->blocks.count; b++) {
        CHARGE(1);
        const CSOsdiPrecisionBlock *block = &p->blocks[b];
        REQUIRE(block->function_index < 3);
        if (block->terminator == CS_OSDI_PRECISION_BRANCH)
          REQUIRE(propagate(p, d, block->condition, stage_pending[block->function_index], &head));
      }
      stage_pending[0] = stage_pending[1] = stage_pending[2] = 0;
    }
    if (head != NONE) {
      CHARGE(1);
      uint32_t r = head;
      const cs_precision_graph_definition def = d[r];
      head = def.next; d[r].pending_mask = 0; d[r].next = NONE;
      uint32_t mask = def.pending_mask;
      if (def.kind == CS_PRECISION_GRAPH_INSTRUCTION) {
        const CSOsdiPrecisionInstruction *op = &p->instructions[def.index];
        REQUIRE(propagate(p, d, op->a, mask |
            (op->opcode == CS_OSDI_PRECISION_OP_SELECT ? CS_PRECISION_GRAPH_CONTROL_DEPENDENCY : 0), &head));
        if (op->b != NONE) REQUIRE(propagate(p, d, op->b, mask, &head));
        if (op->opcode == CS_OSDI_PRECISION_OP_SELECT)
          REQUIRE(propagate(p, d, op->aux, mask, &head));
      } else if (def.kind == CS_PRECISION_GRAPH_PHI) {
        const CSOsdiPrecisionBlock *block = &p->blocks[def.block];
        for (uint32_t k = 0; k < block->phi_edge_count; k++) {
          CHARGE(1);
          const CSOsdiPrecisionPhiEdge *phi = &p->phi[block->first_phi_edge + k];
          if (phi->destination == r)
            REQUIRE(propagate(p, d, phi->source, mask, &head));
        }
      } else if (def.kind == CS_PRECISION_GRAPH_INPUT) {
        const CSOsdiPrecisionInput *input = &p->inputs[def.index];
        uint32_t prior = input->kind == CS_OSDI_PRECISION_PRIOR_ROOT ? input->index :
            input->kind == CS_OSDI_PRECISION_PARAMETER_VALUE ? input->aux : NONE;
        if (prior != NONE) {
          REQUIRE(prior < h->roots.count);
          REQUIRE(propagate(p, d, p->roots[prior].value, mask, &head));
          uint32_t stage = p->roots[prior].function_index;
          REQUIRE(stage < 3);
          stage_pending[stage] |= mask & ~stage_masks[stage];
          stage_masks[stage] |= mask;
        }
      }
    }
  }
  *result = (cs_precision_guarded_graph){p, d, s, reached, cycles, effects};
  return 0;
}
