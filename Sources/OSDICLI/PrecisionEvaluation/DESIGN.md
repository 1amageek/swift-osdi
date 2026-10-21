# Precision CLI Command

## Purpose and Scope

Parent: [OSDICLI](../DESIGN.md). No children. Own explicit precision snapshot,
point and smooth-range JSON requests and reports within the `osdi` executable.

## Responsibilities and Boundaries

Invoke public Runtime snapshot/session contracts with exact model, program,
worker and numeric-profile identities; copy borrowed outputs only for reports.
Numerical interpretation and process ownership remain in Runtime and the worker.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [Runtime precision](../../OSDIRuntime/PrecisionExecution/DESIGN.md) | depends on | Exact session and borrowed output | Sole process owner | No ordinary fallback |
| [Point command](../PointEvaluation/DESIGN.md) | depends on | Internal request values, bindings and topology/report projections | Shared schema vocabulary | Point component owns the reused definitions |

## Architecture

```text
precision --request -> exact model/snapshot -> qualified worker session
 -> scoped result -> report validation/save/reload -> explicit cleanup outcome
```

## Contracts and Invariants

Existing precision schemas 1/2, outward bounds, explicit budgets and bit patterns
are unchanged. Failed work publishes no successful report. Numerical facts retain
their model-local qualification scope.

## Failure, Concurrency, and Constraints

The command returns a success flag; only the composition root exits. Worker
cancellation, deadlines and cleanup failures remain observable in typed reports.

## Verification and Change Impact

`Tools/OSDIPrecisionVerification/verify_cli.py` exercises snapshots, real worker
requests, persistence, invalid inputs, exact-profile mismatch and cleanup. Source
moves require freshly built worker/profile identities.

## Explicit Precision Request

`osdi precision --request <json> --output <json>` selects schema1/profile
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

The same `osdi precision --request` host command accepts the separate schema-2 profile
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
