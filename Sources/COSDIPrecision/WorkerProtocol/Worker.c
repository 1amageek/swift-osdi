#include "../Arena/Arena.h"
#include "../ProgramEvaluation/Program.h"
#include <COSDIPrecision.h>
#include <OSDIPrecisionProtocol.h>
#include <errno.h>
#include <float.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
static int notification(int fd, int writing) {
  unsigned char value = 1;
  ssize_t n;
  do {
    n = writing ? write(fd, &value, 1) : read(fd, &value, 1);
  } while (n < 0 && errno == EINTR);
  return n == 1 ? 1 : n == 0 ? 0 : -1;
}
static int geometry(const cs_osdi_precision_control_v1 *c, size_t size) {
  uint16_t endian = 1;
  if (*(unsigned char *)&endian != 1 || DBL_MANT_DIG != 53 ||
      DBL_MAX_EXP != 1024 || !__atomic_always_lock_free(8, 0) ||
      c->magic != CS_OSDI_PRECISION_MAGIC ||
      (c->protocol_version != CS_OSDI_PRECISION_PROTOCOL_VERSION &&
       c->protocol_version != CS_OSDI_RANGE_PROTOCOL_VERSION) ||
      c->header_bytes != sizeof(*c) || c->mapping_bytes != size ||
      c->byte_order != CS_OSDI_PRECISION_BYTE_ORDER || !c->raw_node_count ||
      c->raw_node_count > UINT32_MAX - 1 ||
      c->active_count > c->raw_node_count ||
      c->raw_node_count > SIZE_MAX / c->raw_node_count / 8 ||
      c->minimum_precision_bits < 2 ||
      c->maximum_precision_bits < c->minimum_precision_bits ||
      !c->maximum_work_units)
    return 0;
  for (unsigned i = 0; i < 14; i++) {
    const cs_osdi_precision_region_v1 *r = &c->regions[i];
    uint64_t stride = i < 2 ? 1 : 8;
    unsigned matrix_start =
        c->protocol_version == CS_OSDI_RANGE_PROTOCOL_VERSION ? 10u : 12u;
    uint64_t count = i < 2              ? r->element_count
                     : i < 6            ? c->active_count
                     : i < matrix_start ? c->raw_node_count
                                        : c->raw_node_count * c->raw_node_count;
    if (r->element_count != count || r->byte_offset < sizeof(*c) ||
        r->byte_offset % 8 || r->byte_offset > size ||
        r->element_count > (size - r->byte_offset) / stride)
      return 0;
    for (unsigned j = 0; j < i; j++) {
      uint64_t as = r->byte_offset, an = r->element_count * stride;
      uint64_t bs = c->regions[j].byte_offset,
               bn = c->regions[j].element_count * (j < 2 ? 1 : 8);
      if (an && bn && as < bs + bn && bs < as + an)
        return 0;
    }
  }
  return 1;
}
static void publish(cs_osdi_precision_control_v1 *c, uint64_t sequence,
                    int status, const cs_precision_program *p) {
  c->response_status = (uint32_t)status;
  c->completed_work_units = p->math.work;
  c->used_precision_bits = p->precision;
  c->model_flags = p->flags;
  c->response_detail = p->detail;
  c->has_maximum_timestep = status ? 0 : (uint32_t)p->has_bound;
  c->maximum_timestep = !status && p->has_bound ? p->bound : 0;
  __atomic_store_n(&c->response_sequence, sequence, __ATOMIC_RELEASE);
}
int cs_osdi_precision_worker_run(int slab_fd, int request_read_fd,
                                 int response_write_fd) {
  struct stat st;
  if (fstat(slab_fd, &st) ||
      st.st_size < (off_t)sizeof(cs_osdi_precision_control_v1) ||
      (uint64_t)st.st_size > SIZE_MAX)
    return CS_OSDI_PRECISION_EXIT_PROTOCOL;
  size_t size = (size_t)st.st_size;
  void *mapping =
      mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, slab_fd, 0);
  if (mapping == MAP_FAILED)
    return CS_OSDI_PRECISION_EXIT_PROTOCOL;
  cs_osdi_precision_control_v1 *c = mapping, admitted;
  memcpy(&admitted, c, sizeof(admitted));
  if (!geometry(&admitted, size) ||
      __atomic_load_n(&c->request_sequence, __ATOMIC_ACQUIRE) != 0 ||
      __atomic_load_n(&c->response_sequence, __ATOMIC_ACQUIRE) !=
          CS_OSDI_PRECISION_INITIAL_RESPONSE) {
    munmap(mapping, size);
    return CS_OSDI_PRECISION_EXIT_PROTOCOL;
  }
  unsigned char *base = mapping;
  cs_precision_arena arena;
  if (!cs_precision_arena_open(&arena, admitted.arena_bytes)) {
    munmap(mapping, size);
    return CS_OSDI_PRECISION_EXIT_ARENA;
  }
  cs_precision_program p;
  int status = cs_precision_program_open(
      &p, base + admitted.regions[0].byte_offset,
      (size_t)admitted.regions[0].element_count,
      base + admitted.regions[1].byte_offset,
      (size_t)admitted.regions[1].element_count, &admitted);
  publish(c, 0, status, &p);
  int sent = notification(response_write_fd, 1);
  uint64_t previous = 0;
  while (!status && sent == 1) {
    int wake = notification(request_read_fd, 0);
    if (!wake)
      break;
    if (wake < 0) {
      status = CS_OSDI_PRECISION_INVALID_INPUT;
      break;
    }
    uint64_t sequence = __atomic_load_n(&c->request_sequence, __ATOMIC_ACQUIRE);
    if (sequence != previous + 1 || sequence > CS_OSDI_PRECISION_MAX_SEQUENCE ||
        memcmp(c, &admitted,
               offsetof(cs_osdi_precision_control_v1, request_sequence)) ||
        c->maximum_work_units != admitted.maximum_work_units) {
      status = CS_OSDI_PRECISION_EXIT_PROTOCOL;
      break;
    }
    double *output[8];
    for (unsigned i = 0; i < 8; i++)
      output[i] = (void *)(base + admitted.regions[i + 6].byte_offset);
    status = cs_precision_program_evaluate(
        &p, c, (void *)(base + admitted.regions[2].byte_offset),
        (void *)(base + admitted.regions[3].byte_offset),
        (void *)(base + admitted.regions[4].byte_offset),
        (void *)(base + admitted.regions[5].byte_offset), output);
    publish(c, sequence, status, &p);
    sent = notification(response_write_fd, 1);
    previous = sequence;
  }
  mpfr_free_cache();
  cs_precision_arena_close(&arena);
  munmap(mapping, size);
  return status == CS_OSDI_PRECISION_EXIT_PROTOCOL ? status : 0;
}
