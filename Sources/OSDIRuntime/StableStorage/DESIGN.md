# StableStorage

## Purpose and Scope

Own stable, zeroed model and instance byte storage for one validated OSDI descriptor.
Parent: [OSDIRuntime](../DESIGN.md). No children. This component does not initialize,
bind, evaluate, or interpret a model.

## Responsibilities and Boundaries

`OSDIStorage` uniquely owns two allocations and one library storage lease. It admits
their combined byte count before allocation and provides exclusive scoped mutable
byte access. The library owner owns loading, descriptor admission and close policy.
COSDI owns calloc/free. Neither public API nor stored descriptor information exposes
native model or allocation pointers.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [OSDIRuntime](../DESIGN.md) | parent | Validated descriptor and storage lease | Runtime composition | A lease must remain live while storage exists |
| [COSDI](../../COSDI/DESIGN.md) | depends on | `cs_osdi_allocate_zeroed` and `cs_osdi_free` | Stable zeroed allocation | Positive sizes can fail; zero sizes require no allocation |

## Architecture

```text
validated descriptor + consuming library lease + byte budget
 -> checked combined admission
 -> allocate model -> allocate instance
 -> exclusive noncopyable OSDIStorage
    -> scoped model MutableSpan<UInt8>
    -> scoped instance MutableSpan<UInt8>
 -> free both blocks -> destroy lease -> library close becomes eligible
```

## Contracts and Invariants

Descriptor byte counts must be nonnegative and their checked UInt64 sum must fit
`maximumByteCount`. No allocation occurs before admission. Each positive allocation
is stable, disjoint, zeroed and owned exactly once; a zero-byte block is nil and
lends the canonical empty `MutableSpan<UInt8>`. Neither lending nor mutation resizes,
replaces or copies a block. The descriptor is available as immutable value metadata.

`withModelBytes` and `withInstanceBytes` require exclusive `inout` access to storage
and invoke a synchronous nonescaping callback with `inout MutableSpan<UInt8>`.
`Result` retains its implicit Escapable constraint, so a span cannot escape as the
return value. The callback error type is generic and propagates unchanged. No
post-initialization admission failure is introduced. Deliberate unsafe pointer
extraction remains subject to the caller's scoped-pointer contract; no raw-pointer
accessor is public. There is no Sendable conformance or shared mutable state.

## Runtime Flows

Successful initialization validates counts and budget, allocates and binds model
bytes, then allocates and binds instance bytes. Only then does the completed storage
consume the lease into its stored owner. Failure of the instance allocation frees
the provisional model block before throwing. All failure paths destroy the incoming
lease. Lending obtains an exclusive span over the original block and ends it before
returning or throwing; callback failure preserves the allocation and previous writes.

## State, Ownership, and Lifecycle

The noncopyable storage is the sole owner of both allocations. Native code provides
zeroed raw memory; initialization binds the full allocation once to trivial UInt8.
UInt8 has stride and alignment one. The byte count is validated as a nonnegative
Int before binding or projection; there are no element-offset products or unchecked
integer conversions. Every byte is initialized and mutable for the storage lifetime.
The only aliases are scoped spans under exclusive access to the same owner.

The deinitializer body frees model and instance blocks exactly once. Swift destroys
the stored lease after the deinitializer body, so library lifetime outlasts both
frees. A failed initializer frees provisional allocations itself before argument
and partial-value destruction. Storage has no async work, global registry, shutdown
race, conditional target isolation, or separately shareable payload owner.

## Failure, Concurrency, and Constraints

Negative byte counts produce `OSDIError.invalidRange`. Combined-count arithmetic
overflow or a count above the caller budget produces `OSDIError.resourceLimit`.
A nil allocation for a positive count produces
`OSDIError.allocationFailed(byteCount:)`. These failures never publish partial
storage. Errors thrown by a lending callback retain their original type and value.
The library close gate remains held for the complete storage lifetime.

Qualification is limited to the fixed Swift 6.4.0 RELEASE macOS arm64 host. Native
OSDI loading and execution capabilities are not inferred for WASM or Embedded.

## Verification and Change Impact

Runtime tests must observe zero initialization of both buffers, persistent writes,
stable addresses across repeated borrows, disjoint allocations, empty buffers,
exact-limit admission and over-budget refusal. Explicit and inferred throwing
callbacks must propagate original consumer errors without losing storage. Lifetime
tests keep a library close blocked while storage exists and allow close after its
blocks and lease are released. Allocation-failure tests cover failure of either
positive allocation and cleanup of the provisional first block. A negative compile
probe must reject returning a borrowed MutableSpan from the callback.

Tests belong to the OSDIRuntime test target. Changes to descriptor counts, C allocation
semantics, the lease owner or public lending signatures invalidate the corresponding
admission, pointer/lifetime or callback evidence and require those owners to be
rechecked together. No model-evaluation correctness is established by these tests.
