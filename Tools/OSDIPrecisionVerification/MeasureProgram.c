/* Direct production evaluation with private-arena counters; no timing claims. */
#include "../../Sources/OSDIPrecisionWorker/Arena/Arena.h"
#include "../../Sources/OSDIPrecisionWorker/ProgramEvaluation/Program.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *message) {
  fprintf(stderr, "%s\n", message);
  exit(1);
}

static void *read_file(const char *path, size_t *length) {
  FILE *file = fopen(path, "rb");
  if (!file || fseek(file, 0, SEEK_END))
    fail("Cannot open input");
  long size = ftell(file);
  if (size < 0 || (uintmax_t)size > SIZE_MAX || fseek(file, 0, SEEK_SET))
    fail("Invalid input length");
  *length = (size_t)size;
  void *bytes = malloc(*length ? *length : 1);
  if (!bytes || fread(bytes, 1, *length, file) != *length || fclose(file))
    fail("Cannot read input");
  return bytes;
}

static void counters(const char *name, const cs_precision_arena *arena) {
  printf("\"%s\":{\"allocations\":%" PRIu64 ",\"reallocations\":%" PRIu64
         ",\"frees\":%" PRIu64 ",\"copiedBytes\":%" PRIu64
         ",\"liveBytes\":%" PRIu64 ",\"peakBytes\":%" PRIu64 "}",
         name, arena->allocations, arena->reallocations, arena->frees,
         arena->copied_bytes, arena->live_bytes, arena->peak_bytes);
}

static void evaluate(cs_precision_program *program,
                     cs_osdi_precision_control_v1 *control,
                     const cs_osdi_precision_control_v1 *admitted,
                     const double *vectors, double *outputs[8],
                     const double *expected, uint64_t expected_work) {
  uint64_t active = control->active_count, raw = control->raw_node_count;
  int status = cs_precision_program_evaluate(program, control, vectors,
      vectors + active, vectors + 2 * active, vectors + 3 * active, outputs);
  if (status || program->precision != admitted->requested_precision_bits ||
      program->math.work != expected_work)
    fail("Evaluation status, used precision, or logical work differs");
  size_t cursor = 0;
  for (unsigned index = 0; index < 8; index++) {
    size_t count = (size_t)(index < 6 ? raw : raw * raw);
    if (memcmp(outputs[index], expected + cursor, count * sizeof(double)))
      fail("Output bits differ from supplied snapshot");
    cursor += count;
  }
  if (control->arena_bytes != admitted->arena_bytes ||
      control->maximum_work_units != admitted->maximum_work_units ||
      control->maximum_precision_bits != admitted->maximum_precision_bits ||
      control->requested_precision_bits != admitted->requested_precision_bits)
    fail("Request limits changed");
}

int main(int argc, char **argv) {
  if (argc != 8)
    fail("Usage: measure-program PROGRAM SETUP REQUEST EXPECTED OUTPUT REQUESTS EXPECTED_WORK");
  uint16_t endian = 1;
  if (*(unsigned char *)&endian != 1 || sizeof(double) != 8)
    fail("Measurement requires the native little-endian binary64 host");
  char *end = NULL;
  uint64_t requests = strtoull(argv[6], &end, 10);
  if (!requests || !end || *end || requests > 100000)
    fail("Invalid steady request count");
  uint64_t expected_work = strtoull(argv[7], &end, 10);
  if (!expected_work || !end || *end)
    fail("Invalid expected work");
  size_t program_length, setup_length, request_length, expected_length;
  void *program_bytes = read_file(argv[1], &program_length);
  void *setup_bytes = read_file(argv[2], &setup_length);
  void *request_bytes = read_file(argv[3], &request_length);
  void *expected_bytes = read_file(argv[4], &expected_length);
  if (request_length < sizeof(cs_osdi_precision_control_v1))
    fail("Truncated request");
  cs_osdi_precision_control_v1 control;
  memcpy(&control, request_bytes, sizeof(control));
  uint64_t active = control.active_count, raw = control.raw_node_count;
  if (!raw || raw > UINT32_MAX - 1 || active > raw ||
      raw > SIZE_MAX / raw / sizeof(double) ||
      active > (SIZE_MAX - sizeof(control)) / (4 * sizeof(double)) ||
      request_length != sizeof(control) + 4 * active * sizeof(double) ||
      control.minimum_precision_bits != 128 ||
      control.maximum_precision_bits < control.minimum_precision_bits ||
      control.requested_precision_bits != control.maximum_precision_bits ||
      !control.arena_bytes || control.arena_bytes > SIZE_MAX ||
      !control.maximum_work_units || expected_work > control.maximum_work_units)
    fail("Invalid request geometry or explicit limits");
  const cs_osdi_precision_control_v1 admitted = control;
  if (raw > (SIZE_MAX / sizeof(double)) / (6 + 2 * raw))
    fail("Output geometry overflow");
  size_t output_count = (size_t)(6 * raw + 2 * raw * raw);
  if (expected_length != output_count * sizeof(double))
    fail("Expected output geometry differs");
  double *output_bytes = calloc(output_count, sizeof(double));
  if (!output_bytes)
    fail("Cannot allocate output storage");
  double *outputs[8];
  size_t cursor = 0;
  for (unsigned index = 0; index < 8; index++) {
    outputs[index] = output_bytes + cursor;
    cursor += (size_t)(index < 6 ? raw : raw * raw);
  }
  const double *vectors = (const void *)((unsigned char *)request_bytes + sizeof(control));
  cs_precision_arena arena;
  if (!cs_precision_arena_open(&arena, control.arena_bytes))
    fail("Cannot open private arena");
  cs_precision_arena opened = arena;
  cs_precision_program program;
  if (cs_precision_program_open(&program, program_bytes, program_length,
                                setup_bytes, setup_length, &control))
    fail("Program admission failed");
  cs_precision_arena prepared = arena;
  evaluate(&program, &control, &admitted, vectors, outputs, expected_bytes, expected_work);
  cs_precision_arena first = arena;
  for (uint64_t index = 0; index < requests; index++)
    evaluate(&program, &control, &admitted, vectors, outputs, expected_bytes, expected_work);
  cs_precision_arena steady = arena;
  FILE *output = fopen(argv[5], "wbx");
  if (!output || fwrite(output_bytes, sizeof(double), output_count, output) != output_count || fclose(output))
    fail("Cannot create complete output artifact");
  mpfr_free_cache();
  cs_precision_arena cache_released = arena;
  cs_precision_arena_close(&arena);
  printf("{\"steadyRequests\":%" PRIu64 ",\"precisionBits\":%" PRIu32 ","
         "\"arenaBytes\":%" PRIu64 ",\"maximumWork\":%" PRIu64 ","
         "\"logicalWorkPerRequest\":%" PRIu64 ",\"allOutputsBitIdentical\":true,"
         "\"arenaReleased\":true,\"snapshots\":{", requests,
         admitted.requested_precision_bits, admitted.arena_bytes,
         admitted.maximum_work_units, expected_work);
  counters("opened", &opened);
  printf(","); counters("prepared", &prepared);
  printf(","); counters("firstRequest", &first);
  printf(","); counters("steadyRequests", &steady);
  printf(","); counters("cacheReleased", &cache_released);
  printf("}}\n");
  free(program_bytes);
  free(setup_bytes);
  free(request_bytes);
  free(expected_bytes);
  free(output_bytes);
  return 0;
}
