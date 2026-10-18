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

## Explicit Precision Request

`--precision-request <json> --output <json>` selects schema1/profile
`stateless-precision-v1`. Existing `--request` schemas/defaults remain unchanged.
An explicit `operation` selects `snapshot` or `evaluate`, without a default.
Snapshot admits the exact worker/program and reports `osdi-precision-snapshot`
with no evaluation arrays; evaluate requires all four endpoint vectors/contexts
and reports `osdi-precision-evaluation`. A snapshot cannot decode as an evaluated
result. The precision request embeds an ordinary preparation request and requires the
exact worker ArtifactReference/path/read budget, semantic and numeric profile
SHA-256, staging directory, every precision resource option, selected bits, four
endpoint high/low vectors and original endpoint contexts. Numeric vectors and
context scalars carry their binary64 bit patterns; reload verifies those patterns
rather than collapsing high+low. Precision preparation accepts no ignored ordinary
localUnknowns/previousState fields. Requests are read with a fixed 16 MiB bound.

```text
explicit request -> exact image load -> ordinary prepare -> immutable snapshot
 -> exact worker acquisition -> precision session -> scoped view -> owned report
 -> fallible precision close -> ordinary close -> library/root close
 -> bounded atomic JSON save and strict same-bit reload
```

The result retains full program/setup bytes and digests, setup parameter/topology
facts, actual worker/model references and supplied profile identities, original
endpoint pairs/context bits, F and delta-Q high/low/radius arrays, G/C midpoints,
used precision/work/sequence and fresh timestep. The `prepared` section records
the ordinary cold setup metadata used for extraction; its binary64 compound
parameter projections do not replace source-stage precision expressions. The
program/setup bytes remain authoritative for precision replay. Program/setup/profile identities
are recomputed or checked on reload. G/C remain inexact Newton aids. No ordinary
fallback, hidden worker reuse, solver decision or accepted state is introduced.
Output capacity includes copied byte arrays, numeric values/bit patterns and
metadata before result materialization; the explicit report ceiling is enforced
again at encoding. Malformed identity/pairs, missing capability, foreign failure,
limits and cleanup failures publish failure only. Primary and cleanup causes are
preserved recursively. The command retries a returned cleanup-only owner once;
cleanup still cannot become successful evaluation evidence.

The precision CLI verification tool invokes the actual built executable under
bounded subprocess timeouts, saves/reloads exact bits, checks the compound-default
sub-ULP result against independent decimal arithmetic, repeats fresh sessions and
checks malformed requests, capability/identity/worker/resource failures. No
transport-only worker is used as numerical qualification.

## Explicit Smooth-Box Request

The same `--precision-request` host command accepts the separate schema-2 profile
`stateless-smooth-box-v2` with operation `enclose`. It requires explicitly named
`lowerHigh/lowerLow/upperHigh/upperLow` vectors and `lowerContext/upperContext`;
all point candidate/reference fields must be absent. The version-1 precision
request and report retain their existing meaning and remain unchanged.
Range execution selects the public range preparation/evaluation requirements on
the same exact program/worker and cleanup lifecycle. Its report stage is
`osdi-smooth-range-enclosure`, and only `rangeEvaluation` contains absolute F/Q
and spatial G/C lower/upper endpoints. A point `evaluation` must be absent.

All eight endpoint planes retain finite binary64 values and exact bit patterns,
checked dimensions/order and complete input box/context/model/program/profile
binding. The conservative `programHasTimeInput` fact is serialized without claiming
explicit time partials. Source regularity or domain refusal publishes only failure.
No CLI result certifies a circuit, continuity across boxes, all-history behavior or
an unsupported seam. Capacity admission accounts for four vector and four matrix
planes plus their JSON value/bit representations before copying borrowed results;
atomic save and strict reloaded validation remain mandatory.
