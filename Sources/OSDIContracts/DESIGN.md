# OSDIContracts

## Purpose and Scope

Parent: [package](../../DESIGN.md). Children: [LibraryMetadata](LibraryMetadata/DESIGN.md),
[ModelEvaluation](ModelEvaluation/DESIGN.md), [Precision Evaluation](PrecisionEvaluation/DESIGN.md), [DecisionCover](DecisionCover/DESIGN.md), [ConstitutiveIdentities](ConstitutiveIdentities/DESIGN.md).
Own portable immutable values used during native model admission and point evaluation.

## Responsibilities and Boundaries

Carry bounds and pointer-free metadata. No dynamic loading, raw ABI structures,
serialization, digest computation, file paths, solver or model equations.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [Runtime](../OSDIRuntime/DESIGN.md) | used by | Validated limits and descriptor values | Native admission | Metadata is not initialized device state |
| [Foundation](../../../CircuiteFoundation/DESIGN.md) | depends on | Portable exact artifact identity | Shared vocabulary | No host imports |

## Architecture

```text
caller-selected bounds -> validated limits
upstream native admission -> pointer-free immutable descriptor values
```

## Contracts and Invariants

All values are Sendable. Positive resource bounds and nonnegative descriptor counts
are checked. Metadata never grants authority to invoke a model or assume a circuit
analysis is supported.

## Verification and Change Impact

Value failure tests belong to OSDIRuntimeTests. Native ABI/ownership tests belong
to Runtime and COSDI, and cannot be replaced by value construction tests.
