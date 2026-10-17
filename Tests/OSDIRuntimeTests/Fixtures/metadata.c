/* Metadata/lifetime oracle only: callbacks deliberately refuse model execution. */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "osdi_0_4.h"

#ifndef FIXTURE_KIND
#error FIXTURE_KIND must identify one bounded metadata fixture
#endif
#ifndef FIXTURE_MARKER
#error FIXTURE_MARKER must be a unique per-build file path
#endif

#if FIXTURE_KIND != 4
uint32_t OSDI_VERSION_MAJOR = FIXTURE_KIND == 2 ? 1 : 0;
#endif
uint32_t OSDI_VERSION_MINOR = FIXTURE_KIND == 3 ? 5 : 4;
uint32_t OSDI_NUM_DESCRIPTORS = FIXTURE_KIND == 6 ? UINT32_MAX : 2;
#if FIXTURE_KIND == 1
typedef struct ExtendedDescriptor {
    OsdiDescriptor descriptor;
    unsigned char future_extension[16];
} ExtendedDescriptor;
ExtendedDescriptor OSDI_DESCRIPTORS[2];
uint32_t OSDI_DESCRIPTOR_SIZE = sizeof(ExtendedDescriptor);
#else
OsdiDescriptor OSDI_DESCRIPTORS[2];
uint32_t OSDI_DESCRIPTOR_SIZE = FIXTURE_KIND == 5 ? sizeof(OsdiDescriptor)-8 : sizeof(OsdiDescriptor);
#endif

static OsdiNode nodes[] = {
    {"p", "V", "A", 16, 24, UINT32_MAX, UINT32_MAX, false},
    {"n", "V", "A", 32, 40, UINT32_MAX, UINT32_MAX, false}
};
static OsdiJacobianEntry jacobian[] = {
    {{0, 0}, 48, JACOBIAN_ENTRY_RESIST | JACOBIAN_ENTRY_REACT}
};
static OsdiJacobianEntry capacity_jacobian[] = {
    {{0, 0}, 48, JACOBIAN_ENTRY_RESIST | JACOBIAN_ENTRY_REACT},
    {{1, 1}, UINT32_MAX, 0}
};
static OsdiNodePair collapsible[] = {{0, UINT32_MAX}};
static OsdiNatureRef natures[] = {{NATREF_NONE, 0}, {NATREF_NONE, 0}};

static void *reject_access(void *instance, void *model, uint32_t id, uint32_t flags) {
    (void)instance; (void)model; (void)id; (void)flags; abort();
}
static void reject_setup_model(void *handle, void *model, OsdiSimParas *paras, OsdiInitInfo *info) {
    (void)handle; (void)model; (void)paras; (void)info; abort();
}
static void reject_setup_instance(void *handle, void *instance, void *model, double temperature,
                                  uint32_t terminals, OsdiSimParas *paras, OsdiInitInfo *info) {
    (void)handle; (void)instance; (void)model; (void)temperature;
    (void)terminals; (void)paras; (void)info; abort();
}
static uint32_t reject_eval(void *handle, void *instance, void *model, OsdiSimInfo *info) {
    (void)handle; (void)instance; (void)model; (void)info; return EVAL_RET_FLAG_FATAL;
}
static void reject_noise(void *instance, void *model, double frequency, double *destination) {
    (void)instance; (void)model; (void)frequency; (void)destination; abort();
}
static void reject_load(void *instance, void *model, double *destination) {
    (void)instance; (void)model; (void)destination; abort();
}
static void reject_rhs_dc(void *instance, void *model, double *destination, double *previous) {
    (void)instance; (void)model; (void)destination; (void)previous; abort();
}
static void reject_rhs_tran(void *instance, void *model, double *destination, double *previous, double alpha) {
    (void)instance; (void)model; (void)destination; (void)previous; (void)alpha; abort();
}
static void reject_jacobian(void *instance, void *model) {
    (void)instance; (void)model; abort();
}
static void reject_jacobian_alpha(void *instance, void *model, double alpha) {
    (void)instance; (void)model; (void)alpha; abort();
}
static uint32_t reject_given(void *storage, uint32_t id) {
    (void)storage; (void)id; abort();
}
static void reject_offset(void *instance, void *model, size_t offset) {
    (void)instance; (void)model; (void)offset; abort();
}
static void reject_noise_parameters(void *instance, void *model, double *power, double *exponent) {
    (void)instance; (void)model; (void)power; (void)exponent; abort();
}

static void record_lifetime(const char *event) {
    FILE *file = fopen(FIXTURE_MARKER, "a");
    if (file == NULL) abort();
    if (fputs(event, file) == EOF || fclose(file) != 0) abort();
}

__attribute__((constructor)) static void construct_fixture(void) {
    record_lifetime("load\n");
    for (unsigned int index = 0; index < 2; ++index) {
#if FIXTURE_KIND == 1
        OsdiDescriptor *descriptor = &OSDI_DESCRIPTORS[index].descriptor;
        memset(OSDI_DESCRIPTORS[index].future_extension, 0xa5, 16);
#else
        OsdiDescriptor *descriptor = &OSDI_DESCRIPTORS[index];
#endif
        *descriptor = (OsdiDescriptor){
            .name = index == 0 ? "metadata_first" : "metadata_second",
            .num_nodes = 2, .num_terminals = 2, .nodes = nodes,
            .num_jacobian_entries = 1, .jacobian_entries = jacobian,
            .collapsed_offset = 0,
            .node_mapping_offset = 0, .jacobian_ptr_resist_offset = 8,
            .state_idx_off = 0, .bound_step_offset = UINT32_MAX,
            .instance_size = 64, .model_size = 16,
            .access = reject_access, .setup_model = reject_setup_model,
            .setup_instance = reject_setup_instance, .eval = reject_eval,
            .load_noise = reject_noise,
            .load_residual_resist = reject_load, .load_residual_react = reject_load,
            .load_limit_rhs_resist = reject_load, .load_limit_rhs_react = reject_load,
            .load_spice_rhs_dc = reject_rhs_dc, .load_spice_rhs_tran = reject_rhs_tran,
            .load_jacobian_resist = reject_jacobian,
            .load_jacobian_react = reject_jacobian_alpha,
            .load_jacobian_tran = reject_jacobian_alpha,
            .given_flag_model = reject_given, .given_flag_instance = reject_given,
            .num_resistive_jacobian_entries = 1, .num_reactive_jacobian_entries = 1,
            .write_jacobian_array_resist = reject_load,
            .write_jacobian_array_react = reject_load,
            .load_jacobian_with_offset_resist = reject_offset,
            .load_jacobian_with_offset_react = reject_offset,
            .load_noise_params = reject_noise_parameters,
            .unknown_nature = natures, .residual_nature = natures
        };
        switch (FIXTURE_KIND) {
        case 7: descriptor->num_nodes = UINT32_MAX; break;
        case 8: descriptor->node_mapping_offset = UINT32_MAX-7; break;
        case 9: descriptor->num_jacobian_entries = UINT32_MAX; break;
        case 10: descriptor->node_mapping_offset = 1; break;
        case 11: jacobian[0].nodes.node_1 = 2; break;
        case 12: descriptor->num_terminals = 3; break;
        case 13: descriptor->nodes = NULL; break;
        case 14: descriptor->eval = NULL; break;
        case 15: nodes[0].resist_residual_off = 60; break;
        case 16: jacobian[0].react_ptr_off = 3; break;
        case 17: descriptor->jacobian_entries = NULL; break;
        case 18: descriptor->instance_size = UINT32_MAX; descriptor->model_size = UINT32_MAX; break;
        case 19: descriptor->num_collapsible = 1; descriptor->collapsible = collapsible;
                 descriptor->collapsed_offset = 56; break;
        case 20: descriptor->num_collapsible = 1; descriptor->collapsible = collapsible;
                 descriptor->collapsed_offset = 56; collapsible[0].node_1 = UINT32_MAX; break;
        case 21: descriptor->setup_model = NULL; break;
        case 22: case 23: case 24: case 25:
            descriptor->num_jacobian_entries = 2;
            descriptor->jacobian_entries = capacity_jacobian;
            descriptor->instance_size = 80;
            descriptor->jacobian_ptr_resist_offset = 56;
            descriptor->num_resistive_jacobian_entries = FIXTURE_KIND == 23 ? 0
                : (FIXTURE_KIND == 25 ? UINT32_MAX : 2);
            descriptor->num_reactive_jacobian_entries = FIXTURE_KIND == 24 ? 3 : 2;
            break;
        default: break;
        }
    }
}

__attribute__((destructor)) static void destruct_fixture(void) {
    record_lifetime("unload\n");
}
