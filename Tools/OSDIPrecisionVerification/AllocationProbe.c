#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
/* Test-only interposition, never linked into the qualified worker. */
static _Atomic uint64_t counters[5];
static void *observed_malloc(size_t n) {
  atomic_fetch_add(&counters[0], 1);
  atomic_fetch_add(&counters[4], n);
  return malloc(n);
}
static void *observed_calloc(size_t n, size_t s) {
  atomic_fetch_add(&counters[1], 1);
  atomic_fetch_add(&counters[4], n * s);
  return calloc(n, s);
}
static void *observed_realloc(void *p, size_t n) {
  atomic_fetch_add(&counters[2], 1);
  atomic_fetch_add(&counters[4], n);
  return realloc(p, n);
}
static void observed_free(void *p) {
  atomic_fetch_add(&counters[3], 1);
  free(p);
}
static ssize_t observed_write(int fd, const void *bytes, size_t count) {
  ssize_t result = write(fd, bytes, count);
  if (fd == 5 && result == 1) {
    uint64_t values[5];
    for (unsigned i = 0; i < 5; i++)
      values[i] = atomic_exchange(&counters[i], 0);
    /* Dedicated fd 6 belongs only to this verification harness. */
    if (write(6, values, sizeof(values)) != (ssize_t)sizeof(values))
      _exit(83);
  }
  return result;
}
__attribute__((used)) static struct {
  const void *replacement, *original;
} interposers[] __attribute__((section("__DATA,__interpose"))) = {
    {(const void *)observed_malloc, (const void *)malloc},
    {(const void *)observed_calloc, (const void *)calloc},
    {(const void *)observed_realloc, (const void *)realloc},
    {(const void *)observed_free, (const void *)free},
    {(const void *)observed_write, (const void *)write}};
