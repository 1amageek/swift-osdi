/* IPC transport/failure oracle only. This does not implement model arithmetic. */
#include "OSDIPrecisionProtocol.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void pause_ns(long nanoseconds) {
    struct timespec delay = {0, nanoseconds}; nanosleep(&delay, NULL);
}
static double *values(cs_osdi_precision_control_v1 *h, unsigned region) {
    return (double *)((unsigned char *)h + h->regions[region].byte_offset);
}
int main(void) {
    struct stat file;
    if (fstat(3, &file) || file.st_size < (off_t)sizeof(cs_osdi_precision_control_v1)) return 82;
    cs_osdi_precision_control_v1 *h = mmap(NULL, (size_t)file.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, 3, 0);
    if (h == MAP_FAILED || h->magic != CS_OSDI_PRECISION_MAGIC || h->mapping_bytes != (uint64_t)file.st_size
        || h->protocol_version != CS_OSDI_PRECISION_PROTOCOL_VERSION || h->header_bytes != sizeof(*h)) return 82;
    unsigned char mode = *((unsigned char *)h + h->regions[0].byte_offset);
    if (mode == 1) _exit(80);
    if (mode == 2) { for (;;) pause(); }
    h->response_status = 0;
    __atomic_store_n(&h->response_sequence, 0, __ATOMIC_RELEASE);
    unsigned char wake = 1;
    if (write(5, &wake, 1) != 1) return 82;
    if (mode == 3) { pause_ns(50000000); _exit(0); }
    for (;;) {
        if (read(4, &wake, 1) != 1) return 0;
        uint64_t sequence = __atomic_load_n(&h->request_sequence, __ATOMIC_ACQUIRE);
        if (mode == 6) _exit(80);
        if (mode == 7) { for (;;) pause(); }
        for (unsigned r = 6; r < 14; ++r) {
            uint64_t count = h->regions[r].element_count;
            double *output = values(h, r);
            for (uint64_t i = 0; i < count; ++i) output[i] = 0;
        }
        values(h, 6)[0] = (double)getpid();
        values(h, 9)[0] = values(h, 2)[0] - values(h, 4)[0];
        values(h, 12)[0] = 1; values(h, 13)[0] = 2;
        h->used_precision_bits = h->requested_precision_bits;
        h->completed_work_units = 17;
        h->model_flags = 0; h->has_maximum_timestep = 0;
        h->response_status = mode == 9 ? CS_OSDI_PRECISION_WORK_EXHAUSTED : 0;
        if (mode == 8) values(h, 8)[0] = NAN;
        if (mode == 10) h->regions[6].byte_offset = UINT64_MAX;
        __atomic_store_n(&h->response_sequence, sequence + (mode == 4), __ATOMIC_RELEASE);
        if (write(5, &wake, 1) != 1) return 82;
        if (mode == 9) return 0;
    }
}
