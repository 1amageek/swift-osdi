# OSDI Native Loading

## Purpose and Scope

Loading is a component of [COSDI](../DESIGN.md), with no children. It owns OSDI
0.4 ABI loading, descriptor validation and zeroed raw storage primitives. It has no
model initialization, evaluation, solver, compiler or interpreter.

## Responsibilities and Boundaries

The parent module owns the pinned upstream ABI header. The public
[header](../include/COSDI.h) exports only the opaque owner and inspection records.
No OSDI ABI structs are duplicated. Initial qualification is macOS arm64.
Native model code is trusted executable code. Budgeted structural checks do not
sandbox malicious native pointers or constructor code.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [COSDI](../DESIGN.md) | parent | Native library owner | Module boundary | Explicit close owns library lifetime |
| [Public header](../include/COSDI.h) | public contract | Status, limits, owner and borrowed inspection | Stable C boundary | Names expire only after successful close |

## Architecture

```text
path + limits -> Mach-O host inspection -> dlopen
  -> OSDI 0.4 symbols and exported descriptor stride
    -> budgeted structural and instance-offset validation
      -> opaque library owner -> borrowed descriptor information
        -> dlclose success -> free owner
```

## Contracts and Invariants

`cs_osdi_open` writes NULL to the output before work and publishes only a completely
validated owner. If provisional cleanup fails, the sole exception returns
CS_OSDI_CLOSE_FAILED with a nonnull cleanup-only owner: metadata access is rejected,
close may be retried, the primary diagnostic remains intact and cleanup_detail
contains the secondary dlclose failure. Version must be 0.4. Exported descriptor stride may exceed the
pinned header size but must preserve its alignment. Descriptor addresses use the
exported stride, including multi-descriptor and extended-stride libraries.
Metadata is an aggregate budget over exported descriptors, tables, aliases and
bounded strings. Storage budget bounds the combined model, one instance and advertised resistive/
reactive array-writer capacities of each descriptor. Inspection does not allocate
these model or Jacobian buffers.
All count products, pointer offsets, storage spans and alignment are checked before
arithmetic or access. Ground node sentinels are accepted only in ABI-permitted
positions. Absent instance offsets are accepted only in optional ABI fields.
The exported resistive/reactive Jacobian counts are array-writer capacities,
not an equality assertion about the final active entry flags. The pinned generator
records DAE counts before later simplification and exports those stored counts,
while descriptor flags reflect the final entries. Each capacity must cover its
active flagged entries. It may exceed the final num_jacobian_entries because
`sim_back/src/lib.rs` constructs/counts the DAE before PostDerivative optimization
and `dae.rs::sparsify` removes zero entries without recomputing stored counts.
Capacity-times-double bytes are checked against the remaining per-descriptor
storage budget before multiplication; no derived upper bound from the final
entry count is imposed. For example, the
real BSIM-BULK 107.2.1 descriptor has total 116, exported resistive/reactive
capacities 115/55 and final active flag counts 110/49; this is admitted.
All required callbacks are inspected but never called. The pinned generator
`openvaf/osdi/src/metadata.rs` at commit
`fdf2522b70f42793f64b1c72f0195c96dea0cc19` is the authority for sentinels:
node 2 may be UINT32_MAX for collapse/noise/input/delay input pairs; Jacobian
nodes and every node 1 must be real node indices. Optional residual/limit, reactive
pointer, bound-step and maximum-delay offsets may be UINT32_MAX. Mapping,
resistive-pointer, collapse and state-array offsets remain real aligned offsets
even for zero-length arrays. Parameter names contain num_alias plus one entries.

## State, Ownership, and Lifecycle

The opaque owner retains the dlopen handle, descriptor base, count, stride and
validated borrowed metadata until a successful explicit close. Swift owns
serialization of open-owner access, storage lifetime and close. Native model
metadata is immutable after its library constructors finish. C functions expose
no global mutable state. Borrowed strings and descriptor records must not outlive
the owner. Zeroed storage is caller-owned and must be freed exactly once before
its library closes if it contains library-relative data.

## Failure, Concurrency, and Constraints

Host capability and Mach-O architecture errors are distinct typed statuses, before
dlopen executes native initializers. dlopen, ABI, metadata, budget, allocation and
close failures include bounded field/detail diagnostics. A failed dlclose retains
the owner and borrowed data for retry; it never reports successful disposal. Failed
open cleans up its provisional handle; any cleanup failure is explicit. The caller
must serialize use and close of an owner. This is a trusted-native boundary, not
an arbitrary-pointer fuzzing safety claim.

## Verification and Change Impact

Swift tests own success/failure loading, descriptor inspection, bounds, extended
stride, zeroed storage and lifetime tests. Native clang fixtures use the unchanged
header. Actual generated OpenVAF models establish compatibility beyond fixture
shape. Other hosts remain unsupported until compile/link/runtime qualification.
Changing the public header requires the Swift loader/storage owner and its tests
to be rechecked together.
