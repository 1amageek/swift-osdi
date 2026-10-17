# Point Evaluation

## Purpose and Scope

Parent: [OSDIRuntime](../DESIGN.md). No children. Own synchronous prepared point
sessions over actual OSDI setup/eval callbacks; no circuit assembly or solver.

## Responsibilities and Boundaries

Bridge explicit model/default and instance assignments, Kelvin temperature and
connected terminals to COSDI. Copy immutable topology, parameter facts and setup
diagnostics. Expose raw F/Q and row-major G/C plus explicit next state as scoped
views. The fixed profile is F/Q/G/C + OP + DC + STATIC at time zero with limiting
disabled. Previous state is caller supplied; no implicit accepted-history commit.

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

Explicit close takes the pointer under Mutex, destroys it outside Mutex, then drops
the lease. Deinit destroys native storage before stored lease destruction. Closed
models reject evaluation. A failed evaluation never invokes the result consumer.

## Failure, Concurrency, and Constraints

COSDI owns checked workspace arithmetic, setup/eval status and bounded diagnostics.
Swift preflights lexical parameter table/string allocations against caller limits;
metadata copies are cold, bounded by admitted native metadata and explicit string
limits. Parameter flags and post-setup effective values are copied without inventing
defaults. Native failures preserve model flags, setup error count/code and parameter
ID. Nonzero model flags and malformed/null result ranges never become success.
All foreign callbacks, deallocation and client callbacks occur outside Mutex.

## Verification and Change Impact

[OSDIEvaluationTests](../../../Tests/OSDIEvaluationTests/DESIGN.md) executes
generated setup/eval, explicit/given parameters,
collapse/internal variables, state input, typed failures and close/lease lifetime.
Pinned-toolchain typecheck, SIL region checks and negative view-escape probes cover
the ownership seam; runtime/allocation evidence is required for performance claims.
