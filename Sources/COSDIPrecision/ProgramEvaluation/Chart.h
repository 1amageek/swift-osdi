#ifndef CS_PRECISION_CHART_H
#define CS_PRECISION_CHART_H
#include "Program.h"

enum {
  CS_PRECISION_CHART_RECORD = 1,
  CS_PRECISION_CHART_EXACT_GRAPH = 2,
  CS_PRECISION_CHART_FORMULA_CLOSURE = 3,
  CS_PRECISION_CHART_PREDICATE = 1,
  CS_PRECISION_CHART_BRANCH = 2,
  CS_PRECISION_CHART_SELECT = 3,
  CS_PRECISION_CHART_TAPE_MISMATCH = 100,
  CS_PRECISION_CHART_CAPACITY = 101,
  CS_PRECISION_CHART_STEP_LIMIT = 102
};
typedef struct {
  uint32_t stage, block, instruction, kind, choice;
  uint64_t ordinal;
} cs_precision_chart_decision;
typedef struct {
  cs_precision_chart_decision decision;
  uint32_t opcode, a, b, a_type, b_type, a_integer, b_integer, formula_truth_known, boundary_extension;
  double a_lower, a_upper, b_lower, b_upper;
} cs_precision_chart_guard;
typedef struct {
  uint32_t mode;
  const cs_precision_chart_decision *tape;
  size_t tape_count;
  uint64_t maximum_steps;
} cs_precision_chart_request;
typedef struct {
  cs_precision_chart_guard *guards;
  size_t guard_capacity;
  double *planes[8];
  size_t plane_capacities[8];
} cs_precision_chart_scratch;
/* Borrowed conditional formula bounds, not nonempty/chart-cover/flow facts.
   Program, request tape and scratch owners must outlive this view. */
typedef struct {
  const cs_precision_program *program;
  const cs_precision_chart_guard *guards;
  size_t guard_count;
  const double *planes[8];
  uint32_t semantics;
  uint64_t completed_steps;
} cs_precision_chart_result;

int cs_precision_program_evaluate_chart(
    cs_precision_program *program, cs_osdi_precision_control_v1 *control,
    const double *lower_high, const double *lower_low,
    const double *upper_high, const double *upper_low,
    const cs_precision_chart_request *request,
    const cs_precision_chart_scratch *scratch,
    cs_precision_chart_result *result);
#endif
