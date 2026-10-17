#ifndef CS_OSDI_PRECISION_PROTOCOL_H
#define CS_OSDI_PRECISION_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

/* Local host IPC, independent of the unchanged upstream OSDI model ABI. */
#define CS_OSDI_PRECISION_MAGIC UINT64_C(0x313056504944534f)
#define CS_OSDI_PRECISION_PROTOCOL_VERSION UINT32_C(1)
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
    CS_OSDI_PRECISION_NUMERIC_INVARIANT = 9
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

#if defined(__cplusplus)
static_assert(sizeof(double) == 8, "Precision protocol requires binary64 double storage");
static_assert(sizeof(cs_osdi_precision_region_v1) == 16, "Invalid precision region layout");
static_assert(sizeof(cs_osdi_precision_context_v1) == 24, "Invalid precision context layout");
static_assert(sizeof(cs_osdi_precision_control_v1) == 464, "Invalid precision control layout");
static_assert(offsetof(cs_osdi_precision_control_v1, request_sequence) % 8 == 0,
              "Precision sequence must be aligned");
#else
_Static_assert(sizeof(double) == 8, "Precision protocol requires binary64 double storage");
_Static_assert(sizeof(cs_osdi_precision_region_v1) == 16, "Invalid precision region layout");
_Static_assert(sizeof(cs_osdi_precision_context_v1) == 24, "Invalid precision context layout");
_Static_assert(sizeof(cs_osdi_precision_control_v1) == 464, "Invalid precision control layout");
_Static_assert(offsetof(cs_osdi_precision_control_v1, request_sequence) % 8 == 0,
               "Precision sequence must be aligned");
#endif

#endif
