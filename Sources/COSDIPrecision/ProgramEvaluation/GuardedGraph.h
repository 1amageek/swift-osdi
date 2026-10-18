#ifndef CS_PRECISION_GUARDED_GRAPH_H
#define CS_PRECISION_GUARDED_GRAPH_H
#include "Program.h"

enum {
  CS_PRECISION_GRAPH_CONSTANT = 1,
  CS_PRECISION_GRAPH_INPUT = 2,
  CS_PRECISION_GRAPH_INSTRUCTION = 3,
  CS_PRECISION_GRAPH_PHI = 4,
  CS_PRECISION_GRAPH_CONTROL_DEPENDENCY = 1u << 16
};
typedef struct {
  uint32_t kind, index, block, root_mask, phi_sources, pending_mask, next;
} cs_precision_graph_definition;
typedef struct {
  uint32_t reachable, color, parent, next_edge, terminal_instruction, incoming_predecessors;
} cs_precision_graph_block;
/* Borrows immutable admitted program bytes and caller scratch. No numeric or
   physical chart authority is implied. A new extraction invalidates old views
   of its scratch, including when that extraction fails. */
typedef struct {
  const cs_precision_program *program;
  const cs_precision_graph_definition *definitions;
  const cs_precision_graph_block *blocks;
  uint32_t reachable_blocks, has_cycles, terminal_effects;
} cs_precision_guarded_graph;

/* Scratch is exclusive, disjoint from the program/output and sized before entry.
   The result object is assigned only on success. Status uses the existing
   precision status enum; work remains cumulative in program->math. */
int cs_precision_guarded_graph_extract(
    cs_precision_program *program,
    cs_precision_graph_definition *definitions, size_t definition_capacity,
    cs_precision_graph_block *blocks, size_t block_capacity,
    cs_precision_guarded_graph *result);
#endif
