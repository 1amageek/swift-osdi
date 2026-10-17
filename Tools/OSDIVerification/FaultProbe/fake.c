#include "COSDI.h"
#include "control.h"
#include <stdlib.h>
#include <string.h>
static unsigned char identity;
void *fake_handle(void) { return &identity; }
static int attempts;
static int allowed;
void fake_reset(void) { attempts = 0; allowed = 0; }
int fake_close_count(void) { return attempts; }
void fake_allow_close(void) { allowed = 1; }
int32_t cs_osdi_close(cs_osdi_library *library, cs_osdi_error *error) {
    (void)library;
    attempts++;
    if (!allowed) {
        error->code = CS_OSDI_CLOSE_FAILED;
        strcpy(error->field, "dlclose");
        strcpy(error->detail, "persistent injected unload failure");
        return CS_OSDI_CLOSE_FAILED;
    }
    return CS_OSDI_SUCCESS;
}
uint32_t cs_osdi_descriptor_count(const cs_osdi_library *library) { (void)library; return 1; }
uint64_t cs_osdi_descriptor_stride(const cs_osdi_library *library) { (void)library; return 128; }
int32_t cs_osdi_descriptor_info(const cs_osdi_library *library, uint32_t index, cs_osdi_descriptor_info_record *info, cs_osdi_error *error) {
    (void)library; (void)index; (void)error;
    memset(info, 0, sizeof(*info));
    info->name = "fake";
    info->name_length = 4;
    info->nodes = 1;
    info->terminals = 1;
    info->model_bytes = 1;
    info->instance_bytes = 1;
    return 0;
}
void *cs_osdi_allocate_zeroed(uint64_t bytes) { return calloc(1, bytes); }
void cs_osdi_free(void *pointer) { free(pointer); }
