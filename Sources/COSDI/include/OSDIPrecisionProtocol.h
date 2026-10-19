#ifndef CS_OSDI_PRECISION_PROTOCOL_H
#define CS_OSDI_PRECISION_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

/* Local host IPC, independent of the unchanged upstream OSDI model ABI. */
#define CS_OSDI_PRECISION_MAGIC UINT64_C(0x313056504944534f)
#define CS_OSDI_PRECISION_PROTOCOL_VERSION UINT32_C(1)
#define CS_OSDI_RANGE_PROTOCOL_VERSION UINT32_C(2)
#define CS_OSDI_PRECISION_BYTE_ORDER UINT32_C(0x01020304)
#define CS_OSDI_PRECISION_REGION_COUNT UINT32_C(14)
#define CS_OSDI_PRECISION_NO_INSTRUCTION UINT32_MAX
#define CS_OSDI_PRECISION_INITIAL_RESPONSE UINT64_MAX
#define CS_OSDI_PRECISION_MAX_SEQUENCE (UINT64_MAX - UINT64_C(1))

#define CS_OSDI_PRECISION_SLAB_FD 3
#define CS_OSDI_PRECISION_REQUEST_FD 4
#define CS_OSDI_PRECISION_RESPONSE_FD 5

enum cs_osdi_precision_region_id {
    CS_OSDI_PRECISION_PROGRAM = 0,
    CS_OSDI_PRECISION_SETUP = 1,
    CS_OSDI_PRECISION_CANDIDATE_HIGH = 2,
    CS_OSDI_PRECISION_CANDIDATE_LOW = 3,
    CS_OSDI_PRECISION_REFERENCE_HIGH = 4,
    CS_OSDI_PRECISION_REFERENCE_LOW = 5,
    CS_OSDI_PRECISION_F_HIGH = 6,
    CS_OSDI_PRECISION_F_LOW = 7,
    CS_OSDI_PRECISION_F_RADIUS = 8,
    CS_OSDI_PRECISION_DELTA_Q_HIGH = 9,
    CS_OSDI_PRECISION_DELTA_Q_LOW = 10,
    CS_OSDI_PRECISION_DELTA_Q_RADIUS = 11,
    CS_OSDI_PRECISION_G = 12,
    CS_OSDI_PRECISION_C = 13
};

/* Explicit version-2 interpretation of the same fourteen physical regions. */
enum cs_osdi_range_region_id {
    CS_OSDI_RANGE_LOWER_HIGH = 2,
    CS_OSDI_RANGE_LOWER_LOW = 3,
    CS_OSDI_RANGE_UPPER_HIGH = 4,
    CS_OSDI_RANGE_UPPER_LOW = 5,
    CS_OSDI_RANGE_F_LOWER = 6,
    CS_OSDI_RANGE_F_UPPER = 7,
    CS_OSDI_RANGE_Q_LOWER = 8,
    CS_OSDI_RANGE_Q_UPPER = 9,
    CS_OSDI_RANGE_G_LOWER = 10,
    CS_OSDI_RANGE_G_UPPER = 11,
    CS_OSDI_RANGE_C_LOWER = 12,
    CS_OSDI_RANGE_C_UPPER = 13
};

enum cs_osdi_precision_status {
    CS_OSDI_PRECISION_SUCCESS = 0,
    CS_OSDI_PRECISION_INVALID_INPUT = 1,
    CS_OSDI_PRECISION_INVALID_PROGRAM = 2,
    CS_OSDI_PRECISION_UNSUPPORTED = 3,
    CS_OSDI_PRECISION_DOMAIN = 4,
    CS_OSDI_PRECISION_UNRESOLVED_BRANCH = 5,
    CS_OSDI_PRECISION_EXHAUSTED = 6,
    CS_OSDI_PRECISION_WORK_EXHAUSTED = 7,
    CS_OSDI_PRECISION_NONFINITE_PROJECTION = 8,
    CS_OSDI_PRECISION_NUMERIC_INVARIANT = 9,
    CS_OSDI_PRECISION_NONSMOOTH = 10
};

enum cs_osdi_precision_worker_exit {
    CS_OSDI_PRECISION_EXIT_ARENA = 80,
    CS_OSDI_PRECISION_EXIT_INVARIANT = 81,
    CS_OSDI_PRECISION_EXIT_PROTOCOL = 82
};

enum cs_osdi_precision_mode {
    CS_OSDI_PRECISION_TRANSIENT = 2,
    CS_OSDI_PRECISION_TRANSIENT_INITIALIZATION = 3
};

typedef struct cs_osdi_precision_region_v1 {
    uint64_t byte_offset;
    uint64_t element_count;
} cs_osdi_precision_region_v1;

typedef struct cs_osdi_precision_context_v1 {
    uint32_t mode;
    uint32_t reserved;
    double time_high;
    double time_low;
} cs_osdi_precision_context_v1;

/*
 * Parent owns immutable geometry/program/setup and request fields. Worker owns
 * response fields and output regions. Exactly one request is in flight.
 *
 * Only C acquire/release atomic operations access the aligned sequence fields.
 * Parent initializes response_sequence to UINT64_MAX. Worker publishes zero
 * only after successful validation/setup, then writes the ready wake byte.
 * Requests/responses thereafter use matching sequences 1..UINT64_MAX-1.
 * A pipe wake alone never authorizes output publication.
 *
 * Regions 0/1 have byte elements; all other elements are binary64 doubles.
 * Vector counts are M for inputs, N for F/delta-Q and N*N for G/C. All offsets,
 * sizes, alignment and disjointness are validated before any region access.
 * Work limit is fixed at admission; fresh setup/evaluation counters cover all
 * executed operations, including both endpoint lanes, derivatives/projection.
 *
 * Version 2 retains this exact structure and region count. Inputs 2..5 are
 * lower-high/lower-low/upper-high/upper-low. Outputs 6..9 are N-element absolute
 * F-lower/F-upper/Q-lower/Q-upper; outputs 10..13 are N*N-element row-major
 * G-lower/G-upper/C-lower/C-upper. candidate/reference store lower/upper time
 * endpoints with one equal mode. No version-1 field changes its interpretation.
 */
typedef struct cs_osdi_precision_control_v1 {
    uint64_t magic;
    uint32_t protocol_version;
    uint32_t header_bytes;
    uint64_t mapping_bytes;
    uint32_t byte_order;
    uint32_t descriptor_index;
    uint64_t active_count;
    uint64_t raw_node_count;
    uint64_t arena_bytes;
    uint32_t maximum_precision_bits;
    uint32_t minimum_precision_bits;
    unsigned char program_sha256[32];
    unsigned char worker_profile_sha256[32];
    cs_osdi_precision_region_v1 regions[CS_OSDI_PRECISION_REGION_COUNT];
    uint64_t request_sequence;
    uint64_t response_sequence;
    cs_osdi_precision_context_v1 candidate;
    cs_osdi_precision_context_v1 reference;
    uint32_t requested_precision_bits;
    uint32_t response_status;
    uint64_t maximum_work_units;
    uint64_t completed_work_units;
    uint32_t used_precision_bits;
    uint32_t model_flags;
    uint32_t has_maximum_timestep;
    uint32_t response_detail;
    double maximum_timestep;
} cs_osdi_precision_control_v1;

/* Protocol 3 is a separate conditional-formula operation. Versions 1/2 above
 * retain their layout. Reserved fields must be zero on both sides. */
#define CS_OSDI_CHART_PROTOCOL_VERSION UINT32_C(3)
enum cs_osdi_chart_mode {
    CS_OSDI_CHART_RECORD = 1, CS_OSDI_CHART_EXACT_GRAPH = 2,
    CS_OSDI_CHART_FORMULA_CLOSURE = 3
};
/* Source REAL sentinels, with the exact constant-record index in integer payload.
 * Ordinary REAL=1 still requires finite ordered binary64 endpoints. */
enum cs_osdi_chart_operand_kind {
    CS_OSDI_CHART_NEGATIVE_PRIMITIVE_INFINITY = 5,
    CS_OSDI_CHART_POSITIVE_PRIMITIVE_INFINITY = 6
};
enum cs_osdi_chart_status {
    CS_OSDI_CHART_TAPE_MISMATCH = 100, CS_OSDI_CHART_CAPACITY = 101,
    CS_OSDI_CHART_STEP_LIMIT = 102
};
typedef struct cs_osdi_chart_decision_v3 {
    uint32_t stage, block, instruction, kind, choice, reserved;
    uint64_t ordinal;
} cs_osdi_chart_decision_v3;
typedef struct cs_osdi_chart_guard_v3 {
    cs_osdi_chart_decision_v3 decision;
    uint32_t opcode, a, b, a_type, b_type, a_integer, b_integer;
    uint32_t formula_truth_known, boundary_extension, reserved;
    double a_lower, a_upper, b_lower, b_upper;
} cs_osdi_chart_guard_v3;
typedef struct cs_osdi_chart_limits_v3 {
    uint64_t maximum_steps, maximum_tape_entries, maximum_guard_records;
} cs_osdi_chart_limits_v3;
typedef struct cs_osdi_chart_control_v3 {
    cs_osdi_precision_control_v1 base;
    cs_osdi_precision_region_v1 tape_region, guard_region;
    cs_osdi_chart_limits_v3 limits;
    uint32_t chart_mode, reserved;
    uint64_t tape_count, guard_count, completed_steps;
} cs_osdi_chart_control_v3;

/* Protocol 4 publishes only independently checked complete source decision
 * covers. Existing versions and compiler program ABI 1 are unchanged. */
#define CS_OSDI_COVER_PROTOCOL_VERSION UINT32_C(4)
#define CS_OSDI_COVER_CELL_LIMBS 17u
#define CS_OSDI_COVER_CELL_BITS 1025u
enum cs_osdi_cover_node_kind {
    CS_OSDI_COVER_DECISION = 1, CS_OSDI_COVER_TERMINAL = 2
};
enum cs_osdi_cover_source_kind {
    CS_OSDI_COVER_RELATION = 4, CS_OSDI_COVER_SIGN = 5, CS_OSDI_COVER_CELL = 6
};
enum cs_osdi_cover_status {
    CS_OSDI_COVER_PREFIX_MALFORMED = 112, CS_OSDI_COVER_FRONTIER_CAPACITY = 113,
    CS_OSDI_COVER_CELL_PRECISION = 114, CS_OSDI_COVER_INVALID = 115,
    CS_OSDI_COVER_CAPACITY = 116
};
typedef struct cs_osdi_cover_integer_v4 {
    uint32_t negative, count;
    uint64_t limbs[CS_OSDI_COVER_CELL_LIMBS];
} cs_osdi_cover_integer_v4;
typedef struct cs_osdi_cover_decision_v4 {
    cs_osdi_chart_decision_v3 site;
    uint32_t opcode, reserved;
    cs_osdi_cover_integer_v4 cell;
} cs_osdi_cover_decision_v4;
typedef struct cs_osdi_cover_event_v4 {
    cs_osdi_cover_decision_v4 decision;
    cs_osdi_chart_guard_v3 operands;
    uint32_t allowed_mask, reserved;
    uint64_t cell_count;
    cs_osdi_cover_integer_v4 first_cell, last_cell;
} cs_osdi_cover_event_v4;
typedef struct cs_osdi_cover_node_v4 {
    uint32_t parent, depth, kind, first_child, child_count, leaf;
    cs_osdi_cover_decision_v4 incoming;
    cs_osdi_cover_event_v4 event;
} cs_osdi_cover_node_v4;
typedef struct cs_osdi_cover_limits_v4 {
    uint64_t maximum_steps, maximum_guard_records, maximum_copied_bytes, maximum_cells;
    uint32_t maximum_nodes, maximum_leaves, maximum_pending, maximum_depth, maximum_integer_bits, reserved;
} cs_osdi_cover_limits_v4;
typedef struct cs_osdi_cover_control_v4 {
    cs_osdi_precision_control_v1 base;
    cs_osdi_precision_region_v1 tree_region, leaf_region;
    cs_osdi_cover_limits_v4 limits;
    uint32_t node_count, leaf_count;
    uint64_t completed_steps, completed_guard_records, completed_copied_bytes;
} cs_osdi_cover_control_v4;

/* Protocol 5 adds checked source-linear identities to the unchanged v4 cover.
 * The immutable quotient is an explicit correlated-domain condition, never a
 * rewrite of the original box. Exact dyadic coefficients use canonical slots. */
#define CS_OSDI_IDENTITY_PROTOCOL_VERSION UINT32_C(5)
enum cs_osdi_identity_status {
    CS_OSDI_IDENTITY_INVALID = 120, CS_OSDI_IDENTITY_CAPACITY = 121,
    CS_OSDI_IDENTITY_COEFFICIENT_LIMIT = 122, CS_OSDI_IDENTITY_NONZERO = 123, CS_OSDI_IDENTITY_EMPTY_CONDITION = 124
};
typedef struct cs_osdi_identity_limits_v5 {
    uint32_t maximum_nodes, maximum_atoms;
    uint64_t maximum_coefficient_bits, maximum_workspace_bytes;
} cs_osdi_identity_limits_v5;
typedef struct cs_osdi_identity_claim_v5 { uint32_t kind, reserved; } cs_osdi_identity_claim_v5;
typedef struct cs_osdi_identity_coefficient_v5 {
    uint64_t first_word, word_count, denominator_power;
    uint32_t negative, reserved;
} cs_osdi_identity_coefficient_v5;
typedef struct cs_osdi_identity_control_v5 {
    cs_osdi_cover_control_v4 cover;
    cs_osdi_precision_region_v1 claim_region, coefficient_region, word_region, quotient_region;
    cs_osdi_identity_limits_v5 limits;
    uint64_t claim_count;
} cs_osdi_identity_control_v5;

#if defined(__cplusplus)
static_assert(sizeof(cs_osdi_identity_control_v5)==680 && offsetof(cs_osdi_identity_control_v5,claim_count)==672, "Invalid identity header layout");
static_assert(sizeof(cs_osdi_cover_integer_v4) == 144 && sizeof(cs_osdi_cover_decision_v4) == 184, "Invalid cover cell/decision layout");
static_assert(sizeof(cs_osdi_cover_event_v4) == 592 && sizeof(cs_osdi_cover_node_v4) == 800, "Invalid cover event/node layout");
static_assert(sizeof(cs_osdi_cover_control_v4) == 584 && offsetof(cs_osdi_cover_control_v4, node_count) == 552, "Invalid cover header layout");
static_assert(sizeof(cs_osdi_chart_decision_v3) == 32 && offsetof(cs_osdi_chart_decision_v3, ordinal) == 24, "Invalid chart decision layout");
static_assert(sizeof(cs_osdi_chart_guard_v3) == 104 && offsetof(cs_osdi_chart_guard_v3, a_lower) == 72, "Invalid chart guard layout");
static_assert(sizeof(cs_osdi_chart_control_v3) == 552 && offsetof(cs_osdi_chart_control_v3, chart_mode) == 520, "Invalid chart control layout");
static_assert(sizeof(double) == 8, "Precision protocol requires binary64 double storage");
static_assert(sizeof(cs_osdi_precision_region_v1) == 16, "Invalid precision region layout");
static_assert(sizeof(cs_osdi_precision_context_v1) == 24, "Invalid precision context layout");
static_assert(sizeof(cs_osdi_precision_control_v1) == 464, "Invalid precision control layout");
static_assert(offsetof(cs_osdi_precision_control_v1, request_sequence) % 8 == 0,
              "Precision sequence must be aligned");
#else
_Static_assert(sizeof(cs_osdi_identity_control_v5)==680 && offsetof(cs_osdi_identity_control_v5,claim_count)==672, "Invalid identity header layout");
_Static_assert(sizeof(cs_osdi_cover_integer_v4) == 144 && sizeof(cs_osdi_cover_decision_v4) == 184, "Invalid cover cell/decision layout");
_Static_assert(sizeof(cs_osdi_cover_event_v4) == 592 && sizeof(cs_osdi_cover_node_v4) == 800, "Invalid cover event/node layout");
_Static_assert(sizeof(cs_osdi_cover_control_v4) == 584 && offsetof(cs_osdi_cover_control_v4, node_count) == 552, "Invalid cover header layout");
_Static_assert(sizeof(cs_osdi_chart_decision_v3) == 32 && offsetof(cs_osdi_chart_decision_v3, ordinal) == 24, "Invalid chart decision layout");
_Static_assert(sizeof(cs_osdi_chart_guard_v3) == 104 && offsetof(cs_osdi_chart_guard_v3, a_lower) == 72, "Invalid chart guard layout");
_Static_assert(sizeof(cs_osdi_chart_control_v3) == 552 && offsetof(cs_osdi_chart_control_v3, chart_mode) == 520, "Invalid chart control layout");
_Static_assert(sizeof(double) == 8, "Precision protocol requires binary64 double storage");
_Static_assert(sizeof(cs_osdi_precision_region_v1) == 16, "Invalid precision region layout");
_Static_assert(sizeof(cs_osdi_precision_context_v1) == 24, "Invalid precision context layout");
_Static_assert(sizeof(cs_osdi_precision_control_v1) == 464, "Invalid precision control layout");
_Static_assert(offsetof(cs_osdi_precision_control_v1, request_sequence) % 8 == 0,
               "Precision sequence must be aligned");
#endif

#endif
