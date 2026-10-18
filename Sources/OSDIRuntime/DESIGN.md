# OSDIRuntime

## Purpose and Scope

Parent: [package](../../DESIGN.md). Children: [LibraryLoading](LibraryLoading/DESIGN.md),
[StableStorage](StableStorage/DESIGN.md), [PointEvaluation](PointEvaluation/DESIGN.md), [Precision Execution](PrecisionExecution/DESIGN.md), [DecisionCover](DecisionCover/DESIGN.md). Host-only artifact/library and storage ownership.

## Responsibilities and Boundaries

Compose bounded artifact reads, private materialization, upstream native loading,
immutable metadata, run-local storage and generated model initialization/evaluation.
Do not start a compiler, reimplement device equations, assemble a circuit matrix
or solve circuit analyses.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [Contracts](../OSDIContracts/DESIGN.md) | depends on | Pointer-free values | Shared portable vocabulary | No file/ABI ownership |
| [COSDI](../COSDI/DESIGN.md) | depends on | C admission and zeroed allocation | Native boundary | Trusted native code, not a sandbox |
| [Foundation](../../../CircuiteFoundation/DESIGN.md) | depends on | ArtifactAccessing/reference/termination | Acquisition identity | Close failures remain failures |

## Architecture

```text
LibraryLoading -> COSDI + Foundation host acquisition
StableStorage -> strong library lease + COSDI allocation
PointEvaluation -> exclusive execution lease + native setup/eval + raw F/Q/G/C views
```

## Contracts and Invariants

One Mutex protects owner state and lease count on every compiled host. Foreign
loader calls and client closures run outside the lock. Buffer addresses remain
stable until storage destruction. Only a successfully prepared point session exposes evaluation.

## State, Ownership, and Lifecycle

Unpublished acquisition owns read session and private path. Published library owner
retains native handle/private path, immutable facts and lease admission. StableStorage
owns a noncopyable lease. Close refuses in-use owners; successful close is idempotent.
Close failure quarantines the owner from new leases and preserves retry ownership.

| Logical state | Supported native storage / isolation | Read and mutation entry | Owner release |
|---|---|---|---|
| Published image, close lifecycle, lease count | `OSDILibraryOwner.State` / `Mutex` | `acquireLease`, `releaseLease`, `close` | Explicit close or retained cleanup error; final raw destruction |
| Failed unpublished image | `OSDIPendingLibraryCleanup.State` / `Mutex` | `close` -> private `closeRaw` | Same pending owner retained on retry failure |
| Model and instance bytes | Exclusive noncopyable `OSDIStorage` | Synchronous scoped `MutableSpan` callbacks | Free both buffers before releasing strong lease |

Runtime has no WASM/Embedded native-image profile and no conditional replacement
for these states, Sendable contracts or locks. Porting requires a real platform
loader and corresponding lifecycle/concurrency execution evidence.

## Verification and Change Impact

Real dynamic-library tests and public CLI/API establish the supported native profile.
ASan checks offsets/buffers/lifetime. No concurrency or architecture is qualified by
compilation alone. PointEvaluation extends these lifetimes to generated setup/eval. Circuit binding
and analyses must separately compose and verify this contract.
