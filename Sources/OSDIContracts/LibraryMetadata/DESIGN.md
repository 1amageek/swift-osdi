# Library Metadata

## Purpose and Scope

Parent: [OSDIContracts](../DESIGN.md). No children. Own caller limits and immutable
published descriptor facts.

## Responsibilities and Boundaries

Validate representable nonnegative sizes/counts. Preserve upstream meaning; do not
infer parameters, device equations, disciplines or analysis support from names.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [COSDI](../../COSDI/DESIGN.md) | coordinates with | Validated descriptor facts | Source of metadata | Pointer validity is a native trust assumption |
| [Runtime](../../OSDIRuntime/DESIGN.md) | used by | Limits and descriptor IDs/counts | Admission/storage allocation | Runtime retains the actual library |

## Architecture

```text
C descriptor admission -> copied name + dense index + counts/byte sizes
caller limits -> positive validated resource values
```

## Contracts and Invariants

Dense descriptor indices refer to one exact loaded library. Strings are display
metadata, not identity. Limits are explicit caller values, not universal operating
constants. Instance parameter count cannot exceed total parameter count; terminal
count cannot exceed node count. The storage admission limit covers the combined
model/instance size and advertised resistive/reactive double-array capacities for
one descriptor; this bounds metadata-advertised storage, not actual aggregate
allocations. S1 storage allocation owns only model and instance bytes. COSDI owns
the checked capacity arithmetic. A DTO does not contain a raw function/pointer.

## Verification and Change Impact

Invalid bounds/counts reject. Actual foreign descriptor validation and lifetime
remain owned by COSDI/Runtime tests. Changes affect storage admission and CLI output.
