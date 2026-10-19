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

/* Conditional formulas only. Limits are cumulative for this chart owner. */
typedef struct cs_osdi_chart_result_view {
    cs_osdi_range_result_view formulas;
    const cs_osdi_chart_guard_v3 *guards;
    uint64_t guard_count, completed_steps;
    uint32_t semantics;
} cs_osdi_chart_result_view;
int32_t cs_osdi_chart_process_create(const char *worker_path,
    const unsigned char *program, uint64_t program_bytes,
    const unsigned char *setup, uint64_t setup_bytes,
    uint32_t descriptor_index, uint32_t active_count, uint32_t raw_node_count,
    const cs_osdi_precision_process_options *options,
    const cs_osdi_chart_limits_v3 *limits,
    cs_osdi_precision_cancel_probe cancelled, void *cancel_context,
    cs_osdi_precision_session **out, cs_osdi_precision_process_issue *issue);
/* Complete-only source cover. All pointers expire at the next attempt/close;
 * no root, nonempty-chart, aggregate timestep, flow or history fact is implied. */
typedef struct cs_osdi_cover_result_view {
    uint64_t raw_node_count, active_count, plane_stride;
    const cs_osdi_cover_node_v4 *nodes;
    const double *leaf_planes;
    const double *lower_high, *lower_low, *upper_high, *upper_low;
    uint32_t node_count, leaf_count, used_precision_bits, program_has_time_input;
    cs_osdi_precision_context_v1 lower_context, upper_context;
    uint64_t completed_work_units, completed_steps, completed_guard_records, completed_copied_bytes, request_sequence;
} cs_osdi_cover_result_view;
int32_t cs_osdi_cover_process_create(const char *worker_path,
    const unsigned char *program, uint64_t program_bytes,
    const unsigned char *setup, uint64_t setup_bytes,
    uint32_t descriptor_index, uint32_t active_count, uint32_t raw_node_count,
    const cs_osdi_precision_process_options *options, const cs_osdi_cover_limits_v4 *limits,
    cs_osdi_precision_cancel_probe cancelled, void *cancel_context,
    cs_osdi_precision_session **out, cs_osdi_precision_process_issue *issue);
int32_t cs_osdi_cover_process_evaluate(cs_osdi_precision_session *session,
    const double *lower_high, const double *lower_low, const double *upper_high, const double *upper_low,
    uint64_t active_count, const cs_osdi_precision_context_v1 *lower_context,
    const cs_osdi_precision_context_v1 *upper_context, uint32_t precision_bits,
    cs_osdi_precision_cancel_probe cancelled, void *cancel_context,
    cs_osdi_cover_result_view *out, cs_osdi_precision_process_issue *issue);

/* Complete source-linear identities only; no physical rank/root/flow authority.
 * All pointers borrow the existing session slab until the next attempt/close. */
typedef struct cs_osdi_identity_result_view {
    cs_osdi_cover_result_view cover;
    const cs_osdi_identity_claim_v5 *claims;
    const cs_osdi_identity_coefficient_v5 *coefficients;
    const uint64_t *words;
    uint64_t claim_count, coefficient_count, words_per_coefficient;
} cs_osdi_identity_result_view;
int32_t cs_osdi_identity_process_create(const char *, const unsigned char *, uint64_t,
    const unsigned char *, uint64_t, uint32_t, uint32_t, uint32_t,
    const cs_osdi_precision_process_options *, const cs_osdi_cover_limits_v4 *, const cs_osdi_identity_limits_v5 *,
    const uint32_t *, uint64_t, cs_osdi_precision_cancel_probe, void *, cs_osdi_precision_session **, cs_osdi_precision_process_issue *);
int32_t cs_osdi_identity_process_evaluate(cs_osdi_precision_session *, const double *, const double *,
    const double *, const double *, uint64_t, const cs_osdi_precision_context_v1 *, const cs_osdi_precision_context_v1 *,
    uint32_t, cs_osdi_precision_cancel_probe, void *, cs_osdi_identity_result_view *, cs_osdi_precision_process_issue *);

/* Tape may alias a preceding result's decisions only through an explicit caller
 * copy: guard records and decision records have distinct strides. */
int32_t cs_osdi_chart_process_evaluate(cs_osdi_precision_session *session,
    const double *lower_high, const double *lower_low,
    const double *upper_high, const double *upper_low, uint64_t active_count,
    const cs_osdi_precision_context_v1 *lower_context,
    const cs_osdi_precision_context_v1 *upper_context, uint32_t precision_bits,
    uint32_t chart_mode, const cs_osdi_chart_decision_v3 *tape, uint64_t tape_count,
    cs_osdi_precision_cancel_probe cancelled, void *cancel_context,
    cs_osdi_chart_result_view *out, cs_osdi_precision_process_issue *issue);

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
