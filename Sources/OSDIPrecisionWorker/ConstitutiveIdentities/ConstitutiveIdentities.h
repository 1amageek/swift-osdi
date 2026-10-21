#ifndef CS_PRECISION_CONSTITUTIVE_IDENTITIES_H
#define CS_PRECISION_CONSTITUTIVE_IDENTITIES_H
#include "../DecisionCover/DecisionCover.h"
#include <gmp.h>

enum { CS_IDENTITY_INVALID = 120, CS_IDENTITY_CAPACITY = 121,
       CS_IDENTITY_COEFFICIENT_LIMIT = 122, CS_IDENTITY_NONZERO = 123, CS_IDENTITY_EMPTY_CONDITION = 124 };
typedef struct {
  uint32_t maximum_nodes, maximum_atoms, maximum_claims;
  uint64_t maximum_coefficient_bits, maximum_workspace_bytes;
} cs_precision_constitutive_limits;
typedef struct {
  uint32_t kind, source, a, b, constant, needed;
  uint64_t bits;
} cs_precision_constitutive_node;
typedef struct {
  uint32_t kind; /* ROOT_F or ROOT_Q; coefficients cover EVERY raw row. */
  mpq_t *coefficients;
} cs_precision_constitutive_claim;
/* All storage belongs to one exclusive worker arena. No borrow survives close. */
typedef struct {
  cs_precision_program *program;
  cs_precision_constitutive_limits limits;
  cs_precision_source_observer observer;
  cs_precision_constitutive_node *nodes;
  uint32_t *registers, *staged, *roots, *atoms, *atom_index, *coordinates, *quotient, *lower_index, *upper_index;
  mpq_t *constant_values, *weights, *matrix, *relations, *composed, *claim_values;
  cs_precision_constitutive_claim *claims;
  uint32_t node_count, atom_count, claim_count, initialized;
  size_t matrix_cells, relation_cells, claim_cells;
  uint8_t *constant_live,*weight_live,*matrix_live,*relation_live,*composed_live,*claim_live;
} cs_precision_constitutive_workspace;
typedef struct {
  const cs_precision_program *program;
  const CSOsdiPrecisionProgramHeader *program_bytes;
  const CSOsdiPrecisionSetupHeader *setup_bytes;
  const cs_precision_decision_cover *cover;
  const cs_precision_constitutive_claim *claims;
  uint32_t raw_row_count, claim_count, terminal_count;
  const uint32_t *coordinate_quotient;
} cs_precision_constitutive_identities;
/* Cold workspace admission. GMP temporaries use the already-installed arena.
 * Close clears initialized exact values; the caller closes the arena last. */
int cs_precision_constitutive_open(cs_precision_program *,
    const cs_precision_constitutive_limits *, cs_precision_constitutive_workspace *);
int cs_precision_constitutive_open_conditioned(cs_precision_program *,
    const cs_precision_constitutive_limits *,const uint32_t *,size_t,cs_precision_constitutive_workspace *);
int cs_precision_constitutive_domain(cs_precision_constitutive_workspace *,const double *,const double *,const double *,const double *);
void cs_precision_constitutive_close(cs_precision_constitutive_workspace *);
/* Whole-cover operations revalidate the complete source cover. Failed attempts
 * publish nothing, remove observation and preserve immutable caller inputs.
 * The output view and claims expire on the next attempt or close. */
int cs_precision_constitutive_construct(cs_precision_program *,
    const cs_osdi_precision_control_v1 *, const double *, const double *,
    const double *, const double *, const cs_precision_cover_limits *,
    cs_precision_cover_workspace *, cs_precision_cover_counters *,
    const cs_precision_decision_cover *, cs_precision_constitutive_workspace *,
    cs_precision_constitutive_identities *);
int cs_precision_constitutive_validate(cs_precision_program *,
    const cs_osdi_precision_control_v1 *, const double *, const double *,
    const double *, const double *, const cs_precision_cover_limits *,
    cs_precision_cover_workspace *, cs_precision_cover_counters *,
    const cs_precision_decision_cover *, cs_precision_constitutive_workspace *,
    const cs_precision_constitutive_identities *);
#endif
