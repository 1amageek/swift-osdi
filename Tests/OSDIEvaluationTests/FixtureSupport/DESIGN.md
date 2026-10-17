# Point Evaluation Fixture Support

## Purpose and Scope

Own one actor-serialized S2 fixture build/capture and immutable JSON decoding.
Parent: [S2 tests](../DESIGN.md). S1 caches, markers and temporary directories remain
independent.

## Responsibilities and Boundaries

Require explicit `OSDI_OPENVAF_COMPILER` and `OSDI_BSIM_LIBRARY` paths, plus
`OSDI_BSIM_SHA256`. Optional `OSDI_COMPILER_PROFILE_RECEIPT` binds the compiler
source/patch/build receipt when supplied by the qualification owner. Build and
capture into a fresh UUID directory, retain diagnostics and expose only immutable
paths/reference records to tests. The selected BSIM digest is checked before any
native model execution. Missing tools, wrong identity and capture failures are
errors, never skipped or replaced results.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [S2 tests](../DESIGN.md) | parent | Shared setup | One cache owner | Serialized suite |
| [Oracle builder](../../../Tools/OSDIEvaluationVerification/DESIGN.md) | depends on | Bounded compilation, capture, manifest | Independent references | No production mapper reuse |

## Architecture

```text
concurrent setup requests -> actor -> one preparation Task -> fresh directory
 -> bounded independent build/capture -> immutable manifest/reference records
```

## Contracts and Invariants

Source paths are derived from the test file location; external inputs are explicit.
Compiler commands and cleanup are bounded. Tests read reference-generated active
ordering rather than constructing expected topology from production outputs.

## Verification and Change Impact

Root executes tests after fixture capture cost is measured. Changing the builder
schema or scoped runtime interface requires this decoder and comparisons to change
with it; stale references fail identity checks.
