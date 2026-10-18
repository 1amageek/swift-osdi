# Precision Worker Verification

## Purpose and Scope
Parent: [swift-osdi](../../DESIGN.md). No children. Own reproducible static builds and focused behavioral/resource evidence.

## Responsibilities and Boundaries
Build the same production C sources using installed static MPFR/GMP archives. Preserve exact compiler, SDK, headers, archives, source and worker identities. Test actual child IPC and independent high-precision mathematical references; complete model/circuit qualification remains owned by CoreSpice.

`build_worker.py` accepts explicit `--cc`, `--sdk`, `--mpfr-prefix` and
`--gmp-prefix` selections. An omitted numeric prefix is resolved through the
selected `--pkg-config` executable's `includedir` and `libdir`; discovery never
installs dependencies. The native macOS qualification still links exact static
archives and rejects non-system dynamic dependencies. Receipts retain the actual
compiler, SDK settings, discovery tool, numeric headers/archives and source hashes
and refuse changes to those inputs during the build.

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

`verify_models.py --manifest` replays every explicitly supplied `families` entry,
with exact program/setup identities, four coordinate vectors, two contexts and
per-entry precision/arena/work limits. `--families` records a deliberate subset.
The existing `--snapshots` directory profile retains the four legacy regression
points, including SOI 4.7.0; it does not imply latest-model qualification.

`independent_replay.py build` compiles the tool-local independent `reference.c`
against only the public precision-program schema and selected static MPFR/GMP
archives. `--oracle-register-limit` makes admission capacity explicit and does not
alter evaluation semantics. The build receipt binds compiler, source/header
snapshots, numeric libraries/headers, SDK settings and resulting binary. Generic
success/refusal controls precede SOI 101 numerical comparison.
`verify_soi101.py` uses this local helper without private sibling imports; its
compiler/SDK/numeric selections use the same explicit discovery interface as the
worker builder. The owned generic interpreter and minimal replay/comparison code
were transferred from CoreSpice verification tools; `oracle-provenance.json`
retains original paths, SHA-256 identities and the repository HEAD separately
from the untracked/ignored source content. Package licensing covers the transferred
owned code. No model physics or circuit solver is copied into these helpers.
The SOI qualification receipt remains pending until independent build/control/
comparison bindings and all four cold-resource refusal pairs pass finalization.
A finalization failure records a failed receipt, even when the positive point
evaluations passed. Exact independent replay inputs are rechecked after the child
exits; changed inputs cannot publish successful numerical evidence.
`verify_soi101.py` requires Python's normal assertion-enabled execution profile.
The module refuses optimized execution (`-O`, `-OO` or `PYTHONOPTIMIZE`) before
imports, evidence reads/writes or child startup, preserving the binding and
qualification checks implemented with assertions.

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

## Smooth-Box Worker and Process Evidence

`verify_range.py` drives the real generic worker with IPC version 2, retaining the
464-byte control and fourteen-region layout. Four input planes contain lower/upper
normalized twofold coordinates; four N-vector and four N*N-matrix output planes
contain outward F/Q/G/C endpoints. A small nonlinear source-faithful program has
independent Decimal closed-form whole-box bounds, including time dependence.
Closed face, floor crossing, abs/min/max tie, domain and independent singleton
identity counterexamples require explicit whole-request refusal. A successful
source-time box retains `programHasTimeInput`; it does not supply time partials.

`RangeTransportTests.c` links the production process owner, runs the same actual
worker/program and checks typed status mapping, wrong-operation admission before
IPC, input/time/topology bounds, preadmitted capacity/work, cancellation and
fallible exact-child cleanup. No fixed-output fake worker establishes numeric
range correctness. Every child and verification command has a deadline. The
range suite supplements unchanged point tests and makes no physical-model or
all-history claim.

Direct range children receive a fixed PATH and, when explicitly set by the
verification command, only `ASAN_OPTIONS` and `UBSAN_OPTIONS` from the parent
environment. Sanitized evidence records those controls; a successful unsanitized
run does not establish sanitizer behavior or imply inherited fail-fast options.

The numeric build profile retains `wireVersion: 1` for compensated point requests
and separately declares `rangeWireVersion: 2` and
`rangeSemantics: source-program-smooth-box-v2`. Range CLI verification binds that
exact profile and worker image. Optional `verify_range.py --executable --model
--worker --semantic-profile --numeric-profile --cli-output` exercises the actual
schema-2 `enclose` command and saved/reloaded endpoint bit patterns against the
compiler's compound-default model. Positive, zero-straddling and sub-ULP boxes
check absolute F/Q and spatial G/C; malformed/mixed inputs, time/coordinate order,
bit tampering and explicit resource exhaustion must produce no partial result.
The point CLI helper and schema remain unchanged.

## Range Structural Proof Falsifiers

The R2.1 proof-only lane retains all R1 expectations and tests two narrowly
admitted generic facts. Inactive exact-zero multiplication may remove source
activity only after operand poison, ordinary arithmetic/domain and finite-result
checks succeed. Independent active zero and prepared raw-alias zero cannot supply
that authority; undefined and nonfinite operands remain failures.

For the retained instruction structure `x*x`, then addition of an inactive finite
exact positive constant, then square root, then addition of the same fresh `x`
value token, whole-box F is compared with independent high-precision Decimal
`c / (sqrt(x*x+c)-x)` for negative x and spatial J with
`c / (sqrt(x*x+c)*(sqrt(x*x+c)-x))`. Numeric singleton equality does not prove source
identity. Copy, definite SELECT and phi must retain the valid token; a loop that
redefines the source register while retaining an earlier square-root value must
not reuse stale structure. Active/nonpositive constants and distinct inputs are
falsifiers of accidental pattern application. Ordinary poison and nonsmooth/domain
refusals remain observable. Receipts retain program/setup digests, source tool,
actual endpoint slabs, exact box inputs and expected/observed status. Optional
same-input v1 comparison checks output bits and work against the frozen worker;
it is distinct from v2 proof admission. The work falsifier uses exactly two
bounded children: a prefix budget must stop at the final shared-sum instruction,
then a budget allowing only 255 of its 256 refinement units must stop at the same
instruction with unchanged completed work. Omitting the new proof charge cannot
pass by exhausting an unrelated earlier operation. All execution remains root-owned.

## Conditional Chart Transport Verification

`ChartProcessTests.c` exercises the actual worker through the public native chart owner, including explicit equality versus formula closure, exact tape replay, all eight endpoint planes, borrowed address reuse, session-cumulative work/step limits, tape/capacity refusals, cancellation and retryable exact-child cleanup. Its optional actual-input mode reads the existing header-defined direct request format from `chart_requests.py`, with exact program/setup files. Source/manifest provenance remains that helper's authority; centers are model evaluation points, not circuit solutions or physical certificates.

`ChartWireTests.c` uses the actual C header to construct protocol-3 geometry, then falsifies version, header size, region overlap/count/overflow, reserved fields and post-ready immutable suffix mutation. It spawns the real worker with bounded waits and exact reaping. It does not synthesize numerical success. `chart_fixtures.py` additionally emits a SHA-bound manifest for `OSDIPrecisionChartTests`, whose public Swift path covers scoped guards, complete tape binding, equality/closure, cumulative quotas, source lease independence, wrong-operation/refusal, cancellation and preservation of both chart and retained cleanup causes. The point-only transport fixture now explicitly rejects unknown versions/header sizes. All execution is root-owned and externally timeout-bounded.

The qualified numeric profile retains point/range fields and separately records `chartWireVersion: 3`, `chartSemantics: source-program-conditional-formula-v3` and `chartWorkPolicy: session-cumulative-work-and-source-steps`. Source/header/archive/worker hashes remain the execution identity. These declarations identify the qualified operation; they do not grant physical model or chart-cover authority.

Exact-infinity falsifiers cover both primitive signs, an OPTBARRIER copy, explicit primitive constant indices, replay at another finite point and refusal of an incompatible closure tape. A finite `DBL_MAX*DBL_MAX` guard operand must fail binary64 projection while its selected F/Q/G/C remain finite. `ChartForgeryWorker.c` is an invalid-response generator only: finite-kind infinity, NaN masquerading as a primitive, out-of-range/wrong-sign/finite-leaf provenance and nonfinite output planes must all fail parent protocol validation. It never supplies numerical qualification evidence.

## Complete Decision Cover Qualification

`frontier_fixtures.py` emits exact ABI-1 programs/setup plus digest-bound `manifest.json` for `OSDI_DECISION_COVER_FIXTURES`. `FrontierTests.c` owns exact occurrence-prefix and arithmetic-cell behavior; `DecisionCoverTests.c` owns the independently replayed complete tree, source/binding/leaf falsifiers, refusal of correlated-invalid formulas and cumulative quotas. Alias tests include both metadata-in-tree and metadata-in-original-input counterexamples, with no successful publication or source work.

`DecisionCoverProcessTests.c` uses the actual public C operation and numeric worker for three-way irrational equality, all terminal planes, original box/time binding, stable borrowed addresses, cumulative work/step/guard/copy limits, cancellation, aliases, wrong-operation and undefined-branch refusal. Its actual mode reads the existing header-defined request capture and executes each of the four current source families. `DecisionCoverWireTests.c` falsifies v4 header/version/region/count/overflow/reserved and post-ready immutable suffix fields. `DecisionCoverForgeryWorker.c` runs the real source checker and then deliberately corrupts a response; those mutations are invalid-response evidence only. The public parent must reject pending/missing/duplicated/cyclic/forged-source/cell/provenance/input/counter/nonfinite-leaf packets and an exclusion without a contradictory-edge witness.

`OSDIPrecisionDecisionCoverTests` exercises public protocol existentials, exact binding/full original box, every leaf, source-lease independence, inline cell conversion, scoped address reuse, cumulative refusal, cancel/old-worker no-fallback and retained primary+cleanup causes. `OSDIPrecisionDecisionCoverValueTests` verifies canonical inline integer and limit validation without a process. Numeric profile records `coverWireVersion: 4`, `coverSemantics: source-program-complete-decision-cover-v4` and the explicit cumulative counter policy. This is finite decision coverage and terminal formula authority only; root/nonempty/flow/history/calibration remain unqualified here. Builds/runs are centralized by the parent task with exact source/header/profile/archive hashes and external timeouts.
