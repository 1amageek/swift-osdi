#include "../../Sources/COSDIPrecision/ProgramEvaluation/GuardedGraph.h"
#include "../../Sources/COSDIPrecision/Arena/Arena.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NDEBUG
#error "Guarded graph verification requires enabled assertions"
#endif

#define NONE CS_OSDI_PRECISION_NONE
static int checked_bytes(size_t count, size_t stride, size_t *bytes) {
  if (stride && count > SIZE_MAX / stride) return 0;
  *bytes = count * stride;
  return 1;
}
typedef struct {
  cs_precision_program p;
  CSOsdiPrecisionProgramHeader header;
  CSOsdiPrecisionFunction functions[3];
  CSOsdiPrecisionBlock blocks[6];
  CSOsdiPrecisionInstruction instructions[4];
  CSOsdiPrecisionConstant constants[2];
  CSOsdiPrecisionInput inputs[2];
  CSOsdiPrecisionPhiEdge phi[2];
  CSOsdiPrecisionRoot roots[5];
} fixture;

static void initialize(fixture *f, uint32_t comparison) {
  memset(f, 0, sizeof(*f));
  f->header.registers.count = 9;
  f->header.functions.count = 3;
  f->header.blocks.count = 6;
  f->header.instructions.count = 4;
  f->header.constants.count = 2;
  f->header.inputs.count = 2;
  f->header.phi_edges.count = 2;
  f->header.roots.count = 5;
  f->functions[0] = (CSOsdiPrecisionFunction){1, 0, 0, 1, 0, 0};
  f->functions[1] = (CSOsdiPrecisionFunction){2, 1, 1, 1, 0, 0};
  f->functions[2] = (CSOsdiPrecisionFunction){3, 2, 2, 4, 0, 0};
  for (uint32_t b = 0; b < 6; b++)
    f->blocks[b] = (CSOsdiPrecisionBlock){b < 2 ? b : 2, 0, 0, 0, 0, 0, NONE, NONE, NONE, 0};
  f->blocks[2] = (CSOsdiPrecisionBlock){2, 0, 1, 0, 0, 2, 3, 3, 4, 0};
  f->blocks[3] = (CSOsdiPrecisionBlock){2, 1, 1, 0, 0, 1, NONE, 5, NONE, 0};
  f->blocks[4] = (CSOsdiPrecisionBlock){2, 2, 1, 0, 0, 1, NONE, 5, NONE, 0};
  f->blocks[5] = (CSOsdiPrecisionBlock){2, 3, 1, 0, 2, 0, NONE, NONE, NONE, 0};
  f->instructions[0] = (CSOsdiPrecisionInstruction){comparison, 3, 0, 1, NONE, 0};
  f->instructions[1] = (CSOsdiPrecisionInstruction){44, 4, 0, 8, NONE, 0};
  f->instructions[2] = (CSOsdiPrecisionInstruction){46, 5, 0, 0, NONE, 0};
  f->instructions[3] = (CSOsdiPrecisionInstruction){74, 7, 3, 6, 1, 0};
  f->constants[0] = (CSOsdiPrecisionConstant){1, 1, 0};
  f->constants[1] = (CSOsdiPrecisionConstant){2, 1, UINT64_C(0x3ff0000000000000)};
  f->inputs[0] = (CSOsdiPrecisionInput){2, 0, 10, 0, NONE, 0};
  f->inputs[1] = (CSOsdiPrecisionInput){2, 8, 11, 0, 0, 0};
  f->phi[0] = (CSOsdiPrecisionPhiEdge){6, 3, 4, 0};
  f->phi[1] = (CSOsdiPrecisionPhiEdge){6, 4, 5, 0};
  f->roots[0] = (CSOsdiPrecisionRoot){0, 5, 2, 0, 1, 0};
  f->roots[1] = (CSOsdiPrecisionRoot){2, 1, 7, 0, NONE, 0};
  f->roots[2] = (CSOsdiPrecisionRoot){2, 2, 6, 0, NONE, 0};
  f->roots[3] = (CSOsdiPrecisionRoot){2, 3, 2, 0, 0, 0};
  f->roots[4] = (CSOsdiPrecisionRoot){2, 4, 1, 0, 0, 0};
  f->p.header = &f->header; f->p.functions = f->functions;
  f->p.blocks = f->blocks; f->p.instructions = f->instructions;
  f->p.constants = f->constants; f->p.inputs = f->inputs;
  f->p.phi = f->phi; f->p.roots = f->roots;
  f->p.math.limit = UINT64_MAX;
}
static int compare(uint32_t op, double x) {
  switch (op) {
  case 53: return x < 0;
  case 54: return x > 0;
  case 55: return x >= 0;
  case 56: return x <= 0;
  case 58: return x == 0;
  case 62: return x != 0;
  default: abort();
  }
}
/* Independent small source evaluator. It follows the extracted guarded syntax,
   not the extractor's masks/DFS, and discriminates the equality arm at x=0. */
static double interpret(const cs_precision_guarded_graph *g, double x) {
  const cs_precision_program *p = g->program;
  double v[9] = {x, 0, 1, 0, 0, 0, 0, 0, 1};
  uint32_t b = 2, previous = NONE;
  for (unsigned steps = 0; steps < 6; steps++) {
    const CSOsdiPrecisionBlock *block = &p->blocks[b];
    for (uint32_t j = 0; j < block->phi_edge_count; j++) {
      const CSOsdiPrecisionPhiEdge *phi = &p->phi[block->first_phi_edge + j];
      if (phi->predecessor_block == previous) v[phi->destination] = v[phi->source];
    }
    for (uint32_t j = 0; j < block->instruction_count; j++) {
      const CSOsdiPrecisionInstruction *i = &p->instructions[block->first_instruction + j];
      if (i->opcode == 44) v[i->destination] = v[i->a] + v[i->b];
      else if (i->opcode == 46) v[i->destination] = v[i->a] * v[i->b];
      else if (i->opcode == 74) v[i->destination] = v[i->a] ? v[i->b] : v[i->aux];
      else v[i->destination] = compare(i->opcode, v[i->a]);
    }
    if (!block->terminator) return v[6];
    previous = b;
    b = block->terminator == 1 || v[block->condition] ? block->then_block : block->else_block;
  }
  abort();
}
static void synthetic(void) {
  size_t bytes = 0;
  assert(checked_bytes(3, sizeof(cs_precision_graph_definition), &bytes));
  assert(bytes == 3 * sizeof(cs_precision_graph_definition));
  bytes = 17;
  assert(!checked_bytes(SIZE_MAX, 2, &bytes) && bytes == 17);
  fixture f;
  cs_precision_graph_definition d[9];
  cs_precision_graph_block b[6];
  cs_precision_guarded_graph result = {0};
  uint32_t ops[] = {53, 54, 55, 56, 58, 62};
  for (unsigned j = 0; j < sizeof(ops)/sizeof(*ops); j++) {
    initialize(&f, ops[j]);
    assert(!cs_precision_guarded_graph_extract(&f.p, d, 9, b, 6, &result));
    assert(result.reachable_blocks == 6 && !result.has_cycles && !result.terminal_effects);
    assert(result.program->instructions[0].opcode == ops[j]);
    assert(d[0].root_mask & (1u << CS_OSDI_PRECISION_ROOT_Q));
    assert(d[0].root_mask & CS_PRECISION_GRAPH_CONTROL_DEPENDENCY);
    assert(d[2].root_mask & (1u << CS_OSDI_PRECISION_ROOT_Q)); /* prior setup root */
    assert(d[4].root_mask & (1u << CS_OSDI_PRECISION_ROOT_Q));
    assert(d[5].root_mask & (1u << CS_OSDI_PRECISION_ROOT_Q));
    for (int x = -1; x <= 1; x++)
      assert(interpret(&result, x) == (compare(ops[j], x) ? x + 1 : x*x));
  }
  initialize(&f, 53);
  f.instructions[1].a = 2;
  f.instructions[2].a = f.instructions[2].b = 2;
  assert(!cs_precision_guarded_graph_extract(&f.p, d, 9, b, 6, &result));
  /* x now affects only control; its F/Q dependence must not disappear. */
  assert(d[0].root_mask & (1u << CS_OSDI_PRECISION_ROOT_F));
  assert(d[0].root_mask & (1u << CS_OSDI_PRECISION_ROOT_Q));
  initialize(&f, 53);
  f.blocks[5].terminator = 1; f.blocks[5].then_block = 2;
  assert(!cs_precision_guarded_graph_extract(&f.p, d, 9, b, 6, &result));
  assert(result.has_cycles && result.reachable_blocks == 6);
  initialize(&f, 53);
  f.instructions[1] = (CSOsdiPrecisionInstruction){73, NONE, 4, NONE, NONE, 0};
  /* Terminal branch has no root-producing definition. Its dead phi source is
     retained syntactically but cannot invent a defined source register. */
  assert(cs_precision_guarded_graph_extract(&f.p, d, 9, b, 6, &result) == CS_OSDI_PRECISION_INVALID_PROGRAM);
  f.phi[0].source = 2;
  assert(!cs_precision_guarded_graph_extract(&f.p, d, 9, b, 6, &result));
  assert(result.terminal_effects == 1 && b[3].terminal_instruction == 1);
  assert(result.program->instructions[b[3].terminal_instruction].a == 4);
  initialize(&f, 53);
  f.blocks[5].phi_edge_count = 1;
  assert(cs_precision_guarded_graph_extract(&f.p, d, 9, b, 6, &result) == CS_OSDI_PRECISION_INVALID_PROGRAM);
  initialize(&f, 53);
  f.phi[1].predecessor_block = 3;
  assert(cs_precision_guarded_graph_extract(&f.p, d, 9, b, 6, &result) == CS_OSDI_PRECISION_INVALID_PROGRAM);
  initialize(&f, 53);
  f.phi[1].predecessor_block = 2;
  assert(cs_precision_guarded_graph_extract(&f.p, d, 9, b, 6, &result) == CS_OSDI_PRECISION_INVALID_PROGRAM);
  initialize(&f, 53);
  cs_precision_guarded_graph unchanged = result;
  assert(cs_precision_guarded_graph_extract(&f.p, d, 8, b, 6, &result) == CS_OSDI_PRECISION_INVALID_INPUT);
  assert(!memcmp(&result, &unchanged, sizeof(result)) && f.p.math.work == 0);
  assert(!cs_precision_guarded_graph_extract(&f.p, d, 9, b, 6, &result));
  uint64_t work = f.p.math.work;
  f.p.math.limit = work;
  unchanged = result;
  assert(cs_precision_guarded_graph_extract(&f.p, d, 9, b, 6, &result) == CS_OSDI_PRECISION_WORK_EXHAUSTED);
  assert(f.p.math.work == work && !memcmp(&result, &unchanged, sizeof(result)));
  initialize(&f, 53); f.p.math.limit = work - 1;
  assert(cs_precision_guarded_graph_extract(&f.p, d, 9, b, 6, &result) == CS_OSDI_PRECISION_WORK_EXHAUSTED);
}
static void *read_file(const char *path, size_t *length) {
  FILE *file = fopen(path, "rb");
  assert(file && !fseek(file, 0, SEEK_END));
  long size = ftell(file);
  assert(size >= 0 && (uint64_t)size <= UINT64_C(67108864));
  assert(!fseek(file, 0, SEEK_SET));
  *length = (size_t)size;
  void *bytes = malloc(*length ? *length : 1);
  assert(bytes && fread(bytes, 1, *length, file) == *length && !fclose(file));
  return bytes;
}
static uint64_t number(const char *text) {
  char *end;
  uint64_t n = strtoull(text, &end, 10);
  assert(n && end != text && !*end);
  return n;
}
static void real_program(char **argv) {
  size_t pn, sn;
  void *program = read_file(argv[1], &pn), *setup = read_file(argv[2], &sn);
  assert(pn >= sizeof(CSOsdiPrecisionProgramHeader) && sn >= sizeof(CSOsdiPrecisionSetupHeader));
  const CSOsdiPrecisionProgramHeader *h = program;
  const CSOsdiPrecisionSetupHeader *sh = setup;
  cs_osdi_precision_control_v1 control = {0};
  control.protocol_version = CS_OSDI_RANGE_PROTOCOL_VERSION;
  control.raw_node_count = h->raw_node_count; control.active_count = sh->active_unknown_count;
  control.descriptor_index = h->descriptor_index;
  control.minimum_precision_bits = control.maximum_precision_bits = 128;
  control.maximum_work_units = number(argv[4]);
  cs_precision_arena arena;
  assert(cs_precision_arena_open(&arena, number(argv[3])));
  cs_precision_program p;
  assert(!cs_precision_program_open(&p, program, pn, setup, sn, &control));
  size_t definition_bytes, block_bytes;
  assert(checked_bytes(h->registers.count, sizeof(cs_precision_graph_definition), &definition_bytes));
  assert(checked_bytes(h->blocks.count, sizeof(cs_precision_graph_block), &block_bytes));
  cs_precision_graph_definition *d = calloc(1, definition_bytes);
  cs_precision_graph_block *b = calloc(1, block_bytes);
  assert(d && b);
  cs_precision_guarded_graph graph;
  assert(!cs_precision_guarded_graph_extract(&p, d, h->registers.count, b, h->blocks.count, &graph));
  uint32_t predicates = 0;
  for (uint32_t j = 0; j < h->blocks.count; j++)
    if (p.blocks[j].terminator == CS_OSDI_PRECISION_BRANCH) {
      uint32_t r = p.blocks[j].condition;
      assert(d[r].root_mask & CS_PRECISION_GRAPH_CONTROL_DEPENDENCY);
      predicates++;
    }
  assert(predicates > 0 && graph.reachable_blocks > 0);
  for (uint32_t j = 0; j < h->roots.count; j++)
    assert(d[p.roots[j].value].root_mask & (1u << p.roots[j].kind));
  printf("actual-program guarded graph: %" PRIu32 " predicates, %" PRIu32
         " reachable blocks, %" PRIu64 " work units\n", predicates, graph.reachable_blocks, p.math.work);
  free(d); free(b); mpfr_free_cache(); cs_precision_arena_close(&arena);
  free(program); free(setup);
}
int main(int argc, char **argv) {
  assert(argc == 1 || argc == 5);
  synthetic();
  puts("guarded graph synthetic differential/refusal tests passed");
  if (argc == 5) real_program(argv);
  return 0;
}
