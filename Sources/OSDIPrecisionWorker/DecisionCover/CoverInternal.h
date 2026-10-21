#ifndef CS_PRECISION_COVER_INTERNAL_H
#define CS_PRECISION_COVER_INTERNAL_H
#include "DecisionCover.h"
#include <limits.h>
#include <string.h>
#define CS_COVER_NONE UINT32_MAX
int cs_cover_admit(cs_precision_program *, const cs_osdi_precision_control_v1 *,
    const cs_precision_cover_limits *, cs_precision_cover_workspace *, const cs_precision_cover_counters *, const cs_precision_decision_cover *, size_t *);
int cs_cover_inputs_disjoint(cs_precision_program *,const cs_precision_cover_workspace *,const double *const [4],const cs_precision_decision_cover *);
int cs_cover_copy(cs_precision_program *, const cs_precision_cover_limits *, cs_precision_cover_counters *, void *, const void *, size_t);
int cs_cover_prefix(cs_precision_program *, const cs_precision_cover_limits *, cs_precision_cover_workspace *, cs_precision_cover_counters *,
    const cs_precision_decision_cover *, uint32_t);
int cs_cover_replay(cs_precision_program *, const cs_precision_cover_limits *, cs_precision_cover_workspace *, cs_precision_cover_counters *,
    const cs_precision_decision_cover *, size_t, cs_precision_frontier_result *);
int cs_cover_control_equal(const cs_osdi_precision_control_v1 *, const cs_osdi_precision_control_v1 *);
int cs_cover_decision_equal(const cs_precision_source_decision *, const cs_precision_source_decision *);
int cs_cover_event_equal(const cs_precision_source_event *, const cs_precision_source_event *);
uint64_t cs_cover_outcomes(const cs_precision_source_event *);
int cs_cover_outcome(cs_precision_program *, const cs_precision_cover_limits *, cs_precision_cover_counters *,
    const cs_precision_source_event *, uint64_t, cs_precision_source_decision *);
#endif
