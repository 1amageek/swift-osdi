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
