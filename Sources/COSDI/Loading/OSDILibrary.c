#include "COSDI.h"
#include "../ABI/osdi_0_4.h"
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__) && defined(__aarch64__)
#include <dlfcn.h>
#include <fcntl.h>
#include <mach-o/loader.h>
#include <mach-o/fat.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

struct cs_osdi_library {
    void *handle;
    const unsigned char *descriptors;
    uint32_t count;
    uint64_t stride;
    cs_osdi_descriptor_info_record *information;
    int validated;
};

typedef struct cs_validation {
    uint64_t remaining;
    uint64_t maximum_storage;
    uint32_t index;
    cs_osdi_error *error;
} cs_validation;

static int32_t failure(cs_osdi_error *error, int32_t code, uint32_t index,
                       const char *field, const char *format, ...) {
    if (error) {
        memset(error, 0, sizeof(*error));
        error->code = code;
        error->descriptor_index = index;
        snprintf(error->field, sizeof(error->field), "%s", field);
        va_list args;
        va_start(args, format);
        vsnprintf(error->detail, sizeof(error->detail), format, args);
        va_end(args);
    }
    return code;
}

static int32_t malformed(cs_validation *v, const char *field, const char *detail) {
    return failure(v->error, CS_OSDI_MALFORMED_DESCRIPTOR, v->index, field, "%s", detail);
}

static int32_t consume(cs_validation *v, uint64_t bytes, const char *field) {
    if (bytes > v->remaining || bytes > SIZE_MAX)
        return failure(v->error, CS_OSDI_RESOURCE_LIMIT, v->index, field, "Metadata budget exceeded");
    v->remaining -= bytes;
    return CS_OSDI_SUCCESS;
}

static int32_t table(cs_validation *v, const void *pointer, uint64_t count,
                     uint64_t size, size_t alignment, const char *field) {
    if (count > UINT64_MAX / size)
        return failure(v->error, CS_OSDI_RESOURCE_LIMIT, v->index, field, "Table size overflow");
    uint64_t bytes = count * size;
    if (bytes && (!pointer || (uintptr_t)pointer % alignment))
        return malformed(v, field, "Required table is null or misaligned");
    if (pointer && bytes > UINTPTR_MAX - (uintptr_t)pointer)
        return malformed(v, field, "Table address overflow");
    return consume(v, bytes, field);
}

static int32_t text(cs_validation *v, const char *string, const char *field, uint64_t *length) {
    if (!string) return malformed(v, field, "Required string is null");
    uint64_t bound = v->remaining;
    if (bound > SIZE_MAX) bound = SIZE_MAX;
    if (bound > UINTPTR_MAX - (uintptr_t)string) bound = UINTPTR_MAX - (uintptr_t)string;
    const char *end = memchr(string, 0, (size_t)bound);
    if (!end) return failure(v->error, CS_OSDI_RESOURCE_LIMIT, v->index, field,
                             "String is not terminated within the metadata budget");
    uint64_t bytes = (uint64_t)(end - string) + 1;
    if (length) *length = bytes - 1;
    return consume(v, bytes, field);
}

static int32_t span(cs_validation *v, uint32_t offset, uint64_t count, uint64_t size,
                    size_t alignment, uint32_t storage, int optional, const char *field) {
    if (offset == UINT32_MAX) {
        if (optional) return CS_OSDI_SUCCESS;
        return malformed(v, field, "Required offset uses the absent sentinel");
    }
    if (count > UINT64_MAX / size || offset > storage || count * size > (uint64_t)storage - offset)
        return malformed(v, field, "Instance span exceeds storage");
    if (offset % alignment) return malformed(v, field, "Instance offset is misaligned");
    return CS_OSDI_SUCCESS;
}

static int node_pair(OsdiNodePair pair, uint32_t nodes, int ground_second) {
    return pair.node_1 < nodes && (pair.node_2 < nodes || (ground_second && pair.node_2 == UINT32_MAX));
}

#define CHECK(call) do { int32_t status = (call); if (status) return status; } while (0)
#define TABLE(field, count, type) CHECK(table(v, d->field, count, sizeof(type), _Alignof(type), #field))
#define SPAN(field, count, type, optional) CHECK(span(v, d->field, count, sizeof(type), _Alignof(type), d->instance_size, optional, #field))
#define CALLBACK(field) do { if (!d->field) return malformed(v, #field, "Required callback is null"); } while (0)

static int32_t validate_descriptor(cs_validation *v, const OsdiDescriptor *d,
                                    cs_osdi_descriptor_info_record *info) {
    if (d->num_terminals > d->num_nodes || d->num_instance_params > d->num_params)
        return malformed(v, "counts", "Terminal or instance-parameter count exceeds its parent count");
    if ((uint64_t)d->instance_size + d->model_size > v->maximum_storage)
        return failure(v->error, CS_OSDI_RESOURCE_LIMIT, v->index, "storage", "Combined model and instance storage exceeds budget");
    CHECK(text(v, d->name, "name", &info->name_length));
    if (!info->name_length) return malformed(v, "name", "Model name is empty");
    TABLE(nodes, d->num_nodes, OsdiNode);
    TABLE(jacobian_entries, d->num_jacobian_entries, OsdiJacobianEntry);
    TABLE(collapsible, d->num_collapsible, OsdiNodePair);
    TABLE(noise_sources, d->num_noise_src, OsdiNoiseSource);
    TABLE(param_opvar, (uint64_t)d->num_params + d->num_opvars, OsdiParamOpvar);
    TABLE(inputs, d->num_inputs, OsdiNodePair);
    TABLE(unknown_nature, d->num_nodes, OsdiNatureRef);
    TABLE(residual_nature, d->num_nodes, OsdiNatureRef);
    TABLE(noise_source_type, d->num_noise_src, uint32_t);
    TABLE(absdelay_info, d->absdelay_count, OsdiAbsDelayInfo);
    SPAN(node_mapping_offset, d->num_nodes, uint32_t, 0);
    SPAN(jacobian_ptr_resist_offset, d->num_jacobian_entries, double *, 0);
    SPAN(collapsed_offset, d->num_collapsible, bool, 0);
    SPAN(state_idx_off, d->num_states, uint32_t, 0);
    SPAN(bound_step_offset, 1, double, 1);
    CALLBACK(access); CALLBACK(setup_model); CALLBACK(setup_instance); CALLBACK(eval);
    CALLBACK(load_noise); CALLBACK(load_residual_resist); CALLBACK(load_residual_react);
    CALLBACK(load_limit_rhs_resist); CALLBACK(load_limit_rhs_react);
    CALLBACK(load_spice_rhs_dc); CALLBACK(load_spice_rhs_tran);
    CALLBACK(load_jacobian_resist); CALLBACK(load_jacobian_react); CALLBACK(load_jacobian_tran);
    CALLBACK(given_flag_model); CALLBACK(given_flag_instance);
    CALLBACK(write_jacobian_array_resist); CALLBACK(write_jacobian_array_react);
    CALLBACK(load_jacobian_with_offset_resist); CALLBACK(load_jacobian_with_offset_react);
    CALLBACK(load_noise_params);
    for (uint32_t i = 0; i < d->num_nodes; ++i) {
        const OsdiNode *node = d->nodes + i;
        CHECK(text(v, node->name, "nodes.name", NULL));
        CHECK(text(v, node->units, "nodes.units", NULL));
        CHECK(text(v, node->residual_units, "nodes.residual_units", NULL));
        CHECK(span(v, node->resist_residual_off, 1, sizeof(double), _Alignof(double), d->instance_size, 1, "nodes.resist_residual_off"));
        CHECK(span(v, node->react_residual_off, 1, sizeof(double), _Alignof(double), d->instance_size, 1, "nodes.react_residual_off"));
        CHECK(span(v, node->resist_limit_rhs_off, 1, sizeof(double), _Alignof(double), d->instance_size, 1, "nodes.resist_limit_rhs_off"));
        CHECK(span(v, node->react_limit_rhs_off, 1, sizeof(double), _Alignof(double), d->instance_size, 1, "nodes.react_limit_rhs_off"));
        if (d->unknown_nature[i].ref_type > NATREF_DISCIPLINE_POTENTIAL || d->residual_nature[i].ref_type > NATREF_DISCIPLINE_POTENTIAL)
            return malformed(v, "nature", "Unknown nature-reference kind");
    }
    uint32_t resistive = 0, reactive = 0;
    for (uint32_t i = 0; i < d->num_jacobian_entries; ++i) {
        const OsdiJacobianEntry *entry = d->jacobian_entries + i;
        if (!node_pair(entry->nodes, d->num_nodes, 0)) return malformed(v, "jacobian_entries.nodes", "Jacobian node index is out of range");
        if (entry->flags & ~UINT32_C(15)) return malformed(v, "jacobian_entries.flags", "Unknown Jacobian flag");
        resistive += !!(entry->flags & JACOBIAN_ENTRY_RESIST);
        reactive += !!(entry->flags & JACOBIAN_ENTRY_REACT);
        CHECK(span(v, entry->react_ptr_off, 1, sizeof(double *), _Alignof(double *), d->instance_size,
                   !(entry->flags & JACOBIAN_ENTRY_REACT), "jacobian_entries.react_ptr_off"));
    }
    // Exported array capacities can retain entries eliminated after the generator counted them.
    if (resistive > d->num_resistive_jacobian_entries || reactive > d->num_reactive_jacobian_entries)
        return malformed(v, "jacobian_counts", "Jacobian capacity does not cover active entries");
    uint64_t capacity_budget = v->maximum_storage - ((uint64_t)d->instance_size + d->model_size);
    if (d->num_resistive_jacobian_entries > capacity_budget / sizeof(double))
        return failure(v->error, CS_OSDI_RESOURCE_LIMIT, v->index, "jacobian_counts", "Resistive Jacobian capacity exceeds storage budget");
    capacity_budget -= (uint64_t)d->num_resistive_jacobian_entries * sizeof(double);
    if (d->num_reactive_jacobian_entries > capacity_budget / sizeof(double))
        return failure(v->error, CS_OSDI_RESOURCE_LIMIT, v->index, "jacobian_counts", "Reactive Jacobian capacity exceeds storage budget");
    for (uint32_t i = 0; i < d->num_collapsible; ++i)
        if (!node_pair(d->collapsible[i], d->num_nodes, 1)) return malformed(v, "collapsible", "Collapse node index is out of range");
    for (uint32_t i = 0; i < d->num_inputs; ++i)
        if (!node_pair(d->inputs[i], d->num_nodes, 1)) return malformed(v, "inputs", "Input node index is out of range");
    for (uint32_t i = 0; i < d->num_noise_src; ++i) {
        CHECK(text(v, d->noise_sources[i].name, "noise_sources.name", NULL));
        if (!node_pair(d->noise_sources[i].nodes, d->num_nodes, 1)) return malformed(v, "noise_sources.nodes", "Noise node index is out of range");
        if (d->noise_source_type[i] > NOISE_TYPE_TABLE) return malformed(v, "noise_source_type", "Unknown noise-source type");
    }
    for (uint64_t i = 0, total = (uint64_t)d->num_params + d->num_opvars; i < total; ++i) {
        const OsdiParamOpvar *parameter = d->param_opvar + i;
        CHECK(table(v, parameter->name, (uint64_t)parameter->num_alias + 1, sizeof(char *), _Alignof(char *), "param_opvar.name"));
        for (uint64_t alias = 0; alias <= parameter->num_alias; ++alias)
            CHECK(text(v, parameter->name[alias], "param_opvar.name", NULL));
        CHECK(text(v, parameter->description, "param_opvar.description", NULL));
        CHECK(text(v, parameter->units, "param_opvar.units", NULL));
        uint32_t kind = parameter->flags >> 30;
        uint32_t expected = i < d->num_instance_params ? 1 : (i < d->num_params ? 0 : 2);
        if (kind != expected || (parameter->flags & PARA_TY_MASK) > PARA_TY_STR)
            return malformed(v, "param_opvar.flags", "Parameter kind or scalar type is invalid");
        uint64_t element = (parameter->flags & PARA_TY_MASK) == PARA_TY_INT ? sizeof(int32_t) : sizeof(double);
        if (parameter->len && (uint64_t)parameter->len * element > v->maximum_storage)
            return failure(v->error, CS_OSDI_RESOURCE_LIMIT, v->index, "param_opvar.len", "Parameter array exceeds storage budget");
    }
    for (uint32_t i = 0; i < d->absdelay_count; ++i) {
        const OsdiAbsDelayInfo *delay = d->absdelay_info + i;
        OsdiNodePair input = {delay->input_node_1, delay->input_node_2};
        if (!node_pair(input, d->num_nodes, 1) || delay->output_node >= d->num_nodes)
            return malformed(v, "absdelay_info.nodes", "Delay node index is out of range");
        CHECK(span(v, delay->delay_offset, 1, sizeof(double), _Alignof(double), d->instance_size, 0, "absdelay_info.delay_offset"));
        CHECK(span(v, delay->max_delay_offset, 1, sizeof(double), _Alignof(double), d->instance_size, 1, "absdelay_info.max_delay_offset"));
    }
    *info = (cs_osdi_descriptor_info_record){d->name, info->name_length, d->num_nodes, d->num_terminals,
        d->num_jacobian_entries, d->num_states, d->num_collapsible, d->num_params, d->num_instance_params,
        d->num_opvars, d->num_noise_src, d->instance_size, d->model_size};
    return CS_OSDI_SUCCESS;
}

#if defined(__APPLE__) && defined(__aarch64__)
static uint32_t big32(const unsigned char *bytes) {
    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 | (uint32_t)bytes[2] << 8 | bytes[3];
}
static uint64_t big64(const unsigned char *bytes) { return (uint64_t)big32(bytes) << 32 | big32(bytes + 4); }

static int32_t inspect_header(int fd, uint64_t offset, uint64_t size, cs_osdi_error *error) {
    struct mach_header_64 header;
    if (size < sizeof(header) || offset > INT64_MAX || pread(fd, &header, sizeof(header), (off_t)offset) != sizeof(header))
        return failure(error, CS_OSDI_OPEN_FAILED, UINT32_MAX, "image", "Truncated Mach-O header");
    if (header.magic != MH_MAGIC_64 || header.cputype != CPU_TYPE_ARM64
        || (header.cpusubtype & ~CPU_SUBTYPE_MASK) != CPU_SUBTYPE_ARM64_ALL)
        return failure(error, CS_OSDI_WRONG_ARCHITECTURE, UINT32_MAX, "architecture", "A native arm64 Mach-O slice is required");
    if (header.filetype != MH_DYLIB && header.filetype != MH_BUNDLE)
        return failure(error, CS_OSDI_OPEN_FAILED, UINT32_MAX, "image", "Image is not a loadable dynamic library or bundle");
    return CS_OSDI_SUCCESS;
}

static int32_t inspect_image(const char *path, const cs_osdi_limits *limits, cs_osdi_error *error) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return failure(error, CS_OSDI_OPEN_FAILED, UINT32_MAX, "path", "%s", strerror(errno));
    struct stat st;
    unsigned char header[8];
    int32_t result;
    if (fstat(fd, &st) || st.st_size < 8 || pread(fd, header, sizeof(header), 0) != sizeof(header)) {
        result = failure(error, CS_OSDI_OPEN_FAILED, UINT32_MAX, "image", "Image header is unavailable");
    } else if (big32(header) == FAT_MAGIC || big32(header) == FAT_MAGIC_64) {
        uint32_t count = big32(header + 4);
        uint64_t entry_size = big32(header) == FAT_MAGIC ? sizeof(struct fat_arch) : sizeof(struct fat_arch_64);
        uint64_t bytes = (uint64_t)count * entry_size;
        if (bytes > limits->maximum_metadata_bytes || bytes > (uint64_t)st.st_size - 8) {
            result = failure(error, CS_OSDI_RESOURCE_LIMIT, UINT32_MAX, "fat_arch", "Universal architecture table exceeds image or metadata budget");
        } else {
            result = failure(error, CS_OSDI_WRONG_ARCHITECTURE, UINT32_MAX, "architecture", "Universal image has no native arm64 slice");
            for (uint32_t i = 0; i < count; ++i) {
                unsigned char entry[sizeof(struct fat_arch_64)];
                if (pread(fd, entry, (size_t)entry_size, (off_t)(8 + (uint64_t)i * entry_size)) != (ssize_t)entry_size) {
                    result = failure(error, CS_OSDI_OPEN_FAILED, UINT32_MAX, "fat_arch", "Truncated architecture entry"); break;
                }
                if (big32(entry) != CPU_TYPE_ARM64 || (big32(entry + 4) & ~CPU_SUBTYPE_MASK) != CPU_SUBTYPE_ARM64_ALL) continue;
                uint64_t offset = entry_size == sizeof(struct fat_arch) ? big32(entry + 8) : big64(entry + 8);
                uint64_t size = entry_size == sizeof(struct fat_arch) ? big32(entry + 12) : big64(entry + 16);
                if (offset > (uint64_t)st.st_size || size > (uint64_t)st.st_size - offset)
                    result = failure(error, CS_OSDI_OPEN_FAILED, UINT32_MAX, "fat_arch", "Architecture slice exceeds image");
                else result = inspect_header(fd, offset, size, error);
                break;
            }
        }
    } else result = inspect_header(fd, 0, (uint64_t)st.st_size, error);
    if (close(fd) && result == CS_OSDI_SUCCESS)
        result = failure(error, CS_OSDI_OPEN_FAILED, UINT32_MAX, "path", "Image inspection close failed: %s", strerror(errno));
    return result;
}
#endif

int32_t cs_osdi_open(const char *path, const cs_osdi_limits *limits,
                     cs_osdi_library **out_library, cs_osdi_error *error) {
    if (error) memset(error, 0, sizeof(*error));
    if (out_library) *out_library = NULL;
    if (!path || !limits || !out_library)
        return failure(error, CS_OSDI_INVALID_ARGUMENT, UINT32_MAX, "arguments", "Path, limits and owner output are required");
#if defined(__APPLE__) && defined(__aarch64__)
    int32_t status = inspect_image(path, limits, error);
    if (status) return status;
    cs_osdi_library *library = calloc(1, sizeof(*library));
    if (!library) return failure(error, CS_OSDI_ALLOCATION_FAILED, UINT32_MAX, "owner", "Owner allocation failed");
    library->handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!library->handle) {
        status = failure(error, CS_OSDI_OPEN_FAILED, UINT32_MAX, "dlopen", "%s", dlerror());
        free(library); return status;
    }
    const uint32_t *major = dlsym(library->handle, "OSDI_VERSION_MAJOR");
    const uint32_t *minor = dlsym(library->handle, "OSDI_VERSION_MINOR");
    const uint32_t *count = dlsym(library->handle, "OSDI_NUM_DESCRIPTORS");
    const uint32_t *stride = dlsym(library->handle, "OSDI_DESCRIPTOR_SIZE");
    library->descriptors = dlsym(library->handle, "OSDI_DESCRIPTORS");
    if (!major || !minor || !count || !stride || !library->descriptors) {
        status = failure(error, CS_OSDI_MISSING_SYMBOL, UINT32_MAX, "symbols", "Required OSDI export is missing"); goto failed;
    }
    if ((uintptr_t)major % _Alignof(uint32_t) || (uintptr_t)minor % _Alignof(uint32_t)
        || (uintptr_t)count % _Alignof(uint32_t) || (uintptr_t)stride % _Alignof(uint32_t)) {
        status = failure(error, CS_OSDI_INCOMPATIBLE_ABI, UINT32_MAX, "symbols", "OSDI scalar export is misaligned"); goto failed;
    }
    if (*major != 0 || *minor != 4 || *stride < sizeof(OsdiDescriptor) || *stride % _Alignof(OsdiDescriptor)
        || (uintptr_t)library->descriptors % _Alignof(OsdiDescriptor)) {
        status = failure(error, CS_OSDI_INCOMPATIBLE_ABI, UINT32_MAX, "version_or_stride", "Expected aligned OSDI 0.4 descriptors with sufficient exported stride"); goto failed;
    }
    library->count = *count;
    library->stride = *stride;
    if (*count > limits->maximum_descriptors) {
        status = failure(error, CS_OSDI_RESOURCE_LIMIT, UINT32_MAX, "descriptor_count", "Descriptor count exceeds budget"); goto failed;
    }
    cs_validation validation = {limits->maximum_metadata_bytes, limits->maximum_storage_bytes, UINT32_MAX, error};
    status = table(&validation, library->descriptors, *count, *stride, _Alignof(OsdiDescriptor), "descriptors");
    if (status) goto failed;
    status = consume(&validation, (uint64_t)*count * sizeof(cs_osdi_descriptor_info_record), "inspection_records");
    if (status) goto failed;
    if (*count) {
        library->information = calloc(*count, sizeof(*library->information));
        if (!library->information) { status = failure(error, CS_OSDI_ALLOCATION_FAILED, UINT32_MAX, "inspection_records", "Inspection allocation failed"); goto failed; }
    }
    for (uint32_t i = 0; i < *count; ++i) {
        validation.index = i;
        const OsdiDescriptor *descriptor = (const OsdiDescriptor *)(library->descriptors + (uint64_t)i * *stride);
        status = validate_descriptor(&validation, descriptor, library->information + i);
        if (status) goto failed;
    }
    library->validated = 1;
    *out_library = library;
    return CS_OSDI_SUCCESS;
failed:
    if (dlclose(library->handle)) {
        const char *secondary = dlerror();
        if (error) snprintf(error->cleanup_detail, sizeof(error->cleanup_detail), "%s", secondary ? secondary : "dlclose failed");
        *out_library = library;
        return CS_OSDI_CLOSE_FAILED;
    }
    free(library->information); free(library);
    return status;
#else
    return failure(error, CS_OSDI_UNSUPPORTED_HOST, UINT32_MAX, "host", "Only macOS arm64 is qualified");
#endif
}

int32_t cs_osdi_close(cs_osdi_library *library, cs_osdi_error *error) {
    if (error) memset(error, 0, sizeof(*error));
    if (!library) return failure(error, CS_OSDI_INVALID_ARGUMENT, UINT32_MAX, "owner", "Owner is null");
#if defined(__APPLE__) && defined(__aarch64__)
    if (dlclose(library->handle)) {
        const char *detail = dlerror();
        return failure(error, CS_OSDI_CLOSE_FAILED, UINT32_MAX, "dlclose", "%s", detail ? detail : "dlclose failed");
    }
    free(library->information); free(library);
    return CS_OSDI_SUCCESS;
#else
    return failure(error, CS_OSDI_UNSUPPORTED_HOST, UINT32_MAX, "host", "Only macOS arm64 is qualified");
#endif
}

uint32_t cs_osdi_descriptor_count(const cs_osdi_library *library) { return library && library->validated ? library->count : 0; }
uint64_t cs_osdi_descriptor_stride(const cs_osdi_library *library) { return library && library->validated ? library->stride : 0; }
int32_t cs_osdi_descriptor_info(const cs_osdi_library *library, uint32_t index,
                               cs_osdi_descriptor_info_record *out_info, cs_osdi_error *error) {
    if (error) memset(error, 0, sizeof(*error));
    if (!library || !out_info || !library->validated)
        return failure(error, CS_OSDI_INVALID_ARGUMENT, UINT32_MAX, "owner", "A validated owner and output are required");
    if (index >= library->count)
        return failure(error, CS_OSDI_DESCRIPTOR_INDEX_OUT_OF_RANGE, index, "descriptor_index", "Descriptor index is out of range");
    *out_info = library->information[index];
    return CS_OSDI_SUCCESS;
}
void *cs_osdi_allocate_zeroed(uint64_t bytes) { return bytes && bytes <= SIZE_MAX ? calloc(1, (size_t)bytes) : NULL; }
void cs_osdi_free(void *storage) { free(storage); }
