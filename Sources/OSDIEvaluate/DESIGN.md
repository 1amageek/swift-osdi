# OSDIEvaluate

## Purpose and Scope

Parent: [package](../../DESIGN.md). No children. Own the S2 host-only point
preparation/evaluation CLI and its versioned input/result serialization.

## Responsibilities and Boundaries

Compose exact artifact loading, public model preparation/evaluation and explicit
cleanup. Preserve the caller's typed parameters, simulation parameters, Kelvin
temperature, connected terminal count, active unknowns and previous state. The
CLI does not assign internal unknowns, solve a circuit or commit next state.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [Runtime Point Evaluation](../OSDIRuntime/PointEvaluation/DESIGN.md) | depends on | prepare/evaluate/close | Public execution owner | Borrowed views are copied only at report boundary |
| [Evaluation Values](../OSDIContracts/ModelEvaluation/DESIGN.md) | depends on | Typed options and topology | Explicit input semantics | Active unknowns can be flow/thermal variables |
| [Library Loading](../OSDIRuntime/LibraryLoading/DESIGN.md) | depends on | Exact artifact and lifetime | Same admission as inspect | No compiler invocation |

## Architecture

```text
exact JSON bytes -> request SHA-256 + decoded input -> public exact artifact load
 -> prepare fixed dc-static-point profile -> topology/parameter/diagnostic facts
   -> operation prepare: preparation-only report
   -> operation evaluate: check explicit M unknowns / previous state
      -> owned raw N F/Q and row-major N*N G/C + diagnostics/next-state report
 -> prepared close -> library close -> source close/wait -> atomic JSON save/reload
```

## Contracts and Invariants

Schema version 1 requires an explicit `operation` of `prepare` or `evaluate` and
explicit load/read/evaluation budgets. `prepare` publishes setup facts at stage
`osdi-preparation` with no numerical result. `evaluate` additionally requires
explicit `localUnknowns` and `previousState` arrays; the preparation report enables
the caller to discover their dimensions/order without guessing internal values. The only accepted
profile is `dc-static-point`: F/Q/G/C and operating-point calculation with DC/static
flags, time zero and limiting disabled. Scalar parameter types and scope remain
explicit. Array parameter assignments are unsupported, never scalarized.

The report binds exact request bytes by SHA-256/byte count, repeats the decoded
input and exact model identity, and records the tool schema version, prepared
parameter facts, raw node nature/units/mapping, inputs, outputs and model flags.
Raw output dimensions remain descriptor N and N*N; input dimensions are prepared
active M and state count. Candidate next state is not automatically accepted.

## State, Ownership, and Lifecycle

One command owns one prepared model. Report arrays are materialized only while
public evaluation views are valid. No view or native pointer is serialized or
retained. Explicit prepared close precedes library/source close. Cleanup failures
retain failure status and primary/secondary diagnostics.

## Failure, Concurrency, and Constraints

Invalid request, profile, parameter type/scope, Kelvin temperature and vector size
exit nonzero with structured diagnostics. Native setup/evaluation and resource
failures preserve native status and setup/model flags. JSON write or round-trip
verification failure also exits nonzero. Native calls require trusted conforming
artifacts and an external process timeout; the CLI provides no native sandbox.

## Verification and Change Impact

Use the shared independently captured C manifest for real OpenVAF RC and the
single exact BSIM artifact. Compare saved/reloaded CLI F/Q/G/C, maps, parameter
facts and failure reports with public API and direct ABI evidence. Existing
`osdi-inspect` remains unchanged. Schema changes require this design, README and
CLI integration evidence to be updated together.

## S4 Dynamic Schema

Schema 1 and dc-static-point input/output defaults remain unchanged. Schema 2
uses profile dynamic-point and an explicit context (mode, timeSeconds and optional
noiseFrequencyHz), retains prepared noise metadata and returns density values and
optional maximumTimestep. JSON materialization remains the external output boundary.
Requests and reports bind exact bytes and generated artifacts as before; normal
JSON decoding verifies saved reports. Dynamic input never silently falls back to DC.
