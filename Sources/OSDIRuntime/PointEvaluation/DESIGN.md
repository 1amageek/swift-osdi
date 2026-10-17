# Point Evaluation

## Purpose and Scope

Parent: [OSDIRuntime](../DESIGN.md). No children. Own synchronous prepared point
sessions over actual OSDI setup/eval callbacks; no circuit assembly or solver.

## Responsibilities and Boundaries

Bridge explicit model/default and instance assignments, Kelvin temperature and
connected terminals to COSDI. Copy immutable topology, parameter facts and setup
diagnostics. Expose raw F/Q and row-major G/C plus explicit next state as scoped
views. The legacy overload retains F/Q/G/C + OP + DC + STATIC at time zero;
the context overload selects the explicit S4 phase below. Limiting remains disabled. Previous state is caller supplied; no implicit accepted-history commit.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [LibraryLoading](../LibraryLoading/DESIGN.md) | depends on | Exclusive evaluation lease and internal handle borrow | Image lifetime | C callbacks run outside Mutex |
| [COSDI](../../COSDI/DESIGN.md) | depends on | Atomic prepare, evaluate, metadata, destroy | Native stable buffers | No escaped Swift Array pointer |
| [ModelEvaluation](../../OSDIContracts/ModelEvaluation/DESIGN.md) | depends on | Immutable options, limits, topology and parameters | Portable facts | No native pointer authority |

## Architecture

```text
library Mutex: admit exclusive evaluation lease
 -> lock-free C prepare with lexical input buffers
 -> immutable facts + noncopyable Sendable prepared model
 -> mutating evaluate: short pointer read under Mutex
 -> lock-free C eval -> synchronous nonescaping borrowed view
 -> C destroy -> release evaluation lease -> library can close
```

## Contracts and Invariants

A shared image admits one prepared evaluation session; ordinary S1 storage leases
may coexist. Preparation acquires the lease before any callback. Its strong owner
keeps all library pointers alive. Preparation failure destroys unpublished native
session buffers before releasing the lease. Initializers transfer the fresh native
pointer using `sending`; no unchecked Sendable conformance is used.

The prepared model is noncopyable and Sendable. Its pointer is held by Mutex; public
mutation and Swift exclusivity own evaluation/close ordering. No second class or
per-call in-flight state duplicates this owner. Evaluation borrows caller input
spans only during the C call; C owns stable solve, state and F/Q/G/C buffers. Result
spans and diagnostic views cannot escape the synchronous consumer callback or cross
await. There is no unconditional per-evaluation String/Array materialization.
The C result record is invocation-local stack/value transport: the innermost lexical
input borrow creates and fills it, then returns an immutable copy of its pointer
fields and status. No mutable result record is captured by input closures or retained
as backing for the output spans. The output addresses belong to the native session,
whose exclusive owner remains alive throughout the consumer callback. This uses
ordinary Swift value return and does not add manual temporary allocation or extend
an unsafe pointer lifetime.

Output F/Q have raw descriptor node count N; G/C have N*N row-major entries. Inputs
have active unknown count M, including internal variables. Raw nodes retain their
units, nature and raw-to-active/ground mapping. State input and next-state output
have the advertised state count. Unknown/terminal vectors are not conflated.

## State, Ownership, and Lifecycle

| State | Owner / isolation | Release |
|---|---|---|
| Shared image and session admission | Library owner Mutex | Session lease destruction |
| Native session pointer | Prepared model Mutex | Explicit idempotent close or deinit |
| Native model/instance/solve/results/state | COSDI exclusive session | Destroy before lease |
| Immutable facts | Sendable copied values | Ordinary value lifetime |

The nested `OSDIPreparedModel.State` has internal visibility so the fixed Swift
6.4 compiler can emit valid layout metadata when an importing module stores the
public noncopyable model inside its own Mutex-protected session. The stored Mutex
remains private; this visibility does not grant public state or pointer access.
The imported layout must compile and link without introducing a second owner.

Explicit close takes the pointer under Mutex, destroys it outside Mutex, then drops
the lease. Deinit destroys native storage before stored lease destruction. Closed
models reject evaluation. A failed evaluation never invokes the result consumer.

## Failure, Concurrency, and Constraints

COSDI owns checked workspace arithmetic, setup/eval status and bounded diagnostics.
Swift preflights lexical parameter table/string allocations against caller limits;
metadata copies are cold, bounded by admitted native metadata and explicit string
limits. Cold preparation snapshots the immutable evaluation limits before the
local string-budget accumulator captures them. This keeps the accumulator's mutable
byte count separate from the borrowed options wrapper and permits Swift 6.4 Release
stack promotion without invalid ownership instructions. All budget predicates and
failure cases use the same limits; point evaluation and its allocation path are
unchanged. Parameter flags and post-setup effective values are copied without inventing
defaults. Native failures preserve model flags, setup error count/code and parameter
ID. Nonzero model flags and malformed/null result ranges never become success.
All foreign callbacks, deallocation and client callbacks occur outside Mutex.

## Verification and Change Impact

[OSDIEvaluationTests](../../../Tests/OSDIEvaluationTests/DESIGN.md) executes
generated setup/eval, explicit/given parameters,
collapse/internal variables, state input, typed failures and close/lease lifetime.
Pinned-toolchain typecheck, SIL region checks and negative view-escape probes cover
the ownership seam; runtime/allocation evidence is required for performance claims.

S3 cross-module regression evidence belongs to CoreSpiceOSDI: its concrete session
stores `OSDIPreparedModel` under Mutex, then exercises preparation, point evaluation
and close. The failing compiler metadata linkage and corrected consumer build/link
are required alongside the existing native S1/S2 behavior tests; a same-module build
alone does not verify this boundary.

The Swift 6.4 Release regression additionally compiles the actual OSDIRuntime and
CoreSpiceOSDI consumer with normal optimized settings. A reduced local accumulator
reproduces the borrowed-field capture compiler assertion; preparation budget
success/failure tests verify that the immutable limits snapshot preserves behavior.

## S4 Dynamic Evaluation Contract

The context overload maps DC to DC|STATIC, AC to AC, transient to TRAN, explicit
initialization to TRAN|IC|STATIC, and noise to AC|NOISE with CALC_NOISE. All request
F/Q/G/C and OP; limiting stays disabled. Existing overloads select DC at time zero.
Native previous state and Q/integrator history remain separate. Every trial uses
caller-provided accepted state and returns a candidate without committing it.

Prepared noise facts copy descriptor identities and raw endpoints under the same
library lease. Noise evaluation loads PSD for the explicit frequency immediately
after the same model callback. The scoped density span is empty in other modes.
The optional maximum timestep comes from this call's fresh native bound slot;
absent offset or positive infinity means no bound, finite positive values mean
seconds, and other values fail. Neither a result view nor native pointer escapes.

The same exclusive Mutex-backed noncopyable owner serializes all operations.
Callbacks and native destruction remain outside locks. No new accepted history,
heap owner or per-call allocation is introduced. Workspace includes a stable PSD
buffer sized by admitted descriptor source count. Analytic mode/time/IC/noise and
state controls, all four exact model families, Debug/Release consumers and scoped
allocation measurements own the new evidence.
