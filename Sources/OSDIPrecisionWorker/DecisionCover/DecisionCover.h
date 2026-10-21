#ifndef CS_PRECISION_DECISION_COVER_H
#define CS_PRECISION_DECISION_COVER_H
#include "../ProgramEvaluation/Frontier.h"
enum {
  CS_PRECISION_COVER_PENDING = 0, CS_PRECISION_COVER_DECISION = 1,
  CS_PRECISION_COVER_TERMINAL = 2, CS_PRECISION_COVER_EXCLUSION = 3,
  CS_PRECISION_COVER_INVALID = 115, CS_PRECISION_COVER_CAPACITY = 116
};
typedef struct {
  uint32_t parent, depth, kind, first_child, child_count, leaf;
  cs_precision_source_decision incoming;
  cs_precision_source_event event;
} cs_precision_cover_node;
typedef struct {
  uint64_t maximum_steps, maximum_guard_records, maximum_copied_bytes, maximum_cells;
  uint32_t maximum_nodes, maximum_leaves, maximum_pending, maximum_depth, maximum_integer_bits;
} cs_precision_cover_limits;
typedef struct {
  cs_precision_cover_node *nodes; size_t node_capacity;
  uint32_t *pending; size_t pending_capacity;
  cs_precision_source_decision *prefix; size_t prefix_capacity;
  cs_precision_frontier_scratch replay;
  double *leaf_planes; size_t leaf_plane_capacity;
  double *domain; size_t domain_capacity;
  uint32_t *node_visits; size_t node_visit_capacity;
  uint32_t *leaf_visits; size_t leaf_visit_capacity;
} cs_precision_cover_workspace;
typedef struct {
  uint64_t steps, guard_records, copied_bytes;
} cs_precision_cover_counters;
typedef struct {
  const cs_precision_program *program;
  const CSOsdiPrecisionProgramHeader *program_bytes;
  const CSOsdiPrecisionSetupHeader *setup_bytes;
  cs_osdi_precision_control_v1 control;
  const double *domain;
  const cs_precision_cover_node *nodes;
  const double *leaf_planes;
  uint32_t node_count, leaf_count;
  size_t plane_stride;
} cs_precision_decision_cover;
/* Both operations borrow immutable inputs and caller-preadmitted exclusive,
 * mutually disjoint workspace. Program bytes, input arrays and workspace do not
 * alias. Failed attempts invalidate prior views and publish no complete result.
 * Counters are cumulative, including independent validation. */
int cs_precision_decision_cover_construct(cs_precision_program *program,
    const cs_osdi_precision_control_v1 *control,
    const double *lower_high, const double *lower_low,
    const double *upper_high, const double *upper_low,
    const cs_precision_cover_limits *limits, cs_precision_cover_workspace *workspace,
    cs_precision_cover_counters *counters, cs_precision_decision_cover *result);
int cs_precision_decision_cover_validate(cs_precision_program *program,
    const cs_osdi_precision_control_v1 *control,
    const double *lower_high, const double *lower_low,
    const double *upper_high, const double *upper_low,
    const cs_precision_cover_limits *limits, cs_precision_cover_workspace *workspace,
    cs_precision_cover_counters *counters, const cs_precision_decision_cover *candidate);
#endif
