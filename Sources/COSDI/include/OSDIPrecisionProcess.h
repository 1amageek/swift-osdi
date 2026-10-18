#ifndef CS_OSDI_PRECISION_PROCESS_H
#define CS_OSDI_PRECISION_PROCESS_H
#include <stdint.h>
#include "OSDIPrecisionProtocol.h"
#include "COSDI.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct cs_osdi_precision_session cs_osdi_precision_session;
/* Cold borrow only; the caller retains an admitted image lease through its copy. */
int32_t cs_osdi_precision_program_view(cs_osdi_library *library, uint32_t descriptor_index,
    uint64_t maximum_program_bytes, const unsigned char **program, uint64_t *bytes, cs_osdi_error *error);
typedef int32_t (*cs_osdi_precision_cancel_probe)(void *context);

enum cs_osdi_precision_process_code {
    CS_OSDI_PRECISION_PROCESS_SUCCESS = 0,
    CS_OSDI_PRECISION_PROCESS_INVALID = 1,
    CS_OSDI_PRECISION_PROCESS_RESOURCE = 2,
    CS_OSDI_PRECISION_PROCESS_TIMEOUT = 3,
    CS_OSDI_PRECISION_PROCESS_CANCELLED = 4,
    CS_OSDI_PRECISION_PROCESS_WORKER = 5,
    CS_OSDI_PRECISION_PROCESS_PROTOCOL = 6,
    CS_OSDI_PRECISION_PROCESS_NUMERIC = 7,
    CS_OSDI_PRECISION_PROCESS_CLEANUP = 8,
    CS_OSDI_PRECISION_PROCESS_CLOSED = 9,
    CS_OSDI_PRECISION_PROCESS_IO = 10,
    CS_OSDI_PRECISION_PROCESS_UNSUPPORTED = 11,
    CS_OSDI_PRECISION_PROCESS_NONSMOOTH = 12
};

typedef struct cs_osdi_precision_process_options {
    uint64_t maximum_mapping_bytes;
    uint64_t arena_bytes;
    uint64_t maximum_work_units;
    uint64_t startup_timeout_nanoseconds;
    uint64_t evaluation_timeout_nanoseconds;
    uint64_t cleanup_timeout_nanoseconds;
    uint64_t poll_nanoseconds;
    uint32_t minimum_precision_bits;
    uint32_t maximum_precision_bits;
    unsigned char program_sha256[32];
    unsigned char worker_profile_sha256[32];
} cs_osdi_precision_process_options;

typedef struct cs_osdi_precision_process_issue {
    int32_t code;
    uint32_t worker_status;
    uint32_t instruction;
    int32_t exit_code;
    int32_t signal_number;
    int32_t system_errno;
    uint64_t request_sequence;
} cs_osdi_precision_process_issue;

typedef struct cs_osdi_precision_result_view {
    uint64_t raw_node_count;
    const double *f_high;
    const double *f_low;
    const double *f_radius;
    const double *delta_q_high;
    const double *delta_q_low;
    const double *delta_q_radius;
    const double *g;
    const double *c;
    uint32_t used_precision_bits;
    uint32_t has_maximum_timestep;
    double maximum_timestep;
    uint64_t completed_work_units;
    uint64_t request_sequence;
} cs_osdi_precision_result_view;

typedef struct cs_osdi_range_result_view {
    uint64_t raw_node_count;
    const double *f_lower;
    const double *f_upper;
    const double *q_lower;
    const double *q_upper;
    const double *g_lower;
    const double *g_upper;
    const double *c_lower;
    const double *c_upper;
    uint32_t used_precision_bits;
    uint32_t has_maximum_timestep;
    double maximum_timestep;
    uint64_t completed_work_units;
    uint64_t request_sequence;
    uint32_t program_has_time_input;
} cs_osdi_range_result_view;

/* The returned owner may be nonnull on failure only when cleanup is still due. */
int32_t cs_osdi_precision_process_create(const char *worker_path,
    const unsigned char *program, uint64_t program_bytes,
    const unsigned char *setup, uint64_t setup_bytes,
    uint32_t descriptor_index, uint32_t active_count, uint32_t raw_node_count,
    const cs_osdi_precision_process_options *options,
    cs_osdi_precision_cancel_probe cancelled, void *cancel_context,
    cs_osdi_precision_session **out, cs_osdi_precision_process_issue *issue);

/* Copies lexical inputs into the retained slab. Output expires at the next call/close. */
int32_t cs_osdi_precision_process_evaluate(cs_osdi_precision_session *session,
    const double *candidate_high, const double *candidate_low,
    const double *reference_high, const double *reference_low, uint64_t active_count,
    const cs_osdi_precision_context_v1 *candidate,
    const cs_osdi_precision_context_v1 *reference,
    uint32_t precision_bits, cs_osdi_precision_cancel_probe cancelled, void *cancel_context,
    cs_osdi_precision_result_view *out, cs_osdi_precision_process_issue *issue);

/* Cold range owner; protocol 2 is immutable and cannot serve point requests. */
int32_t cs_osdi_range_process_create(const char *worker_path,
    const unsigned char *program, uint64_t program_bytes,
    const unsigned char *setup, uint64_t setup_bytes,
    uint32_t descriptor_index, uint32_t active_count, uint32_t raw_node_count,
    const cs_osdi_precision_process_options *options,
    cs_osdi_precision_cancel_probe cancelled, void *cancel_context,
    cs_osdi_precision_session **out, cs_osdi_precision_process_issue *issue);

/* Closed smooth box and absolute F/Q/G/C bounds; no explicit time-partial claim.
 * Copies lexical input planes, which may alias each other. Output expires at the
 * next call/close. The same close API owns both process operations. */
int32_t cs_osdi_range_process_evaluate(cs_osdi_precision_session *session,
    const double *lower_high, const double *lower_low,
    const double *upper_high, const double *upper_low, uint64_t active_count,
    const cs_osdi_precision_context_v1 *lower_context,
    const cs_osdi_precision_context_v1 *upper_context,
    uint32_t precision_bits, cs_osdi_precision_cancel_probe cancelled, void *cancel_context,
    cs_osdi_range_result_view *out, cs_osdi_precision_process_issue *issue);

/* Frees and clears *session only after exact child reaping. Failure retains ownership. */
int32_t cs_osdi_precision_process_close(cs_osdi_precision_session **session,
    cs_osdi_precision_process_issue *issue);

int32_t cs_osdi_precision_process_identifier(const cs_osdi_precision_session *session);

/* Implemented by Swift with @c @implementation; no captured Swift pointer is retained. */
int32_t cs_osdi_precision_swift_task_cancelled(void *context);

#ifdef __cplusplus
}
#endif
#endif
