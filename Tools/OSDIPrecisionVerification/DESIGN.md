# Precision Worker Verification

## Purpose and Scope
Parent: [swift-osdi](../../DESIGN.md). No children. Own reproducible static builds and focused behavioral/resource evidence.

## Responsibilities and Boundaries
Build the same production C sources using installed static MPFR/GMP archives. Preserve exact compiler, SDK, headers, archives, source and worker identities. Test actual child IPC and independent high-precision mathematical references; complete model/circuit qualification remains owned by CoreSpice.

## Related Designs
| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [COSDIPrecision](../../Sources/COSDIPrecision/DESIGN.md) | verifies | Arithmetic, program, resource and IPC | Exact worker path | Focused fixtures do not certify physical models |

## Architecture
```text
production C sources + static archives -> exact worker receipt -> real IPC fixtures
                                                   -> independent Decimal containment
interval operators -> separate 512-bit MPFR reference -> sampled containment
```

## Contracts and Invariants
All subprocesses have external timeouts. Worker failure exposes no successful view. The nonlinear sub-ULP fixture distinguishes complete evaluation from F(high)+G(high)*low. Source setup/phi/resource fixtures reject plausible incomplete implementations. Expression identity tests include a BULK-shaped duplicate division/power predicate, an unequal expression with overlapping enclosures, colliding hash buckets, work refusal and fixed identity-capacity exhaustion. Inactive-value/control fixtures require a disabled undefined intermediate to succeed while demanded values, unsupported control regions, effects, loops and external entries fail. Normalized candidate/reference pairs are asserted before real-model runs. The projection regression covers a rounded low component reaching an odd high component midpoint.

## Verification and Change Impact
Run build_worker.py with --output-dir, then verify_worker.py with --worker under an external timeout. ArithmeticTests.c links the same production arithmetic/arena/identity sources. verify_models.py reuses exact native snapshot bytes and records explicit raw-point vectors, both contexts, all output slabs, first-request timing and optional OS-heap interposition samples; these points are not circuit solutions. Repeat using address/undefined sanitizers. Record steady process/IPC latency separately from arithmetic allocation requests; zero total allocation is not assumed.

## Same-Condition Performance Comparison

`benchmark_models.py` compares two immutable qualified worker images on the same
explicit program/setup/vector/context snapshots. Each variant/round uses a fresh
process; order alternates by round. Cold admission, first request and uninterrupted
steady evaluation are reported separately. Full output-bit and work/precision
comparisons occur outside the timing interval. Input packing, IPC, output
materialization, diagnostic reads and optional heap interposition are identical.
A global deadline kills/reaps the active child; immutable output reservation and
atomic receipts preserve both success and failure. Median, range and MAD describe
repeatability; no significance or full-circuit speed claim is inferred. Optional
heap counters do not measure private arena activity, peak live memory or all copies.
Latest-model qualification is independent of the existing-program benchmark.

Both comparison and private-arena measurement default to every entry in the
snapshot report's `families` mapping. `--families` selects an explicit subset;
receipts record selected and excluded entries. Each entry supplies its precision,
arena bytes, maximum work, expected completed work, vectors and contexts. Tools
validate wire ranges and preserve these exact limits without substituting budgets.
The existing 256-bit/64-MiB/100-million-work snapshots remain valid; a larger model
uses its own explicitly recorded arena budget. The verification helper admits a
128-bit minimum and sets requested and maximum precision to the supplied precision.

## Private-Arena Measurement

```text
exact program/setup + control/vectors + expected output bits
  -> MeasureProgram linked with production Program/Arithmetic/Identity/Arena
  -> arena open -> program open -> first evaluation -> steady evaluations
  -> MPFR cache release -> bulk arena close
  -> counter snapshots + final output bits -> Python identity/bit verification
```

`measure_program.py` owns an untimed resource measurement, separate from the IPC
latency benchmark. `MeasureProgram.c` opens the production arena before program
admission, reuses the program and input/output buffers, and checks all eight output
slabs plus used precision and completed work on the first and every steady request.
It records cumulative arena counters at open, admission, first request, steady
completion and MPFR cache release. Receipts derive first/steady counter differences
and retain absolute live/peak values. The executable must be built from recorded
production sources and static archives; its build receipt establishes that binding.

| Counter | Meaning and boundary |
|---|---|
| allocations / reallocations / frees | Operations served by the private arena, including GMP/MPFR hooks |
| copiedBytes | Bytes copied by arena realloc when an allocation moves; other explicit copies are excluded |
| liveBytes / peakBytes | Requested live payload and its high-water mark; block headers and unused reserved capacity are excluded |
| arenaBytes | Explicit reserved arena capacity; distinct from requested live payload and OS process memory |

OS allocations outside the arena, OS allocator lifetime/peak, and copies outside
arena realloc are not measured by these counters. Zero realloc-copy bytes do not
establish total zero-copy behavior. MPFR cache release occurs while the arena is
valid and has its own snapshot. Bulk close releases the arena backing allocation;
remaining live claims are invalidated rather than reported as individually freed.
Failure or deadline expiration prevents a successful receipt. Existing output
receipts are never overwritten; each new invocation reserves its own output path.
