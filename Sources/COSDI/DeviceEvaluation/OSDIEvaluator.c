#include "../Loading/OSDILibraryInternal.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct cs_block { struct cs_block *next; max_align_t alignment; } cs_block;
struct cs_osdi_evaluator {
    const OsdiDescriptor *descriptor;
    uint32_t descriptor_index;
    cs_osdi_evaluation_limits limits;
    uint64_t workspace_bytes;
    uint64_t string_bytes;
    cs_block *blocks;
    void *model;
    void *instance;
    uint32_t *parent;
    uint32_t *map;
    uint32_t active_count;
    double *solve;
    double *previous_state;
    double *next_state;
    double *noise_densities;
    double *f;
    double *q;
    double *g;
    double *c;
    OsdiSimParas simulation;
    cs_osdi_diagnostic *diagnostics;
    char *diagnostic_bytes;
    uint32_t diagnostic_count;
    uint64_t diagnostic_used;
    int diagnostic_overflow;
    int logged_error;
    uint32_t model_flags;
    int32_t failure_code;
};

static int32_t issue(cs_osdi_evaluator *e, cs_osdi_error *error, int32_t code,
                     const char *field, const char *detail) {
    if (e) e->failure_code = code;
    if (error) {
        memset(error, 0, sizeof(*error)); error->code = code;
        error->descriptor_index = e ? e->descriptor_index : UINT32_MAX;
        error->model_flags = e ? e->model_flags : 0;
        snprintf(error->field, sizeof(error->field), "%s", field);
        snprintf(error->detail, sizeof(error->detail), "%s", detail);
    }
    return code;
}
static cs_osdi_string_view view(const char *string) {
    return (cs_osdi_string_view){string, string ? (uint64_t)strlen(string) : 0};
}
static int same(cs_osdi_string_view input, const char *native) {
    size_t length = strlen(native);
    return input.length == length && (!length || !memcmp(input.data, native, length));
}
static void *allocate(cs_osdi_evaluator *e, uint64_t count, uint64_t size, cs_osdi_error *error) {
    if (!count) return NULL;
    if (size && count > (UINT64_MAX - sizeof(cs_block)) / size) {
        issue(e, error, CS_OSDI_RESOURCE_LIMIT, "workspace", "Allocation size overflow"); return NULL;
    }
    uint64_t bytes = count * size + sizeof(cs_block);
    if (bytes > SIZE_MAX || bytes > e->limits.maximum_workspace_bytes - e->workspace_bytes) {
        issue(e, error, CS_OSDI_RESOURCE_LIMIT, "workspace", "Evaluation workspace budget exceeded"); return NULL;
    }
    cs_block *block = calloc(1, (size_t)bytes);
    if (!block) { issue(e, error, CS_OSDI_ALLOCATION_FAILED, "workspace", "Evaluation allocation failed"); return NULL; }
    e->workspace_bytes += bytes; block->next = e->blocks; e->blocks = block;
    return block + 1;
}
static char *copy_string(cs_osdi_evaluator *e, cs_osdi_string_view input, cs_osdi_error *error) {
    if (input.length == UINT64_MAX || input.length + 1 > e->limits.maximum_string_bytes - e->string_bytes) {
        issue(e, error, CS_OSDI_RESOURCE_LIMIT, "string", "Configured string budget exceeded"); return NULL;
    }
    if ((input.length && !input.data) || input.length > SIZE_MAX
        || (input.length && memchr(input.data, 0, (size_t)input.length))) {
        issue(e, error, CS_OSDI_INVALID_ARGUMENT, "string", "String bytes are invalid or contain embedded NUL"); return NULL;
    }
    char *result = allocate(e, input.length + 1, 1, error);
    if (!result) return NULL;
    if (input.length) memcpy(result, input.data, (size_t)input.length);
    e->string_bytes += input.length + 1;
    return result;
}
static int32_t valid_name(cs_osdi_evaluator *e, cs_osdi_string_view name, cs_osdi_error *error) {
    if (name.length > e->limits.maximum_string_bytes)
        return issue(e, error, CS_OSDI_RESOURCE_LIMIT, "name", "Name exceeds the configured string budget");
    if (!name.data || !name.length || name.length > SIZE_MAX || memchr(name.data, 0, (size_t)name.length))
        return issue(e, error, CS_OSDI_INVALID_ARGUMENT, "name", "A bounded, nonempty name without embedded NUL is required");
    return 0;
}
static void log_message(void *handle, const char *message, uint32_t level) {
    cs_osdi_evaluator *e = handle;
    if (e) {
        if ((level & LOG_LVL_MASK) >= LOG_LVL_ERR) e->logged_error = 1;
        uint64_t remaining = e->limits.maximum_diagnostic_bytes - e->diagnostic_used;
        const char *end = message && remaining <= SIZE_MAX ? memchr(message, 0, (size_t)remaining) : NULL;
        if (!end || e->diagnostic_count == e->limits.maximum_diagnostic_count) e->diagnostic_overflow = 1;
        else {
            uint64_t length = (uint64_t)(end - message);
            char *destination = e->diagnostic_bytes + e->diagnostic_used;
            memcpy(destination, message, (size_t)length + 1);
            e->diagnostics[e->diagnostic_count++] = (cs_osdi_diagnostic){level, {destination, length}};
            e->diagnostic_used += length + 1;
        }
    }
    if (message && !(level & LOG_FMT_ERR)) free((void *)message);
}
static int32_t check_callback(cs_osdi_evaluator *e, cs_osdi_error *error, int setup) {
    if (e->diagnostic_overflow) return issue(e, error, CS_OSDI_DIAGNOSTIC_LIMIT, "diagnostics", "Native diagnostic budget exceeded");
    if (e->model_flags) return issue(e, error, setup ? CS_OSDI_SETUP_FAILED : CS_OSDI_EVALUATION_FAILED,
                                     "model_flags", "Native model requested fatal, finish, stop, limiting or an unknown outcome");
    if (e->logged_error) return issue(e, error, setup ? CS_OSDI_SETUP_FAILED : CS_OSDI_EVALUATION_FAILED,
                                      "diagnostics", "Native model logged an error");
    return 0;
}
static int32_t check_setup(cs_osdi_evaluator *e, OsdiInitInfo *result, cs_osdi_error *error) {
    e->model_flags = result->flags;
    uint32_t count = result->num_errors;
    OsdiInitError first = {0};
    if (count && result->errors) first = result->errors[0];
    int invalid = count && !result->errors;
    free(result->errors); result->errors = NULL;
    int32_t status = check_callback(e, error, 1);
    if (!status && count) status = issue(e, error, invalid ? CS_OSDI_MALFORMED_DESCRIPTOR : CS_OSDI_SETUP_FAILED,
                                       "setup_errors", invalid ? "Native setup returned a null error array" : "Native parameter initialization was rejected");
    if (error) { error->setup_error_count = count; error->setup_error_code = first.code;
                 error->setup_parameter_id = count ? first.payload.parameter_id : UINT32_MAX; }
    return status;
}
static uint32_t representative(cs_osdi_evaluator *e, uint32_t node) {
    while (e->parent[node] != node) node = e->parent[node];
    return node;
}
static int32_t bind_topology(cs_osdi_evaluator *e, cs_osdi_error *error) {
    const OsdiDescriptor *d = e->descriptor;
    uint32_t n = d->num_nodes;
    for (uint64_t i = 0; i <= n; ++i) e->parent[i] = (uint32_t)i;
    const unsigned char *instance = e->instance;
    const bool *collapsed = (const bool *)(instance + d->collapsed_offset);
    for (uint32_t i = 0; i < d->num_collapsible; ++i) if (collapsed[i]) {
        uint32_t a = representative(e, d->collapsible[i].node_1);
        uint32_t b = representative(e, d->collapsible[i].node_2 == UINT32_MAX ? n : d->collapsible[i].node_2);
        if (a == n) e->parent[b] = n;
        else if (b == n) e->parent[a] = n;
        else if (a < b) e->parent[b] = a;
        else e->parent[a] = b;
    }
    for (uint32_t i = 0; i < n; ++i) e->map[i] = UINT32_MAX;
    for (uint32_t i = 0; i < n; ++i) if (representative(e, i) == i) e->map[i] = e->active_count++;
    uint32_t *mapping = (uint32_t *)((unsigned char *)e->instance + d->node_mapping_offset);
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t root = representative(e, i);
        e->map[i] = root == n ? UINT32_MAX : e->map[root];
        mapping[i] = e->map[i] == UINT32_MAX ? 0 : e->map[i] + 1;
    }
    double **resistive = (double **)((unsigned char *)e->instance + d->jacobian_ptr_resist_offset);
    for (uint32_t i = 0; i < d->num_jacobian_entries; ++i) {
        const OsdiJacobianEntry *entry = d->jacobian_entries + i;
        uint64_t position = (uint64_t)entry->nodes.node_1 * n + entry->nodes.node_2;
        resistive[i] = e->g + position;
        if (entry->react_ptr_off != UINT32_MAX)
            *(double **)((unsigned char *)e->instance + entry->react_ptr_off) = e->c + position;
    }
    uint32_t *indices = (uint32_t *)((unsigned char *)e->instance + d->state_idx_off);
    for (uint32_t i = 0; i < d->num_states; ++i) indices[i] = i;
    (void)error;
    return 0;
}
static int32_t find_parameter(cs_osdi_evaluator *e, cs_osdi_string_view name, uint32_t *out,
                               cs_osdi_error *error) {
    int32_t status = valid_name(e, name, error); if (status) return status;
    const OsdiDescriptor *d = e->descriptor;
    uint32_t found = UINT32_MAX;
    for (uint32_t i = 0; i < d->num_params; ++i) {
        const OsdiParamOpvar *parameter = d->param_opvar + i;
        int matches = 0;
        for (uint64_t a = 0; a <= parameter->num_alias; ++a) if (same(name, parameter->name[a])) matches = 1;
        if (matches) {
            if (found != UINT32_MAX) return issue(e, error, CS_OSDI_AMBIGUOUS_PARAMETER, "parameter", "Parameter name or alias is ambiguous");
            found = i;
        }
    }
    if (found == UINT32_MAX) return issue(e, error, CS_OSDI_PARAMETER_NOT_FOUND, "parameter", "Parameter name or alias was not found");
    *out = found; return 0;
}
static int32_t access_value(cs_osdi_evaluator *e, uint32_t id, uint32_t scope, int write,
                             void **out, cs_osdi_error *error) {
    const OsdiDescriptor *d = e->descriptor;
    if (id >= d->num_params || scope > 1)
        return issue(e, error, CS_OSDI_INVALID_ARGUMENT, "parameter", "Parameter index or scope is invalid");
    const OsdiParamOpvar *p = d->param_opvar + id;
    if (p->len) return issue(e, error, CS_OSDI_UNSUPPORTED_FEATURE, "parameter.array", "Array parameter access is unsupported in this profile");
    if (scope == 1 && p->flags >> 30 != 1)
        return issue(e, error, CS_OSDI_PARAMETER_SCOPE_MISMATCH, "parameter.scope", "Only instance parameters have an instance override");
    uint32_t type = p->flags & PARA_TY_MASK;
    uint64_t size = type == PARA_TY_REAL ? sizeof(double) : (type == PARA_TY_INT ? sizeof(int32_t) : sizeof(char *));
    uint64_t alignment = type == PARA_TY_REAL ? _Alignof(double) : (type == PARA_TY_INT ? _Alignof(int32_t) : _Alignof(char *));
    void *base = scope ? e->instance : e->model;
    uint64_t bytes = scope ? d->instance_size : d->model_size;
    void *pointer = d->access(e->instance, e->model, id,
        (write ? ACCESS_FLAG_SET : ACCESS_FLAG_READ) | (scope ? ACCESS_FLAG_INSTANCE : 0));
    uintptr_t address = (uintptr_t)pointer, start = (uintptr_t)base;
    if (!pointer || address < start || address - start > bytes || size > bytes - (address - start) || address % alignment)
        return issue(e, error, CS_OSDI_MALFORMED_DESCRIPTOR, "parameter.access", "Native parameter access returned an invalid storage span");
    *out = pointer; return 0;
}
static int32_t assignments(cs_osdi_evaluator *e, const cs_osdi_prepare_options *options, cs_osdi_error *error) {
    const OsdiDescriptor *d = e->descriptor;
    unsigned char *seen = allocate(e, (uint64_t)d->num_params * 2, 1, error);
    if (d->num_params && !seen) return e->failure_code;
    for (uint64_t i = 0; i < options->parameter_count; ++i) {
        const cs_osdi_parameter_assignment *a = options->parameters + i;
        uint32_t id;
        int32_t status = find_parameter(e, a->name, &id, error); if (status) return status;
        if (a->scope > 1) return issue(e, error, CS_OSDI_INVALID_ARGUMENT, "parameter.scope", "Unknown assignment scope");
        if (a->value.value_type != (d->param_opvar[id].flags & PARA_TY_MASK))
            return issue(e, error, CS_OSDI_PARAMETER_TYPE_MISMATCH, "parameter.type", "Assignment type disagrees with native metadata");
        uint64_t key = (uint64_t)id * 2 + a->scope;
        if (seen[key]) return issue(e, error, CS_OSDI_INVALID_ARGUMENT, "parameter.duplicate", "Parameter was assigned twice in one scope");
        seen[key] = 1;
        void *destination;
        status = access_value(e, id, a->scope, 1, &destination, error); if (status) return status;
        switch (a->value.value_type) {
        case PARA_TY_REAL:
            if (!isfinite(a->value.real_value)) return issue(e, error, CS_OSDI_NONFINITE_INPUT, "parameter.real", "Real parameter must be finite");
            *(double *)destination = a->value.real_value; break;
        case PARA_TY_INT: *(int32_t *)destination = a->value.integer_value; break;
        case PARA_TY_STR: {
            char *copy = copy_string(e, a->value.string_value, error); if (!copy) return e->failure_code;
            *(char **)destination = copy; break;
        }
        default: return issue(e, error, CS_OSDI_PARAMETER_TYPE_MISMATCH, "parameter.type", "Unknown assignment type");
        }
    }
    return 0;
}
static int32_t simulation_parameters(cs_osdi_evaluator *e, const cs_osdi_prepare_options *options, cs_osdi_error *error) {
    uint64_t real_count = 0, string_count = 0;
    for (uint64_t i = 0; i < options->simulation_parameter_count; ++i) {
        const cs_osdi_simulation_parameter *p = options->simulation_parameters + i;
        if (p->value.value_type == PARA_TY_REAL) ++real_count;
        else if (p->value.value_type == PARA_TY_STR) ++string_count;
        else return issue(e, error, CS_OSDI_PARAMETER_TYPE_MISMATCH, "simulation_parameter.type", "Simulation parameters are real or string");
        int32_t status = valid_name(e, p->name, error); if (status) return status;
        for (uint64_t j = 0; j < i; ++j) {
            const cs_osdi_simulation_parameter *previous = options->simulation_parameters + j;
            if (p->name.length == previous->name.length && !memcmp(p->name.data, previous->name.data, (size_t)p->name.length))
                return issue(e, error, CS_OSDI_INVALID_ARGUMENT, "simulation_parameter.duplicate", "Simulation parameter was supplied twice");
        }
    }
    e->simulation.names = allocate(e, real_count + 1, sizeof(char *), error);
    e->simulation.vals = allocate(e, real_count, sizeof(double), error);
    e->simulation.names_str = allocate(e, string_count + 1, sizeof(char *), error);
    e->simulation.vals_str = allocate(e, string_count + 1, sizeof(char *), error);
    if (!e->simulation.names || (real_count && !e->simulation.vals) || !e->simulation.names_str || !e->simulation.vals_str)
        return e->failure_code;
    uint64_t ri = 0, si = 0;
    for (uint64_t i = 0; i < options->simulation_parameter_count; ++i) {
        const cs_osdi_simulation_parameter *p = options->simulation_parameters + i;
        char *name = copy_string(e, p->name, error); if (!name) return e->failure_code;
        if (p->value.value_type == PARA_TY_REAL) {
            if (!isfinite(p->value.real_value)) return issue(e, error, CS_OSDI_NONFINITE_INPUT, "simulation_parameter.real", "Simulation value must be finite");
            e->simulation.names[ri] = name; e->simulation.vals[ri++] = p->value.real_value;
        } else {
            char *value = copy_string(e, p->value.string_value, error); if (!value) return e->failure_code;
            e->simulation.names_str[si] = name; e->simulation.vals_str[si++] = value;
        }
    }
    return 0;
}
void cs_osdi_evaluator_destroy(cs_osdi_evaluator *e) {
    if (!e) return;
    cs_block *block = e->blocks;
    while (block) { cs_block *next = block->next; free(block); block = next; }
    free(e);
}
int32_t cs_osdi_prepare(cs_osdi_library *library, uint32_t descriptor,
                        const cs_osdi_prepare_options *options, cs_osdi_evaluator **out, cs_osdi_error *error) {
    if (error) memset(error, 0, sizeof(*error));
    if (out) *out = NULL;
    if (!options || !out) return issue(NULL, error, CS_OSDI_INVALID_ARGUMENT, "options", "Preparation options and output are required");
    const OsdiDescriptor *d;
    int32_t status = cs_osdi_borrow_descriptor(library, descriptor, &d, error); if (status) return status;
    if (!isfinite(options->temperature_kelvin) || options->temperature_kelvin <= 0
        || options->connected_terminal_count > d->num_terminals
        || !options->limits.maximum_diagnostic_count || !options->limits.maximum_diagnostic_bytes
        || !options->limits.maximum_string_bytes || !options->limits.maximum_workspace_bytes
        || (options->parameter_count && !options->parameters) || (options->simulation_parameter_count && !options->simulation_parameters))
        return issue(NULL, error, CS_OSDI_INVALID_ARGUMENT, "options", "Preparation bounds, temperature or input arrays are invalid");
    if (options->limits.maximum_workspace_bytes < sizeof(cs_osdi_evaluator))
        return issue(NULL, error, CS_OSDI_RESOURCE_LIMIT, "workspace", "Workspace budget cannot hold the evaluator owner");
    if (d->absdelay_count) return issue(NULL, error, CS_OSDI_UNSUPPORTED_FEATURE, "absdelay", "Point evaluation does not provide delay history");
    if (options->parameter_count > options->limits.maximum_workspace_bytes / sizeof(cs_osdi_parameter_assignment)
        || options->simulation_parameter_count > options->limits.maximum_workspace_bytes / sizeof(cs_osdi_simulation_parameter))
        return issue(NULL, error, CS_OSDI_RESOURCE_LIMIT, "options", "Input counts exceed bounded preparation capacity");
    cs_osdi_evaluator *e = calloc(1, sizeof(*e));
    if (!e) return issue(NULL, error, CS_OSDI_ALLOCATION_FAILED, "workspace", "Evaluator allocation failed");
    e->descriptor = d; e->descriptor_index = descriptor; e->limits = options->limits; e->workspace_bytes = sizeof(*e);
#define ALLOC(field, count, type) do { e->field = allocate(e, (count), sizeof(type), error); if ((count) && !e->field) { status = e->failure_code; goto failed; } } while (0)
    ALLOC(model, d->model_size ? d->model_size : 1, unsigned char);
    ALLOC(instance, d->instance_size ? d->instance_size : 1, unsigned char);
    ALLOC(parent, (uint64_t)d->num_nodes + 1, uint32_t);
    ALLOC(map, d->num_nodes, uint32_t);
    ALLOC(solve, (uint64_t)d->num_nodes + 1, double);
    ALLOC(previous_state, d->num_states, double); ALLOC(next_state, d->num_states, double);
    ALLOC(noise_densities, d->num_noise_src, double);
    ALLOC(f, d->num_nodes, double); ALLOC(q, d->num_nodes, double);
    uint64_t matrix_count = (uint64_t)d->num_nodes * d->num_nodes;
    ALLOC(g, matrix_count, double); ALLOC(c, matrix_count, double);
    ALLOC(diagnostics, e->limits.maximum_diagnostic_count, cs_osdi_diagnostic);
    ALLOC(diagnostic_bytes, e->limits.maximum_diagnostic_bytes, char);
#undef ALLOC
    status = simulation_parameters(e, options, error); if (status) goto failed;
    status = cs_osdi_bind_logger(library, log_message, error); if (status) goto failed;
    status = assignments(e, options, error); if (status) goto failed;
    OsdiInitInfo initialized = {0};
    d->setup_model(e, e->model, &e->simulation, &initialized);
    status = check_setup(e, &initialized, error); if (status) goto failed;
    d->setup_instance(e, e->instance, e->model, options->temperature_kelvin,
                      options->connected_terminal_count, &e->simulation, &initialized);
    status = check_setup(e, &initialized, error); if (status) goto failed;
    status = bind_topology(e, error); if (status) goto failed;
    *out = e; return 0;
failed:
    cs_osdi_evaluator_destroy(e); return status;
}
int32_t cs_osdi_evaluator_info(const cs_osdi_evaluator *e, cs_osdi_evaluator_info_record *out, cs_osdi_error *error) {
    if (!e || !out) return issue(NULL, error, CS_OSDI_INVALID_ARGUMENT, "evaluator", "Evaluator and info output are required");
    if (error) memset(error, 0, sizeof(*error));
    *out = (cs_osdi_evaluator_info_record){e->descriptor->num_nodes, e->active_count,
        e->descriptor->num_states, e->map, e->diagnostic_count, e->diagnostics};
    return 0;
}
int32_t cs_osdi_evaluator_parameter(cs_osdi_evaluator *e, uint32_t parameter, uint32_t scope,
                                   cs_osdi_value *out, uint32_t *model_given, uint32_t *instance_given, cs_osdi_error *error) {
    if (!e || !out || !model_given || !instance_given)
        return issue(e, error, CS_OSDI_INVALID_ARGUMENT, "parameter", "Evaluator and value/given outputs are required");
    if (error) memset(error, 0, sizeof(*error));
    memset(out, 0, sizeof(*out));
    void *pointer;
    int32_t status = access_value(e, parameter, scope, 0, &pointer, error); if (status) return status;
    out->value_type = e->descriptor->param_opvar[parameter].flags & PARA_TY_MASK;
    if (out->value_type == PARA_TY_REAL) out->real_value = *(double *)pointer;
    else if (out->value_type == PARA_TY_INT) out->integer_value = *(int32_t *)pointer;
    else {
        const char *value = *(char **)pointer;
        const char *end = value ? memchr(value, 0, (size_t)(e->limits.maximum_string_bytes < SIZE_MAX ? e->limits.maximum_string_bytes : SIZE_MAX)) : NULL;
        if (!end) return issue(e, error, CS_OSDI_RESOURCE_LIMIT, "parameter.string", "Resolved string is not terminated within its budget");
        out->string_value = (cs_osdi_string_view){value, (uint64_t)(end - value)};
    }
    *model_given = e->descriptor->given_flag_model(e->model, parameter);
    *instance_given = e->descriptor->given_flag_instance(e->instance, parameter);
    return 0;
}
int32_t cs_osdi_evaluate(cs_osdi_evaluator *e, const double *active, uint64_t active_count,
                         const double *previous, uint64_t state_count, cs_osdi_result_view *out, cs_osdi_error *error) {
    const cs_osdi_evaluation_context context = {0, 0, 0};
    return cs_osdi_evaluate_context(e, &context, active, active_count, previous, state_count, out, error);
}
int32_t cs_osdi_evaluate_context(cs_osdi_evaluator *e, const cs_osdi_evaluation_context *context,
                                 const double *active, uint64_t active_count,
                                 const double *previous, uint64_t state_count,
                                 cs_osdi_result_view *out, cs_osdi_error *error) {
    if (out) memset(out, 0, sizeof(*out));
    if (error) memset(error, 0, sizeof(*error));
    if (!e || !out || active_count != e->active_count || state_count != e->descriptor->num_states
        || (active_count && !active) || (state_count && !previous))
        return issue(e, error, CS_OSDI_INVALID_ARGUMENT, "inputs", "Active unknown and previous-state counts must match the prepared layout");
    if (!context || context->mode > 4 || !isfinite(context->time_seconds) || context->time_seconds < 0
        || (context->mode != 2 && context->time_seconds != 0)
        || (context->mode == 4 ? (!isfinite(context->noise_frequency_hz) || context->noise_frequency_hz <= 0)
                               : context->noise_frequency_hz != 0))
        return issue(e, error, CS_OSDI_INVALID_ARGUMENT, "context", "Invalid analysis mode, absolute time or noise frequency");
    for (uint64_t i = 0; i < active_count; ++i) if (!isfinite(active[i]))
        return issue(e, error, CS_OSDI_NONFINITE_INPUT, "active_values", "Active unknown values must be finite");
    for (uint64_t i = 0; i < state_count; ++i) if (!isfinite(previous[i]))
        return issue(e, error, CS_OSDI_NONFINITE_INPUT, "previous_state", "Previous states must be finite");
    e->model_flags = 0; e->diagnostic_count = 0; e->diagnostic_used = 0; e->diagnostic_overflow = 0; e->logged_error = 0;
    e->solve[0] = 0;
    if (active_count) memmove(e->solve + 1, active, (size_t)active_count * sizeof(double));
    if (state_count) {
        memmove(e->previous_state, previous, (size_t)state_count * sizeof(double));
        memcpy(e->next_state, e->previous_state, (size_t)state_count * sizeof(double));
    }
    const OsdiDescriptor *d = e->descriptor;
    uint32_t flags = CALC_RESIST_RESIDUAL | CALC_REACT_RESIDUAL | CALC_RESIST_JACOBIAN | CALC_REACT_JACOBIAN | CALC_OP;
    switch (context->mode) {
    case 0: flags |= ANALYSIS_DC | ANALYSIS_STATIC; break;
    case 1: flags |= ANALYSIS_AC; break;
    case 2: flags |= ANALYSIS_TRAN; break;
    case 3: flags |= ANALYSIS_TRAN | ANALYSIS_IC | ANALYSIS_STATIC; break;
    case 4: flags |= ANALYSIS_AC | ANALYSIS_NOISE | CALC_NOISE; break;
    }
    double *bound = d->bound_step_offset == UINT32_MAX ? NULL : (double *)((unsigned char *)e->instance + d->bound_step_offset);
    if (bound) *bound = INFINITY;
    OsdiSimInfo info = {e->simulation, context->time_seconds, e->solve, e->previous_state, e->next_state, flags};
    e->model_flags = d->eval(e, e->instance, e->model, &info);
    int32_t status = check_callback(e, error, 0); if (status) return status;
    uint64_t matrix_count = (uint64_t)d->num_nodes * d->num_nodes;
    if (matrix_count) { memset(e->g, 0, (size_t)matrix_count * sizeof(double)); memset(e->c, 0, (size_t)matrix_count * sizeof(double)); }
    d->load_jacobian_resist(e->instance, e->model);
    d->load_jacobian_react(e->instance, e->model, 1);
    for (uint32_t i = 0; i < d->num_nodes; ++i) {
        const OsdiNode *node = d->nodes + i;
        e->f[i] = node->resist_residual_off == UINT32_MAX ? 0 : *(double *)((unsigned char *)e->instance + node->resist_residual_off);
        e->q[i] = node->react_residual_off == UINT32_MAX ? 0 : *(double *)((unsigned char *)e->instance + node->react_residual_off);
        if (!isfinite(e->f[i]) || !isfinite(e->q[i])) return issue(e, error, CS_OSDI_NONFINITE_OUTPUT, "residual", "Native residual or charge is nonfinite");
    }
    for (uint64_t i = 0; i < matrix_count; ++i) if (!isfinite(e->g[i]) || !isfinite(e->c[i]))
        return issue(e, error, CS_OSDI_NONFINITE_OUTPUT, "jacobian", "Native raw Jacobian is nonfinite");
    for (uint64_t i = 0; i < state_count; ++i) if (!isfinite(e->next_state[i]))
        return issue(e, error, CS_OSDI_NONFINITE_OUTPUT, "next_state", "Native next state is nonfinite");
    if (bound && (isnan(*bound) || *bound <= 0))
        return issue(e, error, CS_OSDI_NONFINITE_OUTPUT, "bound_step", "Native timestep bound must be positive or positive infinity");
    uint32_t noise_count = context->mode == 4 ? d->num_noise_src : 0;
    if (noise_count) {
        memset(e->noise_densities, 0, (size_t)noise_count * sizeof(double));
        d->load_noise(e->instance, e->model, context->noise_frequency_hz, e->noise_densities);
        status = check_callback(e, error, 0); if (status) return status;
        for (uint32_t i = 0; i < noise_count; ++i) {
            if (!isfinite(e->noise_densities[i]))
                return issue(e, error, CS_OSDI_NONFINITE_OUTPUT, "noise_density", "Native noise power must be finite");
            /* Fixed-bias source PSD is the magnitude of signed OSDI noise power. */
            e->noise_densities[i] = fabs(e->noise_densities[i]);
        }
    }
    *out = (cs_osdi_result_view){.node_count=d->num_nodes, .active_unknown_count=e->active_count,
        .state_count=d->num_states, .f=e->f, .q=e->q, .g=e->g, .c=e->c, .next_state=e->next_state,
        .noise_count=noise_count, .noise_densities=noise_count ? e->noise_densities : NULL,
        .has_maximum_timestep=bound && isfinite(*bound), .maximum_timestep=bound && isfinite(*bound) ? *bound : 0,
        .model_flags=e->model_flags, .diagnostic_count=e->diagnostic_count, .diagnostics=e->diagnostics};
    return 0;
}
int32_t cs_osdi_node_info(const cs_osdi_library *library, uint32_t descriptor, uint32_t node,
                          cs_osdi_node_info_record *out, cs_osdi_error *error) {
    const OsdiDescriptor *d;
    int32_t status = cs_osdi_borrow_descriptor(library, descriptor, &d, error); if (status) return status;
    if (!out || node >= d->num_nodes) return issue(NULL, error, CS_OSDI_INVALID_ARGUMENT, "node", "Node index or output is invalid");
    const OsdiNode *n = d->nodes + node;
    if (error) memset(error, 0, sizeof(*error));
    *out = (cs_osdi_node_info_record){view(n->name), view(n->units), view(n->residual_units), n->is_flow,
        d->unknown_nature[node].ref_type, d->unknown_nature[node].index, d->residual_nature[node].ref_type, d->residual_nature[node].index,
        n->react_residual_off != UINT32_MAX};
    return 0;
}
int32_t cs_osdi_parameter_info(const cs_osdi_library *library, uint32_t descriptor, uint32_t parameter,
                               cs_osdi_parameter_info_record *out, cs_osdi_error *error) {
    const OsdiDescriptor *d;
    int32_t status = cs_osdi_borrow_descriptor(library, descriptor, &d, error); if (status) return status;
    if (!out || parameter >= (uint64_t)d->num_params + d->num_opvars)
        return issue(NULL, error, CS_OSDI_INVALID_ARGUMENT, "parameter", "Parameter index or output is invalid");
    const OsdiParamOpvar *p = d->param_opvar + parameter;
    if (error) memset(error, 0, sizeof(*error));
    *out = (cs_osdi_parameter_info_record){view(p->name[0]), view(p->description), view(p->units),
        p->flags & PARA_TY_MASK, p->len, p->flags >> 30, p->num_alias};
    return 0;
}
int32_t cs_osdi_parameter_alias(const cs_osdi_library *library, uint32_t descriptor, uint32_t parameter,
                                uint32_t alias, cs_osdi_string_view *out, cs_osdi_error *error) {
    const OsdiDescriptor *d;
    int32_t status = cs_osdi_borrow_descriptor(library, descriptor, &d, error); if (status) return status;
    if (!out || parameter >= (uint64_t)d->num_params + d->num_opvars || alias >= d->param_opvar[parameter].num_alias)
        return issue(NULL, error, CS_OSDI_INVALID_ARGUMENT, "parameter.alias", "Parameter alias index or output is invalid");
    if (error) memset(error, 0, sizeof(*error));
    *out = view(d->param_opvar[parameter].name[(uint64_t)alias + 1]);
    return 0;
}

int32_t cs_osdi_noise_source_info(const cs_osdi_library *library, uint32_t descriptor,
                                 uint32_t source, cs_osdi_noise_source_info_record *out, cs_osdi_error *error) {
    const OsdiDescriptor *d;
    int32_t status = cs_osdi_borrow_descriptor(library, descriptor, &d, error); if (status) return status;
    if (!out || source >= d->num_noise_src) return issue(NULL, error, CS_OSDI_INVALID_ARGUMENT, "noise_source", "Noise source index or output is invalid");
    if (error) memset(error, 0, sizeof(*error));
    const OsdiNoiseSource *n = d->noise_sources + source;
    *out = (cs_osdi_noise_source_info_record){view(n->name), d->noise_source_type[source], n->nodes.node_1, n->nodes.node_2};
    return 0;
}
