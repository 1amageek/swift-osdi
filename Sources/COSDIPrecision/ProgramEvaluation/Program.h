#ifndef CS_PRECISION_PROGRAM_H
#define CS_PRECISION_PROGRAM_H
#include "../Arithmetic/Arithmetic.h"
#include "Identity.h"
typedef struct {
  uint32_t block, phase;
} cs_precision_control_frame;
typedef struct {
  uint64_t token, x_token, c_token;
  uint32_t x_register, c_register;
  unsigned char active, shape;
  /* Immutable source constant index + 1; zero means no exact infinity fact. */
  uint32_t primitive_infinity;
} cs_precision_range_fact;
typedef struct {
  const CSOsdiPrecisionProgramHeader *header;
  const CSOsdiPrecisionSetupHeader *setup;
  const CSOsdiPrecisionRegister *registers;
  const CSOsdiPrecisionConstant *constants;
  const CSOsdiPrecisionInput *inputs;
  const CSOsdiPrecisionFunction *functions;
  const CSOsdiPrecisionBlock *blocks;
  const CSOsdiPrecisionInstruction *instructions;
  const CSOsdiPrecisionPhiEdge *phi;
  const CSOsdiPrecisionRoot *roots;
  const CSOsdiPrecisionParameter *parameters;
  const CSOsdiPrecisionNode *nodes;
  const CSOsdiPrecisionParameterFact *facts;
  const CSOsdiPrecisionSimulationFact *simulation;
  const uint32_t *map;
  const unsigned char *strings, *setup_strings;
  cs_precision_value *values, *root_values, *stage;
  uint32_t *owners, *phi_blocks, *fact_index, *model_peers, *input_simulation,
      *topology, *dense;
  cs_precision_interval *candidate_q;
  /* Version-2-only arena storage; no range allocation changes point admission. */
  cs_precision_range_fact *range_values, *range_roots, *range_stage;
  cs_precision_interval *range_coordinates;
  uint64_t range_next_token;
  uint32_t *predecessors, *control_colors, *control_nodes;
  cs_precision_control_frame *control_stack;
  cs_precision_math math;
  cs_precision_identity identity;
  uint32_t detail, flags, precision, maximum_precision, stage_capacity;
  uint64_t active_count;
  int has_bound;
  int range_mode;
  double bound;
  /* Exclusive synchronous chart call only; never retained after its return. */
  struct cs_precision_chart_execution *chart;
} cs_precision_program;
int cs_precision_program_open(cs_precision_program *program, const void *bytes,
                              size_t length, const void *setup,
                              size_t setup_length,
                              const cs_osdi_precision_control_v1 *control);
int cs_precision_program_evaluate(cs_precision_program *program,
                                  cs_osdi_precision_control_v1 *control,
                                  const double *candidate_high,
                                  const double *candidate_low,
                                  const double *reference_high,
                                  const double *reference_low,
                                  double *outputs[8]);
#endif
