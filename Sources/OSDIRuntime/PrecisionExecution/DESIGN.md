# Precision Execution

## Purpose and Scope

Parent: [OSDIRuntime](../DESIGN.md). No children. Own the separate optional precision boundary; ordinary evaluation remains unchanged.

## Responsibilities and Boundaries

Own cold program admission, exact worker staging, exclusive process ownership and scoped result borrowing. No interval arithmetic, solver or accepted history.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [Precision Process](../../COSDI/PrecisionProcess/DESIGN.md) | depends on | Versioned bounded IPC | Exact twofold inputs and enclosed outputs | Failure publishes no view |

## Architecture

```text
immutable program/setup + exact worker -> exclusive precision session
 -> four input spans + endpoint contexts -> bounded helper -> scoped F/delta-Q enclosure + G/C midpoint spans
```

## Contracts and Invariants

Status: parent D1 cross-owner contract frozen; implementation and behavioral qualification follow this contract.

## S4D Precision Session — D1 Contract

Selected precision composition uses a separate noncopyable Sendable
`OSDIPrecisionSession`, owning only the cold-copied program, exact worker, slabs and
child process. CoreSpice's outer owner retains both ordinary model and precision
session. The ordinary `OSDIPreparedModel.close()` signature and ownership remain
unchanged. A library lease protects program extraction; no library pointer or
model callback survives the deep copy into the precision session. [Model Evaluation](../../OSDIContracts/PrecisionEvaluation/DESIGN.md)
owns input/result semantics; [Precision Process](../../COSDI/PrecisionProcess/DESIGN.md#s4d-precision-execution--d1-contract)
owns the optional compiler interface and native workspace. No accepted solver or
native history moves into Runtime.

```text
exact image + optional precision capability + explicit precision options
 -> ordinary setup + program extraction + bounded isolated helper admission
 -> precision setup/arena + shared slabs/pipes (atomic publication)
 -> candidate high/low/context + accepted-reference high/low/original-context
 -> synchronous shared-slab request, cancellation/deadline-aware wait outside locks
 -> checked raw centers/radii + declared linearization + candidate outcomes
 -> scoped view -> consumer alone decides acceptance
 -> helper kill/reap if necessary -> slab release
 -> precision cold provenance/resource release
outer owner alone -> ordinary evaluator destroy
```

Precision admission either constructs the requested helper/slabs or releases
unpublished resources on failure. It receives an already prepared ordinary model
for cold snapshot extraction. Immutable capabilities and provenance are copied
once. The selected precision and maximum workspace are explicit caller policy;
configuration must not resize a live workspace below retained storage. Inputs are
finite, normalized nonoverlapping pairs with matching dimensions and endpoint
identity. Validation must not collapse `high + low` into one binary64 value.

One mutating synchronous call borrows all endpoint spans and the noncopyable
precision-session owner. Result F/delta-Q center/radius and G/C midpoint spans cannot escape the consumer
closure. Callback reentry, close, cancellation and failure use one Mutex-protected session
handle and lifecycle. Only short admission/state transfer occurs under that lock;
shared child/mapping state is never accessed through an Embedded-only raw owner. Foreign evaluation, destruction and
client callbacks remain outside Mutex. No conditional weakening of ownership or
Sendable is introduced.

A helper wake byte is not sufficient: Runtime acquire-validates the matching
response sequence/status before any output borrow. It then validates version/profile,
raw dimensions, finite centers, nonnegative finite radii, selected derivative
semantics, fresh bound and outcome facts. Unsupported/exhausted precision never
invokes the consumer and is not converted into the ordinary lane or a solver
step-retry hint. Cancellation before publication discards the candidate. Runtime invalidates a failed precision child without closing the ordinary outer
model. It never silently respawns. An unreaped child retains a retryable cleanup
owner; failed operations expose no result spans.

Ordinary overloads, binary64 S1/S2/S4 state behavior and CLI schema defaults remain
unchanged. The new precision lane requires a regenerated extension-capable artifact
and its exact compiler/numeric-runtime provenance. Historic `8369952` allocation
and timing measurements cannot certify new precision workspace behavior. New
parent/slab workspace, fixed worker arena and allocation/copy/IPC counts require
their own bounded measurement after implementation; full all-four-model integration remains owned
by CoreSpice S4D.

Precision session `close()` is explicit and fallible. It takes exclusive cleanup
authority under Mutex, then terminates/reaps/releases outside Mutex. Failed bounded
cleanup transfers the still-owned native context into `OSDIPendingPrecisionCleanup`
and a typed retained-cleanup error; no second owner or global orphan reaper is
created. The failed session cannot evaluate or implicitly restart. The cleanup
owner retries only cleanup and retains exact PID/group/fd/mapping identity.
The existing pending-library-cleanup policy governs final deinitialization: an
unfulfilled release invariant is not silently ignored or turned into success.
Success, operation failure plus cleanup failure, cancellation, explicit close and
deinit must each preserve exactly-once native ownership.

## Verification and Change Impact

Focused real worker transport, failure, scoped view and cleanup tests own this boundary. Numeric and compiler correctness remain separately verified by their owners.

## Concrete Public Seam

`OSDINativePrecisionSessionPreparer.snapshot(library:descriptorIndex:model:preparation:semanticProfile:options:)`
checks the ordinary model's immutable original image, descriptor and preparation
facts, then copies the optional compiler program under a cold image lease. The
returned `OSDIPrecisionProgram` exposes immutable program/setup byte arrays and
their SHA-256 digests for independent replay. Only explicit given assignments are
serialized; compiler metadata owns primitive defaults and source SSA owns all
compound defaults. Unknown parameter flags and record strides fail admission.

`prepare(program:worker:source:numericProfile:options:)` admits an exact statically
qualified helper through `ArtifactAccessing`, stages it privately and starts the
bounded process adapter. The helper artifact identity binds numeric code; the
external qualifier verifies a content-only canonical numeric profile (including
actual worker SHA and numeric archive/header/source/compiler identities) and its
separate build receipt. The caller supplies the qualified profile digest; runtime
checks artifact bytes and retains that declared profile assumption, without
inferring mathematical qualification from a digest alone. Runtime does not search mutable dylib paths.

`OSDIPrecisionEvaluating.evaluate` accepts four borrowing spans and two explicit
contexts. `OSDIPrecisionEvaluationView` returns `fHigh/fLow/fRadius`,
`deltaQHigh/deltaQLow/deltaQRadius`, midpoint `g/c`, used precision/work/sequence
and a fresh optional maximum timestep. No view crosses await. Any helper failure
closes the precision session before throwing, retaining cleanup-only ownership
if bounded cleanup fails. The ordinary prepared model remains caller-owned.

| State | Native storage and isolation | Read/mutation | Release |
|---|---|---|---|
| Precision handle | `OSDIPrecisionSession.State` / `Mutex` | Short exclusive handle admission; evaluate/close outside lock | Fallible close or retained cleanup owner |
| Pending cleanup | `OSDIPendingPrecisionCleanup.State` / `Mutex` | Short lifecycle admission; child/file I/O outside lock | Exact reap, unmap, fd close and directory removal |

Only native Apple arm64 is admitted. WASM/Embedded receive unsupported C process
admission and have no alternate raw state or weakened Sendable path.

The hard workspace policy covers bounded program/setup cold snapshots, slab bytes
and the worker arena separately. It does not claim a bound on process RSS, images,
stack, OS runtime or numeric-library TLS. Qualified-worker receipts report measured
startup overhead and after-ready allocation behavior for their exact platform;
a measured overhead is not a universal maximum.

## Causal Range Execution

The [precision range contract](../../OSDIContracts/PrecisionEvaluation/DESIGN.md#causal-range-contract)
owns box and result semantics. The [system boundary](../../../DESIGN.md#causal-range-boundary)
owns the constitutive certificate's scope. Runtime adds no arithmetic, solver,
accepted history, or separate process owner.

```text
same exact program/setup + exact worker/source/profile/options
 -> prepareRange -> existing noncopyable OSDIPrecisionSession(operation: range)
 -> lower high/low + upper high/low + same-mode lower/upper contexts
 -> cs_osdi_range_process_evaluate (protocol v2, outside Mutex)
 -> checked complete outward F/Q/G/C endpoint view -> synchronous callback
 -> same fallible close / retained cleanup authority
```

`OSDIPrecisionRangeSessionPreparing` and `OSDIPrecisionRangeEvaluating` are separate
protocols implemented by the existing native preparer/evaluator. The version 1
protocol requirements, signatures and point result view remain unchanged.
`prepareRange(program:worker:source:numericProfile:options:)` reuses exact artifact
staging and policy admission, selecting `cs_osdi_range_process_create` rather than
the point constructor. Creation is atomic; every unpublished failure transfers
the native handle and staging directory to the existing cleanup owner.

The session stores an immutable point/range discriminator alongside immutable
program/provenance/options. The only mutable handle remains in its existing
`Mutex<State>`. Either operation checks that discriminator before a request or
callback; a mismatch throws `OSDIError.invalidRange` without invalidating a valid
session. No I/O, child wait, client callback or destruction occurs under Mutex.

`evaluateRange(session:lowerHigh:lowerLow:upperHigh:upperLow:lowerContext:upperContext:precisionBits:_:)`
checks dimensions and same-mode contexts before the bounded native call. Native
admission owns exact finite normalized endpoint ordering and numerical validity.
The scoped nonescapable `OSDIPrecisionRangeEvaluationView` borrows eight planes:
`fLower/fUpper/qLower/qUpper` in raw-node order and
`gLower/gUpper/cLower/cUpper` in row-major raw-node order. It also publishes used
precision, completed work, matching sequence, optional fresh maximum timestep and
the conservative immutable `programHasTimeInput` fact. Runtime checks dimensions
and the Boolean fact before publication. The native boundary validates output
alignment, initialized regions, finite ordered endpoints and transport geometry.

Process failures close the same session before throwing; failed bounded cleanup
retains the same cleanup-only owner. Callback reentry, cancellation, close and
Sendable contracts are unchanged. Native/WASM/Embedded storage type, isolation,
read/mutation entrypoints and release remain exactly the existing matrix above;
unsupported platforms do not acquire an alternate raw handle path.

| Platform | Handle storage / isolation | Read / mutation | Shutdown / owner release |
|---|---|---|---|
| Native | Existing `Mutex<State>` | Same `withLock` admission and operation discriminator; native work outside lock | Same fallible close and retained cleanup owner |
| WASM | Same `Mutex<State>` and Sendable declarations | Same source entrypoints; process admission is unsupported | Same cleanup contract; no alternate raw owner |
| Embedded | Same `Mutex<State>` and Sendable declarations | Same source entrypoints; process admission is unsupported | Same cleanup contract; no alternate raw owner |

Focused range tests own actual worker range success, nonsmooth refusal, time fact,
context/dimension/operation mismatch, failure-before-callback and cleanup. Existing
point tests own version 1 regression. The process/interpreter tests own numeric
enclosure and regularity correctness; upper consumers must separately prove any
causal/history/flow claim.

## Conditional Chart Execution

The [chart contract](../../OSDIContracts/PrecisionEvaluation/DESIGN.md#conditional-chart-contract) supplies semantics; Runtime composes the same noncopyable session with immutable `.chart` operation and caller limits. `prepareChart` stages the exact worker and copies the same program/setup once. The chart binding contains model reference, descriptor, complete topology and program/setup/semantic digests. `evaluateChart` rejects a mismatched tape binding before publication. External circuit projection is not inferred here.

```text
prepareChart(program, worker, profile, options, limits)
 -> existing Mutex handle + immutable binding/limits
 -> validate scoped input box + optional exact-bound tape
 -> protocol 3 outside lock -> validated chart result
 -> synchronous borrowed planes + guard collection
 -> same close/retained cleanup owner
```

`OSDIPrecisionChartEvaluationView` and its guard collection are nonescapable and expire at callback return. The callback receives binding and explicit result semantics; it may explicitly copy individual decisions into its own tape. Mapping pointers cannot escape. No guard array is automatically materialized. Native/WASM/Embedded use the identical existing Mutex/Sendable storage and cleanup matrix; unsupported process platforms have no alternate state path. The public chart error preserves primary and all nested cleanup errors without changing old error enums. Only chart requests use session-cumulative work and step counters; old point/range per-request policy remains unchanged.

Verification owns API binding/operation/shape refusal before transport, exact record/replay on actual worker, closure tags, cumulative quotas, no callback on failures/cancel, source lease closure, stable borrowed regions and exactly-once explicit/failed cleanup. Lower arithmetic and upper physical obligations retain their owners.

Guard view indexing preserves the explicit signed primitive-infinity kind and constant index after native O(1) provenance validation. Runtime never coerces an ordinary nonfinite REAL into this kind. The existing borrowed storage, tape binding and error cleanup contracts remain unchanged.
