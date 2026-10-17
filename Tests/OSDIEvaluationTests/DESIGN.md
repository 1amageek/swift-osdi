# OSDI Point Evaluation Qualification

## Purpose and Scope

Own S2 native behavioral evidence for preparation and raw F/Q/G/C point evaluation.
Parent: [swift-osdi](../../DESIGN.md). Children: [fixtures](Fixtures/DESIGN.md)
and [support](FixtureSupport/DESIGN.md). This target supplies no circuit solver or
claim of complete compact-model physical qualification.

## Responsibilities and Boundaries

Compare the public Swift API and CLI with a separately compiled direct-ABI C oracle.
The oracle includes only the pinned OSDI header and never links COSDI or imports the
production mapper. Every BSIM observation uses the same selected full-residuals artifact, with its
explicit `OSDI_BSIM_SHA256` checked before loading. The prior `6eced9dd` artifact is
retained only as a failed baseline, never full-residual qualification. Previous raw
references are input seeds only. Every qualified generated VA model is compiled
with the explicit `--full-residuals` profile. Fixed flags request F/Q/G/C, operating
point, DC and STATIC at time zero with limiting disabled.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [Runtime](../../Sources/OSDIRuntime/DESIGN.md) | verification target | Preparation, exclusive evaluator, scoped views | Actual native execution | No implicit state acceptance |
| [Contracts](../../Sources/OSDIContracts/ModelEvaluation/DESIGN.md) | verification target | Typed options, topology, parameters | Portable boundary | Raw descriptor order |
| [Fixtures](Fixtures/DESIGN.md) | child | Actual Verilog-A programs | Analytic and feature evidence | Compiler syntax must be exercised |
| [Support](FixtureSupport/DESIGN.md) | child | Isolated serialized capture | Immutable independent references | No S1 fixture sharing |
| [Oracle](../../Tools/OSDIEvaluationVerification/DESIGN.md) | depends on | Direct ABI capture and finite differences | Independent expected values | Same exact artifact and flags |

## Architecture

```text
pinned native artifact + explicit requests -> independent C ABI process -> frozen JSON
same artifact + same requests -> artifact-backed public API / CLI -> compare all raw values
analytic RC / full-active finite differences -> independent Jacobian discrimination
```

## Contracts and Invariants

Nineteen actual-model input points include nonzero BSIM N1/N2 and a small internal
auxiliary whose F/Q coefficients depend on physical bias. All original 608 finite-
difference checks are retained; the added points bring the total to 1204. A separate
default-profile compilation of that same auxiliary source must reproduce the old
pruning failure and is excluded from qualified positive points.

Raw N-element F/Q and row-major N*N G/C, descriptor node order, dense collapse map,
parameter values/types/scopes/aliases/given flags and temperature are compared.
Topology inputs use the independent oracle's active ordering, including all surviving
internal unknowns. Collapsed finite differences perturb only alias-consistent active
coordinates and compare independent incidence contractions. Final Jacobian counts
are not upper bounds for advertised pre-optimization capacities.

Prepared owners block library close; views cannot escape; close and repeated
point evaluation do not silently accept candidate state. Wrong dimensions, types,
scopes, duplicate aliases, nonfinite values, native initialization failures and
resource constraints must remain explicit failures.

A held prepared owner keeps concurrent metadata reads valid while every close and
second preparation attempt refuses with `libraryInUse`. The evaluation callback
reenters those same operations synchronously, proving client code executes outside
the owner mutex. Explicit model close releases admission for a new preparation.

## Failure, Concurrency, and Constraints

One serialized suite and an actor-owned preparation task isolate S2 fixture resources.
Every compiler/oracle child has a thirty-second bound. Setup is measured before the
full capture and all source/compiler/binary/request identities are recorded. Required
compiler/artifact inputs come from explicit environment paths; missing inputs fail
setup rather than skip or fetch substitutes.

## Verification and Change Impact

Root owns Native, sanitizer and CLI integration execution. Analytic and finite-
difference checks supplement exact-artifact independent comparisons; they do not
establish interval inclusion or arbitrary-model accuracy. Changes to flags, node
mapping, parameter scope, string ownership or native lifetime invalidate associated
captures and regressions.

## S4 Dynamic Evidence

Dynamic tests distinguish mode/time/IC, white/flicker PSD, raw ground endpoints,
fresh bound values, invalid context/PSD/bounds and explicit state retry. A generated
analytic source and negative C protocol images test actual callbacks. Four exact
model artifacts receive fresh AC/TRAN/noise API comparisons and CLI round trips.
Existing S1/S2 counts and defaults remain regression requirements.

Reactive-row metadata tests own ABI sentinel/present discrimination and conservative
manual defaults. Protocol Q depends on absolute time and accepted state while C is
zero, so sampled derivative inference cannot satisfy the metadata contract.

## Precision Execution

`OSDIPrecisionTests` owns scoped shared-slab views, exact worker acquisition,
helper failure/cancellation and close behavior. `OSDI_PRECISION_TRANSPORT_WORKER`
points to the C transport oracle; it does not qualify arithmetic.
`OSDI_PRECISION_MODEL`, `OSDI_PRECISION_WORKER` and
`OSDI_PRECISION_SEMANTIC_SHA256` and `OSDI_PRECISION_NUMERIC_PROFILE_SHA256` bind the actual compiler compound-default fixture
and statically linked numeric helper. Tests discriminate binary64-folded defaults,
sub-ULP coordinates, scoped explicit primitive facts and original ordinary-model
usability after precision failure. Independent interval arithmetic and all-family
qualification remain owned by the numeric/compiler and composition tests.
