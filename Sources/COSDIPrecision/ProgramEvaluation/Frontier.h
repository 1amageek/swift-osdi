#ifndef CS_PRECISION_FRONTIER_H
#define CS_PRECISION_FRONTIER_H
#include "Chart.h"
/* 1024 bits cover every finite binary64 operand; one extra bit covers the
 * adjacent exact integer cell endpoint. No finite overflow becomes infinity. */
#define CS_PRECISION_CELL_LIMBS 17
#define CS_PRECISION_CELL_BITS 1025

enum {
  CS_PRECISION_SOURCE_RELATION = 4,
  CS_PRECISION_SOURCE_SIGN = 5,
  CS_PRECISION_SOURCE_CELL = 6,
  CS_PRECISION_SOURCE_LESS = 1,
  CS_PRECISION_SOURCE_EQUAL = 2,
  CS_PRECISION_SOURCE_GREATER = 3,
  CS_PRECISION_FRONTIER = 1,
  CS_PRECISION_TERMINAL = 2,
  CS_PRECISION_EXCLUDED = 3,
  CS_PRECISION_FRONTIER_STOP = 110,
  CS_PRECISION_PREFIX_EXCLUDED = 111,
  CS_PRECISION_PREFIX_MALFORMED = 112,
  CS_PRECISION_FRONTIER_CAPACITY = 113,
  CS_PRECISION_CELL_PRECISION = 114
};
typedef struct {
  uint32_t negative, count;
  uint64_t limbs[CS_PRECISION_CELL_LIMBS];
} cs_precision_cell_integer;
typedef struct {
  cs_precision_chart_decision site;
  uint32_t opcode;
  cs_precision_cell_integer cell;
} cs_precision_source_decision;
typedef struct {
  cs_precision_source_decision decision;
  cs_precision_chart_guard operands;
  uint32_t allowed_mask;
  uint64_t cell_count;
  cs_precision_cell_integer first_cell, last_cell;
} cs_precision_source_event;
typedef struct {
  const cs_precision_source_decision *prefix;
  size_t prefix_count;
  uint64_t maximum_steps, maximum_cells;
  uint32_t maximum_integer_bits;
} cs_precision_frontier_request;
typedef struct { uint64_t steps, events; } cs_precision_frontier_usage;
typedef struct {
  cs_precision_source_event *events;
  size_t event_capacity;
  cs_precision_chart_scratch formulas;
  /* Diagnostic accounting only; updated after every started evaluation, even
   * on refusal. It never makes an incomplete proof result observable. */
  cs_precision_frontier_usage *usage;
} cs_precision_frontier_scratch;
typedef struct {
  const cs_precision_program *program;
  const cs_precision_source_event *events;
  size_t event_count;
  uint32_t state;
  uint64_t completed_steps;
  const double *planes[8];
} cs_precision_frontier_result;
/* Exact source exploration facts only. The result is assigned only for a
 * frontier, complete terminal or checked empty prefix; numerical errors publish
 * nothing. Input/tape/program/scratch must outlive the synchronous borrow. */
int cs_precision_program_explore_frontier(cs_precision_program *program,
    cs_osdi_precision_control_v1 *control,
    const double *lower_high, const double *lower_low,
    const double *upper_high, const double *upper_low,
    const cs_precision_frontier_request *request,
    const cs_precision_frontier_scratch *scratch,
    cs_precision_frontier_result *result);
/* Exact bounded integer capsules used by arithmetic partitions and tree edges. */
int cs_precision_cell_valid(const cs_precision_cell_integer *integer, uint32_t maximum_bits);
int cs_precision_cell_compare(const cs_precision_cell_integer *a,
                              const cs_precision_cell_integer *b);
#endif
