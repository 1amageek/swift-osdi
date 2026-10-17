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
    CS_OSDI_DESCRIPTOR_INDEX_OUT_OF_RANGE = 11
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

#ifdef __cplusplus
}
#endif
#endif
