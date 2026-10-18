# ProgramEvaluation

## Purpose and Scope
Validate and execute the compiler-owned stateless setup/evaluation program. Parent: [COSDIPrecision](../DESIGN.md). No children.

## Responsibilities and Boundaries
Each endpoint executes all three stages, simultaneous phi transfers and explicit scoped facts. Compiler-proved preparation-only collapse guards may be serialized in evaluation; after each complete three-stage endpoint, their equivalence must match the supplied map before publication. Original derivative roots supply inexact Newton midpoints.

## Related Designs
| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [COSDIPrecision](../DESIGN.md) | parent | Isolated precision execution | Owns composition and resource contract | Failure never changes accepted circuit state |

## Architecture
```text
Program/setup bytes -> validated SSA/CFG -> candidate/reference pipelines -> F and delta-Q
```

## Contracts and Invariants
Each endpoint executes all three stages, simultaneous phi transfers and explicit scoped facts. Compiler-proved preparation-only collapse guards may be serialized in evaluation; after each complete three-stage endpoint, their equivalence must match the supplied map before publication. Original derivative roots supply inexact Newton midpoints.

Expression identity is local to one endpoint and reset before the next pipeline. A fixed arena-owned interning table uses complete typed keys: exact singleton MPFR values, exact integer/Boolean/string values, or supported opcode plus already-proved operand identities. Hash matches alone never prove equality, and overlapping/equal interval endpoints never prove expression identity. Definite SELECT/phi/root copies preserve the selected token. After operand initialization and successful domain checks, subtraction of the same finite expression is exact zero and comparisons of proved identical expressions are definite. Table capacity, stored singleton significands and collision probes are bounded; capacity exhaustion terminates the isolated worker as a resource failure, and all probes/key work consume the request work budget. Tokens never cross endpoints or programs.

Arithmetic domain, nonfinite and indeterminate results carry a failure status and originating instruction in fixed arena-owned value storage. Pure instructions propagate invalid operands without reading their numeric payload. Definite SELECT/phi/root copies preserve only the selected value and status. F/Q/G/C, collapse and bound-step roots demand valid values; resolved parameter roots may propagate invalidity until a downstream observable demands them. Unknown opcodes, missing primitive facts, model-status instructions, work/resource failures and cancellation remain immediate failures.

A poisoned CFG predicate can defer control only through a worker-proved finite acyclic pure reconvergent region. Cold predecessor counts and fixed block-count color/node/DFS storage support a narrow recognizer: candidate joins occur along an arm's linear jump chain; bounded DFS proves every path from both arms reaches the same in-function join, with no return, loop or MODEL_STATUS. A charged predecessor scan rejects external region entries. After proof, all region definitions and join phi destinations become poisoned, then execution resumes after join phi transfers. No branch direction is guessed, no observable effect is discarded, and unsupported regions fail explicitly. Later definite selection may discard this invalid computation; any demanded invalid value still fails.

## Verification and Change Impact
Real IPC fixtures cover nonlinear sub-ULP values, setup/default precedence, phi cycles, malformed programs, domain/branch refusal and work exhaustion. Actual emitted model programs remain required. Tests: [verification tools](../../../Tools/OSDIPrecisionVerification/DESIGN.md). Changes require the parent contract and affected runtime/compiler boundaries to remain consistent.

## Smooth-Box Execution

Version 2 implements the [package range boundary](../../../DESIGN.md#causal-range-boundary)
with one complete three-stage pipeline over lower/upper normalized twofold
coordinates and absolute times. The prepared raw-to-active map remains fixed:
aliased raw endpoints have identical coordinates and their difference is exactly
zero. Distinct original raw endpoints nevertheless remain source-active for
regularity, including aliases and ground mappings: the published G/C matrix is
with respect to raw coordinates, whose classical partials need a smooth source
neighborhood. Only an identical raw endpoint proves an inactive difference.
Every other value difference is enclosed with directed endpoint subtraction.
Topology is rechecked before any F/Q/G/C result is published. Native state remains
unsupported.

Cold admission allocates range-only activity tracking for registers, phi staging
and roots in the existing fixed arena. Coordinate and time leaves remain active
even for singleton boxes. Copies preserve activity; constant setup facts remain
inactive. Active numerical singleton identities never establish source equality:
only the same SSA operand may cancel or resolve comparisons without a strict
margin. Version 1 neither allocates nor executes this tracking.

Range arithmetic propagates `NONSMOOTH` as a poisoned value, using the existing
SELECT/phi/pure-reconvergence demand rules. Every demanded F/Q/G/C dependency,
including its control predicates, must be regular. Unknown comparison sides,
ties, integer-conversion boundaries and elementary domain faces are refused;
branches are never guessed or hulled. Independent active values are not merged
because they have equal endpoints. Regularity metadata is conservative and may
refuse a mathematically smooth expression when its source proof is unavailable.

The range result contains outward absolute F/Q and same-source spatial G/C bounds,
not DeltaQ or derivative midpoints. Maximum-step bounds use the conservative lower
bound over the complete box. The complete time interval participates in every
stage; this local constitutive result does not certify a trajectory or history.
All range work, metadata clearing and projection use the existing cumulative
request budget; storage exhaustion and cancellation expose no partial success.
