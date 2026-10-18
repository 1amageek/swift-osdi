#include "../../Sources/COSDIPrecision/ProgramEvaluation/Chart.h"
#include "../../Sources/COSDIPrecision/Arena/Arena.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NDEBUG
#error "Chart verification requires enabled assertions"
#endif

typedef struct {
  void *program, *setup;
  cs_precision_program p;
  cs_osdi_precision_control_v1 control;
  cs_precision_chart_scratch scratch;
  cs_precision_chart_result result;
} model;
static size_t product(size_t a, size_t b) {
  assert(!b || a <= SIZE_MAX / b);
  return a*b;
}
static void *allocate(size_t count, size_t stride) {
  void *p = calloc(1, product(count, stride));
  assert(p); return p;
}
static void *read_file(const char *path, size_t *length) {
  FILE *f = fopen(path, "rb");
  assert(f && !fseek(f, 0, SEEK_END));
  long n = ftell(f);
  assert(n > 0 && (uint64_t)n <= UINT64_C(67108864) && !fseek(f, 0, SEEK_SET));
  *length = (size_t)n;
  void *bytes = allocate(*length, 1);
  assert(fread(bytes, 1, *length, f) == *length && !fclose(f));
  return bytes;
}
static void open_model(model *m, const char *program, const char *setup,
                       uint64_t work, size_t guards, uint32_t precision) {
  memset(m, 0, sizeof(*m));
  size_t pn, sn;
  m->program = read_file(program, &pn); m->setup = read_file(setup, &sn);
  assert(pn >= sizeof(CSOsdiPrecisionProgramHeader) && sn >= sizeof(CSOsdiPrecisionSetupHeader));
  const CSOsdiPrecisionProgramHeader *h = m->program;
  const CSOsdiPrecisionSetupHeader *s = m->setup;
  m->control.protocol_version = CS_OSDI_RANGE_PROTOCOL_VERSION;
  m->control.descriptor_index = h->descriptor_index;
  m->control.raw_node_count = h->raw_node_count; m->control.active_count = s->active_unknown_count;
  assert(precision >= 2 && precision <= 65536);
  m->control.minimum_precision_bits = m->control.maximum_precision_bits = m->control.requested_precision_bits = precision;
  m->control.maximum_work_units = work;
  m->control.candidate.mode = m->control.reference.mode = CS_OSDI_PRECISION_TRANSIENT;
  assert(!cs_precision_program_open(&m->p, m->program, pn, m->setup, sn, &m->control));
  m->scratch.guard_capacity = guards;
  m->scratch.guards = allocate(guards, sizeof(*m->scratch.guards));
  for (unsigned j = 0; j < 8; j++) {
    size_t n = j < 4 ? h->raw_node_count : product(h->raw_node_count, h->raw_node_count);
    m->scratch.planes[j] = allocate(n, sizeof(double)); m->scratch.plane_capacities[j] = n;
  }
}
static void fixture(model *m, const char *directory, const char *name) {
  char program[4096], setup[4096];
  int a = snprintf(program, sizeof(program), "%s/%s.program", directory, name);
  int b = snprintf(setup, sizeof(setup), "%s/%s.setup", directory, name);
  assert(a > 0 && (size_t)a < sizeof(program) && b > 0 && (size_t)b < sizeof(setup));
  open_model(m, program, setup, UINT64_C(10000000), 1024, 192);
}
static void close_model(model *m) {
  for (unsigned j = 0; j < 8; j++) free(m->scratch.planes[j]);
  free(m->scratch.guards); free(m->program); free(m->setup);
}
static cs_precision_chart_decision *tape(const model *m) {
  cs_precision_chart_decision *copy = allocate(m->result.guard_count + 1, sizeof(*copy));
  for (size_t j = 0; j < m->result.guard_count; j++) copy[j] = m->result.guards[j].decision;
  return copy;
}
static int box(model *m, uint32_t mode, const cs_precision_chart_decision *decisions,
               size_t count, double lower, double upper, uint64_t steps) {
  double zero = 0;
  cs_precision_chart_request request = {mode, decisions, count, steps};
  cs_precision_chart_result before = m->result;
  int status = cs_precision_program_evaluate_chart(&m->p, &m->control,
      &lower, &zero, &upper, &zero, &request, &m->scratch, &m->result);
  assert(!m->p.chart);
  if (status) assert(!memcmp(&before, &m->result, sizeof(before)));
  return status;
}
static void enclosed(const model *m, unsigned kind, double value) {
  assert(m->result.planes[2*kind][0] <= value && m->result.planes[2*kind+1][0] >= value);
}
static void nonlinear(const char *directory) {
  model m; fixture(&m, directory, "nonlinear");
  assert(!box(&m, CS_PRECISION_CHART_RECORD, NULL, 0, .25, .25, 1000));
  size_t low_count = m.result.guard_count;
  cs_precision_chart_decision *low = tape(&m);
  assert(low_count == 3 && low[0].choice == 0);
  assert(!box(&m, CS_PRECISION_CHART_RECORD, NULL, 0, .75, .75, 1000));
  size_t high_count = m.result.guard_count;
  cs_precision_chart_decision *high = tape(&m);
  assert(high_count == low_count && high[0].choice == 1);
  assert(high[0].instruction == low[0].instruction && high[0].kind == CS_PRECISION_CHART_PREDICATE);
  for (unsigned side = 0; side < 2; side++) {
    const cs_precision_chart_decision *selected = side ? high : low;
    assert(!box(&m, CS_PRECISION_CHART_EXACT_GRAPH, selected, low_count, 0, 1, 1000));
    assert(!m.result.guards[0].formula_truth_known);
    assert(m.result.guards[0].opcode == CS_OSDI_PRECISION_OP_FGT);
    assert(m.result.guards[0].a_lower <= 0 && m.result.guards[0].a_upper >= 1);
    for (unsigned j = 0; j <= 8; j++) {
      double x = j/8.0, z = x-.5, base = x+x*x*x;
      if ((x > .5) != (int)side) continue;
      double extra = side ? z*z*z : 0;
      enclosed(&m, 0, 4*base+extra);
      enclosed(&m, 1, base);
      enclosed(&m, 2, 4*(1+3*x*x)+(side ? 3*z*z : 0));
      enclosed(&m, 3, 1+3*x*x);
    }
  }
  low[0].ordinal++;
  assert(box(&m, CS_PRECISION_CHART_EXACT_GRAPH, low, low_count, 0, 1, 1000) == CS_PRECISION_CHART_TAPE_MISMATCH);
  low[0].ordinal--;
  assert(box(&m, CS_PRECISION_CHART_EXACT_GRAPH, low, low_count-1, 0, 1, 1000) == CS_PRECISION_CHART_TAPE_MISMATCH);
  assert(box(&m, CS_PRECISION_CHART_EXACT_GRAPH, low, low_count+1, 0, 1, 1000) == CS_PRECISION_CHART_TAPE_MISMATCH);
  assert(box(&m, CS_PRECISION_CHART_RECORD, NULL, 0, 0, 1, 1000) == CS_OSDI_PRECISION_UNRESOLVED_BRANCH);
  size_t capacity = m.scratch.guard_capacity;
  m.scratch.guard_capacity = 0;
  assert(box(&m, CS_PRECISION_CHART_RECORD, NULL, 0, .25, .25, 1000) == CS_PRECISION_CHART_CAPACITY);
  m.scratch.guard_capacity = capacity;
  assert(box(&m, CS_PRECISION_CHART_RECORD, NULL, 0, .25, .25, 1) == CS_PRECISION_CHART_STEP_LIMIT);
  uint64_t prior_work = m.p.math.work;
  m.p.math.limit = prior_work + 1;
  assert(box(&m, CS_PRECISION_CHART_RECORD, NULL, 0, .25, .25, 1000) == CS_OSDI_PRECISION_WORK_EXHAUSTED);
  assert(m.p.math.work >= prior_work && m.p.math.work <= m.p.math.limit);
  free(low); free(high); close_model(&m);
}
static void equality(const char *directory) {
  model m; fixture(&m, directory, "jump");
  assert(!box(&m, CS_PRECISION_CHART_RECORD, NULL, 0, .75, .75, 1000));
  size_t count = m.result.guard_count;
  cs_precision_chart_decision *right = tape(&m);
  assert(!box(&m, CS_PRECISION_CHART_RECORD, NULL, 0, .5, .5, 1000));
  assert(m.result.semantics == CS_PRECISION_CHART_RECORD);
  enclosed(&m, 0, 1); enclosed(&m, 1, .625);
  assert(box(&m, CS_PRECISION_CHART_EXACT_GRAPH, right, count, .5, .5, 1000) == CS_PRECISION_CHART_TAPE_MISMATCH);
  assert(!box(&m, CS_PRECISION_CHART_FORMULA_CLOSURE, right, count, .5, .5, 1000));
  assert(m.result.semantics == CS_PRECISION_CHART_FORMULA_CLOSURE && m.result.guards[0].boundary_extension);
  enclosed(&m, 0, 2); enclosed(&m, 1, .75); enclosed(&m, 2, 0); enclosed(&m, 3, 1.75);
  assert(box(&m, CS_PRECISION_CHART_FORMULA_CLOSURE, right, count, .25, .25, 1000) == CS_PRECISION_CHART_TAPE_MISMATCH);
  free(right); close_model(&m);
}
static void refusals_and_loops(const char *directory) {
  for (unsigned j = 0; j < 2; j++) {
    model m; fixture(&m, directory, j ? "removable" : "sqrt");
    assert(box(&m, CS_PRECISION_CHART_FORMULA_CLOSURE, NULL, 0, 0, 1, 1000) == CS_OSDI_PRECISION_NONSMOOTH);
    close_model(&m);
  }
  model m; fixture(&m, directory, "loop");
  assert(!box(&m, CS_PRECISION_CHART_RECORD, NULL, 0, 0, 0, 1000));
  assert(m.result.guard_count == 4);
  assert(m.result.guards[0].decision.choice == 1 && m.result.guards[2].decision.choice == 0);
  enclosed(&m, 0, 21);
  cs_precision_chart_decision *copy = tape(&m);
  assert(!box(&m, CS_PRECISION_CHART_EXACT_GRAPH, copy, 4, 0, 0, 1000));
  enclosed(&m, 0, 21);
  assert(box(&m, CS_PRECISION_CHART_EXACT_GRAPH, copy, 4, 0, 0, 2) == CS_PRECISION_CHART_STEP_LIMIT);
  free(copy); close_model(&m);
  fixture(&m, directory, "inactive");
  assert(!box(&m, CS_PRECISION_CHART_RECORD, NULL, 0, 0, 0, 1000)); enclosed(&m, 0, 0);
  close_model(&m);
  fixture(&m, directory, "live_invalid");
  assert(box(&m, CS_PRECISION_CHART_RECORD, NULL, 0, 0, 0, 1000) == CS_OSDI_PRECISION_DOMAIN);
  close_model(&m);
}
static uint64_t number(const char *text) {
  char *end; uint64_t value = strtoull(text, &end, 10);
  assert(value && end != text && !*end); return value;
}
static void actual(char **argv) {
  size_t size; void *data = read_file(argv[4], &size);
  assert(size >= sizeof(cs_osdi_precision_control_v1));
  cs_osdi_precision_control_v1 original;
  memcpy(&original, data, sizeof(original));
  model m; open_model(&m, argv[2], argv[3], number(argv[6]), number(argv[8]), original.maximum_precision_bits);
  size_t n = (size_t)m.p.active_count;
  assert(size == sizeof(original) + product(product(n, 4), sizeof(double)));
  m.control.candidate = m.control.reference = original.candidate;
  m.control.minimum_precision_bits = original.minimum_precision_bits;
  m.control.requested_precision_bits = original.requested_precision_bits;
  assert(m.control.requested_precision_bits <= m.p.maximum_precision);
  const double *v = (const void *)((unsigned char *)data + sizeof(original));
  cs_precision_chart_request request = {CS_PRECISION_CHART_RECORD, NULL, 0, number(argv[7])};
  assert(!cs_precision_program_evaluate_chart(&m.p, &m.control, v, v+n, v, v+n, &request, &m.scratch, &m.result));
  assert(m.result.guard_count > 0 && !m.p.chart);
  size_t count = m.result.guard_count;
  cs_precision_chart_decision *copy = tape(&m);
  double *expected[8];
  for (unsigned j = 0; j < 8; j++) {
    expected[j] = allocate(m.scratch.plane_capacities[j], sizeof(double));
    memcpy(expected[j], m.scratch.planes[j], product(m.scratch.plane_capacities[j], sizeof(double)));
  }
  request.mode = CS_PRECISION_CHART_EXACT_GRAPH; request.tape = copy; request.tape_count = count;
  assert(!cs_precision_program_evaluate_chart(&m.p, &m.control, v, v+n, v, v+n, &request, &m.scratch, &m.result));
  for (unsigned j = 0; j < 8; j++) {
    assert(!memcmp(expected[j], m.scratch.planes[j], product(m.scratch.plane_capacities[j], sizeof(double))));
    free(expected[j]);
  }
  printf("actual source chart: %zu guards, %" PRIu64 " cumulative work\n", count, m.p.math.work);
  free(copy); free(data); close_model(&m);
}
int main(int argc, char **argv) {
  assert(argc == 2 || argc == 9);
  cs_precision_arena arena;
  assert(cs_precision_arena_open(&arena, argc == 9 ? number(argv[5]) : UINT64_C(16777216)));
  nonlinear(argv[1]); equality(argv[1]); refusals_and_loops(argv[1]);
  puts("conditional chart differential, equality/closure and refusal fixtures passed");
  if (argc == 9) actual(argv);
  mpfr_free_cache(); cs_precision_arena_close(&arena);
  return 0;
}
