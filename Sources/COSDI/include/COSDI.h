#ifndef CS_OSDI_H
#define CS_OSDI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cs_osdi_library cs_osdi_library;

enum {
    CS_OSDI_SUCCESS = 0,
    CS_OSDI_UNSUPPORTED_HOST = 1,
    CS_OSDI_WRONG_ARCHITECTURE = 2,
    CS_OSDI_OPEN_FAILED = 3,
    CS_OSDI_MISSING_SYMBOL = 4,
    CS_OSDI_INCOMPATIBLE_ABI = 5,
    CS_OSDI_MALFORMED_DESCRIPTOR = 6,
    CS_OSDI_RESOURCE_LIMIT = 7,
    CS_OSDI_ALLOCATION_FAILED = 8,
    CS_OSDI_CLOSE_FAILED = 9,
    CS_OSDI_INVALID_ARGUMENT = 10,
    CS_OSDI_DESCRIPTOR_INDEX_OUT_OF_RANGE = 11,
    CS_OSDI_UNSUPPORTED_FEATURE = 12,
    CS_OSDI_PARAMETER_NOT_FOUND = 13,
    CS_OSDI_PARAMETER_TYPE_MISMATCH = 14,
    CS_OSDI_PARAMETER_SCOPE_MISMATCH = 15,
    CS_OSDI_SETUP_FAILED = 16,
    CS_OSDI_EVALUATION_FAILED = 17,
    CS_OSDI_NONFINITE_INPUT = 18,
    CS_OSDI_NONFINITE_OUTPUT = 19,
    CS_OSDI_DIAGNOSTIC_LIMIT = 20,
    CS_OSDI_AMBIGUOUS_PARAMETER = 21
};

typedef struct cs_osdi_limits {
    uint32_t maximum_descriptors;
    uint64_t maximum_metadata_bytes;
    /* Per-descriptor model+instance bytes plus advertised resistive/reactive
     * double-array capacities; inspection does not allocate these buffers. */
    uint64_t maximum_storage_bytes;
} cs_osdi_limits;

typedef struct cs_osdi_error {
    int32_t code;
    uint32_t descriptor_index;
    char field[80];
    char detail[256];
    char cleanup_detail[256];
    uint32_t model_flags;
    uint32_t setup_error_count;
    uint32_t setup_error_code;
    uint32_t setup_parameter_id;
} cs_osdi_error;

/* Borrowed names remain valid until the owning library closes successfully. */
typedef struct cs_osdi_descriptor_info_record {
    const char *name;
    uint64_t name_length;
    uint32_t nodes;
    uint32_t terminals;
    uint32_t jacobian_entries;
    uint32_t states;
    uint32_t collapsible;
    uint32_t parameters;
    uint32_t instance_parameters;
    uint32_t opvars;
    uint32_t noise_sources;
    uint64_t instance_bytes;
    uint64_t model_bytes;
} cs_osdi_descriptor_info_record;

/* Ordinary failures leave out_library NULL. Failed cleanup returns CLOSE_FAILED and
 * a cleanup-only owner; error retains the primary cause and cleanup_detail.
 * Native initializers run only after host inspection. */
int32_t cs_osdi_open(const char *path, const cs_osdi_limits *limits,
                     cs_osdi_library **out_library, cs_osdi_error *error);
/* Close failure retains the owner and its borrowed data for an explicit retry. */
int32_t cs_osdi_close(cs_osdi_library *library, cs_osdi_error *error);
uint32_t cs_osdi_descriptor_count(const cs_osdi_library *library);
uint64_t cs_osdi_descriptor_stride(const cs_osdi_library *library);
int32_t cs_osdi_descriptor_info(const cs_osdi_library *library, uint32_t index,
                               cs_osdi_descriptor_info_record *out_info,
                               cs_osdi_error *error);
/* Zero bytes, an unrepresentable size, or allocation failure returns NULL. */
void *cs_osdi_allocate_zeroed(uint64_t bytes);
void cs_osdi_free(void *storage);


/* S2 point evaluation uses the pinned native model; it provides no solver. */
typedef struct cs_osdi_evaluator cs_osdi_evaluator;
typedef struct cs_osdi_string_view { const char *data; uint64_t length; } cs_osdi_string_view;
typedef struct cs_osdi_value {
    uint32_t value_type; /* 0 real, 1 int32, 2 string */
    double real_value;
    int32_t integer_value;
    cs_osdi_string_view string_value;
} cs_osdi_value;
typedef struct cs_osdi_parameter_assignment {
    cs_osdi_string_view name;
    uint32_t scope; /* 0 model/default, 1 instance override */
    cs_osdi_value value;
} cs_osdi_parameter_assignment;
typedef struct cs_osdi_simulation_parameter {
    cs_osdi_string_view name;
    cs_osdi_value value; /* Only real or string is admitted. */
} cs_osdi_simulation_parameter;
typedef struct cs_osdi_evaluation_limits {
    uint64_t maximum_workspace_bytes;
    uint32_t maximum_diagnostic_count;
    uint64_t maximum_diagnostic_bytes;
    uint64_t maximum_string_bytes;
} cs_osdi_evaluation_limits;
typedef struct cs_osdi_prepare_options {
    double temperature_kelvin;
    uint32_t connected_terminal_count;
    const cs_osdi_parameter_assignment *parameters;
    uint64_t parameter_count;
    const cs_osdi_simulation_parameter *simulation_parameters;
    uint64_t simulation_parameter_count;
    cs_osdi_evaluation_limits limits;
} cs_osdi_prepare_options;
typedef struct cs_osdi_node_info_record {
    cs_osdi_string_view name;
    cs_osdi_string_view units;
    cs_osdi_string_view residual_units;
    uint32_t is_flow;
    uint32_t unknown_nature_kind;
    uint32_t unknown_nature_index;
    uint32_t residual_nature_kind;
    uint32_t residual_nature_index;
} cs_osdi_node_info_record;
typedef struct cs_osdi_parameter_info_record {
    cs_osdi_string_view name;
    cs_osdi_string_view description;
    cs_osdi_string_view units;
    uint32_t value_type;
    uint32_t array_length;
    uint32_t kind; /* 0 model, 1 instance, 2 operating point */
    uint32_t alias_count; /* Excludes the canonical name. */
} cs_osdi_parameter_info_record;
typedef struct cs_osdi_diagnostic {
    uint32_t level; /* Native level and LOG_FMT_ERR bit are retained. */
    cs_osdi_string_view message;
} cs_osdi_diagnostic;
typedef struct cs_osdi_evaluator_info_record {
    uint32_t node_count;
    uint32_t active_unknown_count;
    uint32_t state_count;
    const uint32_t *collapse_map; /* Dense active index or UINT32_MAX ground. */
    uint32_t diagnostic_count;
    const cs_osdi_diagnostic *diagnostics;
} cs_osdi_evaluator_info_record;
/* Stable mode numbers: DC=0, AC=1, transient=2, transient IC=3, noise=4. */
typedef struct cs_osdi_evaluation_context {
    uint32_t mode;
    double time_seconds;
    double noise_frequency_hz; /* Required only for noise; zero otherwise. */
} cs_osdi_evaluation_context;
typedef struct cs_osdi_noise_source_info_record {
    cs_osdi_string_view name;
    uint32_t kind;
    uint32_t positive_node;
    uint32_t negative_node; /* UINT32_MAX ground. */
} cs_osdi_noise_source_info_record;
typedef struct cs_osdi_result_view {
    uint32_t node_count;
    uint32_t active_unknown_count;
    uint32_t state_count;
    const double *f;
    const double *q;
    const double *g; /* Raw row-major node_count squared. */
    const double *c; /* Raw row-major node_count squared. */
    const double *next_state;
    uint32_t noise_count;
    const double *noise_densities;
    uint32_t has_maximum_timestep;
    double maximum_timestep;
    uint32_t model_flags;
    uint32_t diagnostic_count;
    const cs_osdi_diagnostic *diagnostics;
} cs_osdi_result_view;

int32_t cs_osdi_node_info(const cs_osdi_library *library, uint32_t descriptor,
                          uint32_t node, cs_osdi_node_info_record *out_info, cs_osdi_error *error);
int32_t cs_osdi_parameter_info(const cs_osdi_library *library, uint32_t descriptor,
                               uint32_t parameter, cs_osdi_parameter_info_record *out_info, cs_osdi_error *error);
int32_t cs_osdi_parameter_alias(const cs_osdi_library *library, uint32_t descriptor,
                                uint32_t parameter, uint32_t alias, cs_osdi_string_view *out_alias,
                                cs_osdi_error *error);
/* The caller's exclusive library lease must outlive the evaluator. All inputs are
 * borrowed only for this call; prepared string/simulation values are copied. */
int32_t cs_osdi_prepare(cs_osdi_library *library, uint32_t descriptor,
                        const cs_osdi_prepare_options *options,
                        cs_osdi_evaluator **out_evaluator, cs_osdi_error *error);
int32_t cs_osdi_evaluator_info(const cs_osdi_evaluator *evaluator,
                              cs_osdi_evaluator_info_record *out_info, cs_osdi_error *error);
int32_t cs_osdi_evaluator_parameter(cs_osdi_evaluator *evaluator, uint32_t parameter,
                                   uint32_t scope, cs_osdi_value *out_value,
                                   uint32_t *out_model_given, uint32_t *out_instance_given,
                                   cs_osdi_error *error);
/* Fixed F/Q/G/C + OP + DC + STATIC, time zero, limiting disabled. Views expire on
 * the next evaluator operation or destruction. Failure clears the output view. */
int32_t cs_osdi_evaluate(cs_osdi_evaluator *evaluator, const double *active_values,
                         uint64_t active_count, const double *previous_state,
                         uint64_t state_count, cs_osdi_result_view *out_result,
                         cs_osdi_error *error);
/* Same scoped views and lifetime as the DC entry point. */
int32_t cs_osdi_evaluate_context(cs_osdi_evaluator *evaluator,
                                const cs_osdi_evaluation_context *context,
                                const double *active_values, uint64_t active_count,
                                const double *previous_state, uint64_t state_count,
                                cs_osdi_result_view *out_result, cs_osdi_error *error);
int32_t cs_osdi_noise_source_info(const cs_osdi_library *library, uint32_t descriptor,
                                 uint32_t source, cs_osdi_noise_source_info_record *out_info,
                                 cs_osdi_error *error);
void cs_osdi_evaluator_destroy(cs_osdi_evaluator *evaluator);

#ifdef __cplusplus
}
#endif
#endif
