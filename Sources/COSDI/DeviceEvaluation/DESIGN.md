# Native Device Evaluation

## Purpose and Scope

DeviceEvaluation is a child of [COSDI](../DESIGN.md), with no children. It owns one
prepared native OSDI 0.4 model/instance and bounded, stable point-evaluation buffers.
It supplies no solver, circuit assembly, integration algorithm or model equations.

## Responsibilities and Boundaries

Use the admitted library and unchanged upstream descriptor directly. Swift holds
an exclusive evaluator lease on the materialized native image for the entire C
context lifetime. C never closes that library. Model callbacks run outside Swift
owner locks, with a stable C logger and the evaluator as their opaque handle.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [COSDI](../DESIGN.md) | parent | Public C boundary | Native module composition | No public upstream structs |
| [Loading](../Loading/DESIGN.md) | depends on | Admitted descriptor and library lifetime | Structural validation | The caller retains a lease |
| [Public header](../include/COSDI.h) | public contract | Prepare, metadata, evaluate and destroy | C/Swift boundary | Views are bounded borrows |

## Architecture

```text
admitted library + exclusive Swift lease + explicit options
  -> owned parameter/simulation strings and stable zeroed storage
    -> parameter access SET -> setup_model -> setup_instance
      -> checked setup outcomes -> collapse map -> raw Jacobian pointer binding
        -> prepared evaluator
          -> explicit active unknowns + previous states
            -> fixed native eval -> raw F/Q/G/C + next states
              -> scoped result view -> destroy -> release Swift lease
```

## Contracts and Invariants

Preparation is atomic: every failure destroys private C storage, and only a fully
prepared evaluator is published. Scalar real, int32 and string assignments use
canonical names or aliases and exact case. Duplicate assignments in the same
scope, ambiguous names, type mismatches, operating-point writes and array writes
are explicit failures. Model scope includes model parameters and instance defaults;
instance scope admits only instance parameters. Native access SET records given
bits. Model setup precedes instance setup, which receives positive finite ambient
Kelvin and an explicit connected-terminal count. Numeric/string simulation values
are explicit; no gmin or other simulation parameter is inferred. Prepared reads
report resolved values and both model/instance given flags.

The fixed evaluation profile is resistive/reactive residuals and Jacobians plus
operating-point, DC and static analysis; absolute time is zero and limiting is
not enabled. Absdelay descriptors are unsupported in this point profile. Previous
state input has exactly the descriptor state count. Each call copies it into both
native previous and next buffers, preserving untouched slots; next state is an
output and is never implicitly committed. State indices are initialized to their
identity mapping. No limit-function dispatch is needed when ENABLE_LIM is off.

Collapse mapping is deterministic: ground wins, otherwise the lowest raw node is
the representative; active representatives receive dense indices in ascending raw
order. Public mapping entries are dense indices or UINT32_MAX ground. Native solve
index zero is ground and other indices are public dense index plus one. Input
contains all active unknowns, including internal ones, without inferred values.

F and Q are raw descriptor-node vectors read from validated optional offsets;
absent ABI residual fields are exactly zero. G and C are raw row-major N-by-N
matrices populated through stable bound pointers and native Jacobian loaders with
reactive alpha one. Their storage is zeroed before each additive load. These raw
values are not circuit-assembled results; the separate map defines their relation
to active unknowns. Names, units, nature references and flow tags preserve native
metadata. Packed array-writer capacities and ordering are not used for evaluation.

## State, Ownership, and Lifecycle

The C context owns all shim allocations: model/instance, solve, raw results,
previous/next states, collapse working arrays, copied strings, simulation arrays
and bounded diagnostics. Their addresses remain stable until destruction. Input
borrows last only for a call; result/diagnostic views expire on the next evaluator
operation or destruction. Native metadata borrows require the library lease.
Destroy frees shim allocations and performs no model callback or dlclose.

## Failure, Concurrency, and Constraints

Every native setup result is inspected; allocated init-error arrays are freed on
all paths. The structured error preserves model flags, error count and first native
code/parameter identity. Any model evaluation flag, nonfinite consumed output or
diagnostic-budget exhaustion rejects publication and clears the output view.
Fatal, finish, stop and limiting flags retain their native bit mask. Logger
messages are copied into bounded diagnostic storage and freed unless LOG_FMT_ERR
marks static formatting-failure text. No Swift callback crosses the native frame.

Workspace arithmetic is checked before allocation or multiplication; diagnostic
and string sublimits are included in the workspace budget. Native generated code
is trusted executable code and can allocate internally before returning; these
native allocations cannot be preemptively capped by the shim's allocation budget.
The caller serializes every evaluator operation and retains the exclusive lease.

## Verification and Change Impact

[OSDIEvaluationTests](../../../Tests/OSDIEvaluationTests/DESIGN.md) owns the
consumer regressions; the [independent oracle](../../../Tools/OSDIEvaluationVerification/DESIGN.md)
owns direct-ABI and finite-difference evidence.

Real generated RC and BSIM-BULK 107.2.1 fixtures own setup/eval compatibility,
typed assignments, collapse mapping, scalar outputs and raw matrix behavior.
Independent raw-C, analytic RC and finite-difference evidence own numerical
qualification. Failure fixtures own setup errors, return flags, nonfinite values,
unsupported arrays/history and budget refusal. Native sanitizer checks own buffer,
callback and lifetime paths. Changing the profile, mapping or output axes requires
Swift contracts, CLI and independent verification to change together.

Pinned source authority is OpenVAFReloaded commit
`fdf2522b70f42793f64b1c72f0195c96dea0cc19`: `osdi/src/access.rs` (SET/given scopes),
`setup.rs` (model/instance order and errors), `stdlib.c` (errors/logging/state),
`eval.rs` (flags and state indices), `load.rs` (additive pointer loaders), and
`hir_lower/src/expr.rs` (limiting gate).
