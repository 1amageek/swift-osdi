#ifndef CS_PRECISION_PROGRAM_H
#define CS_PRECISION_PROGRAM_H
#include "../Arithmetic/Arithmetic.h"
#include "Identity.h"
typedef struct {
  uint32_t block, phase;
} cs_precision_control_frame;
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
  unsigned char *range_values_active, *range_roots_active, *range_stage_active;
  cs_precision_interval *range_coordinates;
  uint32_t *predecessors, *control_colors, *control_nodes;
  cs_precision_control_frame *control_stack;
  cs_precision_math math;
  cs_precision_identity identity;
  uint32_t detail, flags, precision, maximum_precision, stage_capacity;
  uint64_t active_count;
  int has_bound;
  int range_mode;
  double bound;
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
