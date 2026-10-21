# Independent OSDI Point Oracle

## Purpose and Scope

Own direct-ABI observation and bounded finite-difference qualification for S2.
Parent: [swift-osdi](../../DESIGN.md). Input fixtures belong to the
[S2 test target](../../Tests/OSDIEvaluationTests/Fixtures/DESIGN.md).

## Responsibilities and Boundaries

Compile an independent C program against the unchanged pinned OSDI header. It loads
and calls the selected native library directly, without COSDI or Swift mapper code.
Capture raw node residual/charge offsets and pointer-stamped dense Jacobians, then
record model metadata, effective parameters, setup topology and flags. Build actual
VA fixtures with an explicit compiler and pin the existing unchanged BSIM artifact.
`compile_bsim.py` generates that artifact once from the unchanged 107.2.1 source,
explicit include directory and selected compiler using `--full-residuals`. Its
receipt binds the input snapshots, compiler build receipt, ABI header and output.
The target triple is explicit (`--target`, retaining the original macOS arm64
default). `prepare_soi101.py` requires the expected generated image SHA-256 as an
input, independently of its unchanged official source identity; a newly built
image is never silently substituted for the historic qualified binary.
`verify_families.py --library-ids` selects explicit manifest library entries and
records both selected and excluded IDs; its default still requires the four
baseline families and includes SOI 101 when advertised. Every selected library
must have actual point cases and an exact artifact identity.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [S2 tests](../../Tests/OSDIEvaluationTests/DESIGN.md) | used by | Exact independent references | Behavioral authority | No universal numeric certification |
| [Fixtures](../../Tests/OSDIEvaluationTests/Fixtures/DESIGN.md) | depends on | Actual source programs | Analytic discrimination | Compiler-backed |

## Architecture

```text
explicit compiler + pinned header + VA -> native fixture artifacts
explicit exact BSIM artifact + request -> independent C direct ABI -> raw JSON
raw aliases -> independent incidence algebra -> collapsed-coordinate FD comparison
source/compiler/header/artifact/request hashes + command receipts -> manifest
```

## Contracts and Invariants

Fixed evaluation is F/Q/G/C+OP+DC+STATIC, time zero and limiting off. All scalar
parameters use actual exported type/scope/alias metadata. The selected full-residuals BSIM SHA-256 is supplied explicitly and checked against
the exact artifact before execution; the prior 6eced9dd artifact is rejected for this
qualification. Compiler provenance is archived with its hash. Qualified generated
VA models use `--full-residuals`; one separately labeled default-profile auxiliary
artifact must reproduce the historical pruning counterexample.
Ground and aliases derive from generated setup collapse flags; no production map
is reused. Finite differences use two step sizes and all independent active
coordinates for no-collapse points, supplementing exact-output comparisons. The
original 608 checks remain unchanged and new nonzero-auxiliary BSIM/analytic points
add 596 checks, for 1204 total. Neither failed coordinates nor tolerances are
modified to accommodate a compiler outcome.

## Failure, Concurrency, and Constraints

Each child has a thirty-second timeout; a failed or timed-out process tree is
terminated. Manifest generation records every finite-difference outcome, including
failed coordinates, without changing tolerance or dropping columns. A failed
qualification remains explicitly failed in the manifest and the corresponding
Swift qualification test; successful evidence generation is not derivative success. Preparation uses a fresh directory
and records compiler/source/header/binary SHA-256 plus argv/stdout/stderr/exit/time.
Native code is trusted, and this is not a malicious-pointer sandbox.

## Verification and Change Impact

Cold capture measures cost before full regression. Wrong artifact digest, unknown
parameters, duplicate alias assignments and native failure flags remain explicit.
Numeric tolerances for finite differences are tied to measured stencil disagreement
and floating roundoff; these are ordinary point checks, not interval certificates.

S4 direct-ABI capture accepts explicit mode/time/frequency and emits raw independent
noise topology/densities and the model timestep bound. It does not call COSDI and
keeps state/integration ownership outside the oracle point operation.

The existing bounded protocol builder also emits reactive-offset absence and
time/state-charge variants for the [metadata tests](../../Tests/OSDIEvaluationTests/DESIGN.md).
These do not alter the nineteen numeric model points or finite-difference matrix.

## Real-Family CLI Matrix

`verify_families.py` executes the actual `osdi evaluate` process for every real-device
case in an explicit manifest and the ordinary six-context matrix. Each request,
report, invocation, diagnostics and exact tool/model identity is retained. A separate
read/decode checks saved bit patterns and exact request binding; all raw values,
PSD, state, flags, bounds, parameters and collapse mapping are compared with the
direct-ABI executable. Both child paths have external timeouts. Completion of this
matrix establishes the consumer of those exact artifacts, not latest-source or
foundry physical qualification.

Ordinary schema 2 persists JSON number arrays: the Swift encoder canonicalizes
signed zero. CLI comparison requires exact binary64 bits for every finite nonzero
value, numerical equality for zeros, and records every zero-sign canonicalization.
This does not weaken the API bit comparison or precision reports, which carry
explicit bit patterns. The tool does not claim ordinary JSON preserves zero signs.
