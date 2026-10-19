#include "../Arena/Arena.h"
#include "../ProgramEvaluation/Program.h"
#include "../ProgramEvaluation/Chart.h"
#include "DecisionCover/Transport.h"
#include "ConstitutiveIdentities/Transport.h"
#include <COSDIPrecision.h>
#include <OSDIPrecisionProtocol.h>
#include <errno.h>
#include <float.h>
#include <math.h>
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
static int native_bytes(uint64_t count,size_t stride,size_t *out){
  if(stride && count>SIZE_MAX/stride)return 0;*out=(size_t)count*stride;return 1;
}
static int geometry(const cs_osdi_precision_control_v1 *c, size_t size) {
  int chart = c->protocol_version == CS_OSDI_CHART_PROTOCOL_VERSION;
  int identity = c->protocol_version == CS_OSDI_IDENTITY_PROTOCOL_VERSION;
  int cover = c->protocol_version == CS_OSDI_COVER_PROTOCOL_VERSION || identity;
  size_t header_bytes = identity ? sizeof(cs_osdi_identity_control_v5) : cover ? sizeof(cs_osdi_cover_control_v4) : chart ? sizeof(cs_osdi_chart_control_v3) : sizeof(*c);
  uint16_t endian = 1;
  if (*(unsigned char *)&endian != 1 || DBL_MANT_DIG != 53 ||
      DBL_MAX_EXP != 1024 || !__atomic_always_lock_free(8, 0) ||
      c->magic != CS_OSDI_PRECISION_MAGIC ||
      (c->protocol_version != CS_OSDI_PRECISION_PROTOCOL_VERSION &&
       c->protocol_version != CS_OSDI_RANGE_PROTOCOL_VERSION && !chart && !cover) ||
      c->header_bytes != header_bytes || size < header_bytes || c->mapping_bytes != size ||
      c->byte_order != CS_OSDI_PRECISION_BYTE_ORDER || !c->raw_node_count ||
      c->raw_node_count > UINT32_MAX - 1 ||
      c->active_count > c->raw_node_count ||
      c->raw_node_count > SIZE_MAX / c->raw_node_count / 8 ||
      c->minimum_precision_bits < 2 ||
      c->maximum_precision_bits < c->minimum_precision_bits ||
      !c->maximum_work_units)
    return 0;
  cs_osdi_precision_region_v1 regions[20];
  memcpy(regions, c->regions, sizeof(c->regions));
  unsigned total = 14;
  if (chart) {
    const cs_osdi_chart_control_v3 *h = (const void *)c;
    if (!h->limits.maximum_steps || !h->limits.maximum_tape_entries ||
        !h->limits.maximum_guard_records || h->reserved ||
        h->limits.maximum_tape_entries > SIZE_MAX / sizeof(cs_precision_chart_decision) ||
        h->limits.maximum_guard_records > SIZE_MAX / sizeof(cs_precision_chart_guard))
      return 0;
    regions[14] = h->tape_region; regions[15] = h->guard_region; total = 16;
    if (regions[14].element_count != h->limits.maximum_tape_entries ||
        regions[15].element_count != h->limits.maximum_guard_records) return 0;
  }
  if (cover) {
    const cs_osdi_cover_control_v4 *h=(const void *)c;const cs_osdi_cover_limits_v4 *l=&h->limits;
    uint64_t n=c->raw_node_count,matrix=n*n,vector=4*n;
    if(!l->maximum_steps || !l->maximum_guard_records || !l->maximum_copied_bytes || !l->maximum_cells ||
        !l->maximum_nodes || l->maximum_nodes==UINT32_MAX || !l->maximum_leaves || !l->maximum_pending ||
        !l->maximum_depth || !l->maximum_integer_bits || l->maximum_integer_bits>CS_OSDI_COVER_CELL_BITS || l->reserved ||
        matrix>(UINT64_MAX-vector)/4)return 0;
    uint64_t stride=vector+4*matrix;size_t native_size;
    if(l->maximum_leaves>UINT64_MAX/stride ||
        h->tree_region.element_count!=l->maximum_nodes || h->leaf_region.element_count!=stride*l->maximum_leaves ||
        !native_bytes(l->maximum_nodes,sizeof(cs_precision_cover_node),&native_size) ||
        !native_bytes(l->maximum_depth,sizeof(cs_precision_source_event),&native_size) ||
        !native_bytes(l->maximum_depth,sizeof(cs_precision_source_decision),&native_size) ||
        !native_bytes(l->maximum_pending,sizeof(uint32_t),&native_size) ||
        !native_bytes(l->maximum_leaves,sizeof(uint32_t),&native_size) || c->active_count>SIZE_MAX/4/sizeof(double))return 0;
    regions[14]=h->tree_region;regions[15]=h->leaf_region;total=16;
  }
  if(identity){
    const cs_osdi_identity_control_v5 *h=(const void *)c;
    const cs_osdi_identity_limits_v5 *l=&h->limits;
    if(c->raw_node_count>UINT32_MAX/2 || !l->maximum_nodes || l->maximum_nodes==UINT32_MAX ||
        !l->maximum_atoms || l->maximum_atoms==UINT32_MAX || !l->maximum_coefficient_bits || !l->maximum_workspace_bytes)return 0;
    uint64_t claims=2*c->raw_node_count,coefficients=claims*c->raw_node_count;
    uint64_t words=(l->maximum_coefficient_bits-1)/64+1;
    if(words>UINT64_MAX/coefficients || h->claim_region.element_count!=claims ||
        h->coefficient_region.element_count!=coefficients || h->word_region.element_count!=words*coefficients)return 0;
    regions[16]=h->claim_region;regions[17]=h->coefficient_region;regions[18]=h->word_region;regions[19]=h->quotient_region;total=20;
    if(h->quotient_region.element_count!=c->active_count)return 0;
  }
  for (unsigned i = 0; i < total; i++) {
    const cs_osdi_precision_region_v1 *r = &regions[i];
    uint64_t stride = i==19 ? sizeof(uint32_t) : i==16 ? sizeof(cs_osdi_identity_claim_v5) : i==17 ? sizeof(cs_osdi_identity_coefficient_v5) : i < 2 ? 1 : i == 14 ? (cover ? sizeof(cs_osdi_cover_node_v4) : sizeof(cs_osdi_chart_decision_v3)) :
                      i == 15 && chart ? sizeof(cs_osdi_chart_guard_v3) : 8;
    unsigned matrix_start = c->protocol_version == CS_OSDI_PRECISION_PROTOCOL_VERSION ? 12u : 10u;
    uint64_t count = i < 2 || i >= 14 ? r->element_count :
                     i < 6 ? c->active_count : i < matrix_start ? c->raw_node_count :
                     c->raw_node_count * c->raw_node_count;
    if (r->element_count != count || r->byte_offset < header_bytes ||
        r->byte_offset % 8 || r->byte_offset > size ||
        r->element_count > (size - r->byte_offset) / stride)
      return 0;
    for (unsigned j = 0; j < i; j++) {
      uint64_t other_stride = j==19 ? sizeof(uint32_t) : j==16 ? sizeof(cs_osdi_identity_claim_v5) : j==17 ? sizeof(cs_osdi_identity_coefficient_v5) : j < 2 ? 1 : j == 14 ? (cover ? sizeof(cs_osdi_cover_node_v4) : sizeof(cs_osdi_chart_decision_v3)) : 8;
      uint64_t as = r->byte_offset, an = r->element_count * stride;
      uint64_t bs = regions[j].byte_offset, bn = regions[j].element_count * other_stride;
      if (an && bn && as < bs + bn && bs < as + an) return 0;
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
  cs_osdi_precision_control_v1 *c = mapping;
  union {cs_osdi_precision_control_v1 base;cs_osdi_chart_control_v3 chart;cs_osdi_cover_control_v4 cover;cs_osdi_identity_control_v5 identity;} snapshot={0};
  if (!geometry(c, size)) { munmap(mapping, size); return CS_OSDI_PRECISION_EXIT_PROTOCOL; }
  memcpy(&snapshot, c, c->header_bytes);
  cs_osdi_chart_control_v3 admitted_chart=snapshot.chart;
  cs_osdi_cover_control_v4 admitted_cover=snapshot.cover;
  cs_osdi_identity_control_v5 admitted_identity=snapshot.identity;
  cs_osdi_precision_control_v1 admitted = snapshot.base;
  int is_chart = admitted.protocol_version == CS_OSDI_CHART_PROTOCOL_VERSION;
  int is_identity=admitted.protocol_version==CS_OSDI_IDENTITY_PROTOCOL_VERSION;
  int is_cover = admitted.protocol_version == CS_OSDI_COVER_PROTOCOL_VERSION || is_identity;
  if (!geometry(&snapshot.base, size) ||
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
  /* Only after full v3 geometry admission, adapt a private interpreter view.
   * Public protocol 3 never enters the smooth-box process operation. */
  cs_osdi_precision_control_v1 interpreter_control = admitted;
  if (is_chart || is_cover) interpreter_control.protocol_version = CS_OSDI_RANGE_PROTOCOL_VERSION;
  cs_precision_program p;
  int status = cs_precision_program_open(
      &p, base + admitted.regions[0].byte_offset,
      (size_t)admitted.regions[0].element_count,
      base + admitted.regions[1].byte_offset,
      (size_t)admitted.regions[1].element_count, &interpreter_control);
  cs_precision_chart_decision *tape = NULL;
  cs_precision_chart_guard *guards = NULL;
  if (!status && is_chart) {
    tape = cs_precision_arena_allocate((size_t)admitted_chart.limits.maximum_tape_entries * sizeof(*tape));
    guards = cs_precision_arena_allocate((size_t)admitted_chart.limits.maximum_guard_records * sizeof(*guards));
  }
  cs_precision_cover_transport cover_transport={0};
  if(!status && is_cover){double *planes[8];for(unsigned i=0;i<8;++i)planes[i]=(void *)(base+admitted.regions[i+6].byte_offset);
    status=cs_precision_cover_transport_open(&cover_transport,&admitted_cover,planes);}
  cs_precision_constitutive_workspace identity_workspace={0};
  if(!status && is_identity)status=cs_precision_identity_transport_open(&p,&admitted_identity,(const void *)(base+admitted_identity.quotient_region.byte_offset),&identity_workspace);
  uint64_t completed_steps = 0;
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
        c->maximum_work_units != admitted.maximum_work_units ||
        (is_chart && memcmp((unsigned char *)c + sizeof(*c),
          (unsigned char *)&admitted_chart + sizeof(*c),
          offsetof(cs_osdi_chart_control_v3, chart_mode) - sizeof(*c))) ||
        (is_cover && memcmp((unsigned char *)c+sizeof(*c),(unsigned char *)&admitted_cover+sizeof(*c),
          offsetof(cs_osdi_cover_control_v4,node_count)-sizeof(*c))) ||
        (is_identity && memcmp((unsigned char *)c+sizeof(admitted_cover),
          (unsigned char *)&admitted_identity+sizeof(admitted_cover),
          offsetof(cs_osdi_identity_control_v5,claim_count)-sizeof(admitted_cover)))) {
      status = CS_OSDI_PRECISION_EXIT_PROTOCOL;
      break;
    }
    double *output[8];
    for (unsigned i = 0; i < 8; i++)
      output[i] = (void *)(base + admitted.regions[i + 6].byte_offset);
    if(is_identity){
      status=cs_precision_identity_transport_evaluate(&cover_transport,&identity_workspace,&p,(void *)c,&admitted_identity,base);
    }else if(is_cover){
      status=cs_precision_cover_transport_evaluate(&cover_transport,&p,(void *)c,&admitted_cover,base);
    } else if (is_chart) {
      cs_osdi_chart_control_v3 *chart = (void *)c;
      chart->guard_count = 0; chart->completed_steps = completed_steps;
      if (chart->reserved || chart->chart_mode < CS_OSDI_CHART_RECORD ||
          chart->chart_mode > CS_OSDI_CHART_FORMULA_CLOSURE ||
          chart->tape_count > admitted_chart.limits.maximum_tape_entries ||
          (chart->chart_mode == CS_OSDI_CHART_RECORD && chart->tape_count)) {
        status = CS_OSDI_PRECISION_INVALID_INPUT;
      } else if (completed_steps >= admitted_chart.limits.maximum_steps) {
        status = CS_OSDI_CHART_STEP_LIMIT;
      } else {
        const cs_osdi_chart_decision_v3 *wire = (void *)(base + admitted_chart.tape_region.byte_offset);
        if (!cs_precision_charge(&p.math, chart->tape_count)) status = p.math.status;
        for (uint64_t i = 0; !status && i < chart->tape_count; ++i) {
          if (wire[i].reserved || wire[i].choice > 1 || wire[i].ordinal != i ||
              (wire[i].stage < 1 || wire[i].stage > 3) || wire[i].kind < 1 || wire[i].kind > 3) {
            status = CS_OSDI_CHART_TAPE_MISMATCH; break;
          }
          tape[i] = (cs_precision_chart_decision){wire[i].stage, wire[i].block,
            wire[i].instruction, wire[i].kind, wire[i].choice, wire[i].ordinal};
        }
        if (!status) {
          cs_osdi_precision_control_v1 local = *c;
          local.protocol_version = CS_OSDI_RANGE_PROTOCOL_VERSION;
          cs_precision_chart_request request = {chart->chart_mode, tape, (size_t)chart->tape_count,
            admitted_chart.limits.maximum_steps - completed_steps};
          cs_precision_chart_scratch scratch = {0};
          scratch.guards = guards; scratch.guard_capacity = (size_t)admitted_chart.limits.maximum_guard_records;
          for (unsigned i = 0; i < 8; ++i) {
            scratch.planes[i] = output[i];
            scratch.plane_capacities[i] = (size_t)admitted.regions[i + 6].element_count;
          }
          cs_precision_chart_result result = {0};
          status = cs_precision_program_evaluate_chart(&p, &local,
            (void *)(base + admitted.regions[2].byte_offset),
            (void *)(base + admitted.regions[3].byte_offset),
            (void *)(base + admitted.regions[4].byte_offset),
            (void *)(base + admitted.regions[5].byte_offset), &request, &scratch, &result);
          if (!status) {
            /* Input/output projection work shares the existing cumulative owner. */
            if (!cs_precision_charge(&p.math, result.guard_count)) status = p.math.status;
            cs_osdi_chart_guard_v3 *out = (void *)(base + admitted_chart.guard_region.byte_offset);
            for (size_t i = 0; !status && i < result.guard_count; ++i) {
              const cs_precision_chart_guard *g = &result.guards[i];
              if ((g->a_type == 1 && (!isfinite(g->a_lower) || !isfinite(g->a_upper))) ||
                  (g->b_type == 1 && (!isfinite(g->b_lower) || !isfinite(g->b_upper)))) {
                p.detail = g->decision.instruction;
                status = CS_OSDI_PRECISION_NONFINITE_PROJECTION; break;
              }
              out[i] = (cs_osdi_chart_guard_v3){
                {g->decision.stage, g->decision.block, g->decision.instruction,
                 g->decision.kind, g->decision.choice, 0, g->decision.ordinal},
                g->opcode, g->a, g->b, g->a_type, g->b_type, g->a_integer, g->b_integer,
                g->formula_truth_known, g->boundary_extension, 0,
                g->a_lower, g->a_upper, g->b_lower, g->b_upper};
            }
            if (!status) {
              completed_steps += result.completed_steps;
              chart->completed_steps = completed_steps; chart->guard_count = result.guard_count;
            }
          }
        }
      }
    } else {
      status = cs_precision_program_evaluate(
          &p, c, (void *)(base + admitted.regions[2].byte_offset),
          (void *)(base + admitted.regions[3].byte_offset),
          (void *)(base + admitted.regions[4].byte_offset),
          (void *)(base + admitted.regions[5].byte_offset), output);
    }
    publish(c, sequence, status, &p);
    sent = notification(response_write_fd, 1);
    previous = sequence;
  }
  cs_precision_constitutive_close(&identity_workspace);
  mpfr_free_cache();
  cs_precision_arena_close(&arena);
  munmap(mapping, size);
  return status == CS_OSDI_PRECISION_EXIT_PROTOCOL ? status : 0;
}
