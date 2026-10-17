# Library Loading

## Purpose and Scope

Parent: [OSDIRuntime](../DESIGN.md). No children. Own host acquisition and shared
library/lease lifecycle, including typed cleanup ownership.

## Responsibilities and Boundaries

Consume existing ArtifactAccessing sessions, materialize exact bytes privately,
verify identity, then call COSDI. Preserve source/read/native/cleanup failures.
No model initialization/evaluation, compiler process or matrix binding.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [COSDI](../../COSDI/DESIGN.md) | depends on | open/inspect/close | Actual upstream ABI | C close failures keep a retryable owner |
| [StableStorage](../StableStorage/DESIGN.md) | used by | Noncopyable storage lease | Prevents unload while storage exists | Client callbacks run without Mutex |
| [Foundation](../../../../CircuiteFoundation/DESIGN.md) | depends on | Source session and exact reference | Identity/availability separation | Final source receipt and close both required |

## Architecture

```text
source open -> bounded pages to private file -> successful termination
  -> private-file SHA256/size check -> COSDI open/inspect -> shared owner
shared owner Mutex -> open/closing/closed/close-failed + lease count
```

## Contracts and Invariants

The source's observed artifact ID, total materialized bytes and private-file ID must
match the requested ID. No native constructor runs before these checks. Private files
are read-only and retained until unload; loading a trusted image does not qualify
unobserved native dependencies. Native-image pointer values stay inside the owner.
No global registry or environment variables hold a library/model.

## State, Ownership, and Lifecycle

State transitions are serialized; C loader calls occur after admission and outside
Mutex. While closing, new storage leases refuse. In-use close is a failure with no
state change. On close failure, metadata values remain historical but native access
is quarantined; retry close is allowed. A failed open whose unload fails yields an
explicit pending-cleanup owner. Automatic destruction is a last-resort release;
explicit close carries fallible cleanup to the caller. A public library close that
fails native unload or private-file removal returns `cleanupRequired`, retaining a
cleanup-only delegate to the same shared library owner. Stack unwinding therefore
cannot destroy that owner before the caller observes the typed cause and chooses a
retry. The delegate owns no duplicate native handle and calls the internal close
operation directly. Every public pending-cleanup retry also retains that same
pending owner in `cleanupRequired` when native or filesystem cleanup fails, so
propagating a retry failure across another helper boundary preserves custody.
Automatic destruction invokes a private raw cleanup operation that cannot create a
self-retaining error. An intentional in-use refusal preserves its existing error
and owner state.

Native owner construction and publication accept a `sending OpaquePointer`: callers
relinquish the disconnected native-handle region on entry, including publication
failure. The initializer transfers that region into the sole Mutex-owned State.
No caller may retain an independently usable native handle after transfer; immutable
metadata contains only copied values. Evaluation leases additionally admit at most
one prepared session per shared image under this same Mutex; ordinary S1 storage
leases remain independent and may coexist. Internal synchronous handle borrowing
requires a live lease and runs its foreign-operation body after releasing Mutex.
`OSDIStorageLease` remains internal and noncopyable; `@usableFromInline` exports
its nominal type metadata because public noncopyable storage contains the lease in
its private layout. Optimized importing consumers may emit a symbolic reference
to that nominal descriptor. Metadata availability is an ABI obligation, not public
lease construction, state access or a second image authority. Stored ownership,
lease admission/release and Mutex isolation are unchanged.

Session destruction precedes evaluation-lease release. This is an ownership boundary, not unchecked
Sendable conformance or a target-specific synchronization exception.

## Failure, Concurrency, and Constraints

Budgets are caller owned. Reads are asynchronous; native loading and memory state
operations are synchronous. Cancellation is checked between bounded pages and
before native loading. In-process dlopen cannot be force-cancelled. A trusted
conforming image is required; arbitrary forged pointers are not safe in-process.

## Verification and Change Impact

Test tampered bytes/size, source/read/close failure, native descriptor refusal,
same-content independent images, active-lease close refusal and source/owner release.
Concurrency tests exercise metadata/lease/close access on the same owner. Future
foreign evaluation must retain a lease and avoid executing callbacks under Mutex.

Release compatibility verifies an actual external consumer link and model execution,
including its reference to `OSDIStorageLease` metadata; target compilation alone
cannot establish that the descriptor is exported.
