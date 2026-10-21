# ProgramEvaluation

## Purpose and Scope
Validate and execute the compiler-owned stateless setup/evaluation program. Parent: [OSDIPrecisionWorker](../DESIGN.md). No children.

## Responsibilities and Boundaries
Each endpoint executes all three stages, simultaneous phi transfers and explicit scoped facts. Kind 15 reads the exact requested-scope immutable given bit; existing kind 2 retains instance-or-model presence. Source guards, not merged resolved values, choose the original setup branch. Compiler-proved preparation-only collapse guards may be serialized in evaluation; after each complete three-stage endpoint, their equivalence must match the supplied map before publication. Original derivative roots supply inexact Newton midpoints.

Typed simulation-REAL input kinds 13/14 follow the
[format contract](../../COSDI/CompilerPrecision/DESIGN.md#additive-typed-simulation-real-bindings).
Their exact-name/type lookup is admitted once in the existing input index table;
all source stages use the same immutable setup facts. They neither change legacy
8/12 semantics nor choose the source fallback. No extra arena pool or operation
owner is introduced.

## Related Designs
| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [OSDIPrecisionWorker](../DESIGN.md) | parent | Isolated precision execution | Owns composition and resource contract | Failure never changes accepted circuit state |

## Architecture
```text
Program/setup bytes -> validated SSA/CFG -> candidate/reference pipelines -> F and delta-Q
```

## Contracts and Invariants
Each endpoint executes all three stages, simultaneous phi transfers and explicit scoped facts. Kind 15 reads the exact requested-scope immutable given bit; existing kind 2 retains instance-or-model presence. Source guards, not merged resolved values, choose the original setup branch. Compiler-proved preparation-only collapse guards may be serialized in evaluation; after each complete three-stage endpoint, their equivalence must match the supplied map before publication. Original derivative roots supply inexact Newton midpoints.

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

### Local Source Proofs

Version 2 may clear multiplication activity only after both operands and the
ordinary arithmetic result have passed all existing failure checks, and only
when one finite operand is an inactive exact singleton zero. An active singleton
zero, including a prepared raw alias difference, is not this proof. Neither
domain failures nor poisoned operands are discarded by multiplication by zero.

Range-only arena metadata records a fresh dynamic source token and one of three
narrow expression shapes: `x*x`, `x*x+c`, or `sqrt(x*x+c)`. The addition shape
requires an inactive finite exact singleton `c>0`. Definite copies preserve the
token and shape. A new computation receives a never-reused token; overflow is a
numeric invariant failure. Tokens are independent of numerical identity and reset
with the complete pipeline. A final `x+sqrt(x*x+c)` may request the arithmetic
positive-sum refinement only when its x token matches and the recorded x/c
registers still hold the recorded tokens. This rejects stale loop/phi values and
independent equal-valued operands. Only successfully evaluated operations produce
shape evidence; a shape never bypasses domain or poison propagation.

All metadata is cold-allocated, bounded by existing value/root/phi capacities,
and cleared/copied/created under the cumulative work budget. Version 1 executes
none of these rules and retains its arena admission and arithmetic work charges.
Verification must falsify active-zero annihilation, stale x/c provenance,
independent equal-valued x, nonpositive/active c, poison erasure and unbudgeted
proof work, in addition to checking actual outward positive-sum bounds.

## Internal Guarded Graph Extraction

This lower operation extracts an immutable, lossless graph view of an already
admitted version-1 program; it does not evaluate a branch or certify a smooth
chart, physical source flow, reset, trajectory or history. It changes no program,
wire or public Swift API. `GuardedGraph.h/.c` owns this internal operation.

```text
admitted exact program/setup + caller-owned bounded scratch
 -> validate complete definition/phi/predecessor incidence
 -> iterative reachability/DFS with both guarded edges and explicit cycles
 -> monotone backward root/control dependency closure
 -> immutable graph view or failure with no published view
```

The view borrows the original program, so comparisons retain their exact opcode
(including strict/non-strict/equality), operands, source register identities,
SELECT arms, simultaneous predecessor phi edges, stage/setup dependencies,
root identities, constants, raw coordinates and explicit MODEL_STATUS effects.
No binary64 value, midpoint, numerical equality or chosen branch collapses this
graph. A MODEL_STATUS terminates its instruction path; extraction retains its
exact immediate flags and the original instruction order. Reachability is
structural, following both guarded branches; a structurally reachable path may
have infeasible guard constraints and is never asserted to be a physical path.

Every definition maps to its constant/input/instruction/phi owner. A root-mask
closure conservatively includes all data dependencies, both SELECT/phi arms,
prior-stage root bindings and all branch conditions. Source effects remain in
the graph independently of whether their result is demanded by F/Q/G/C. Phi
admission checks every incoming CFG edge and rejects missing or extraneous phi
predecessors. Cycles are detected by iterative DFS and retained without any
unrolling, iteration count or no-Zeno claim. A later chart consumer must prove
finite unfolding/progress or explicitly handle cyclic source semantics.

The caller reserves block and register scratch before entry; the operation makes
no allocation or callback. Scratch capacities are checked before any writes and
the output view is assigned only on success. An attempt invalidates prior views
of the same scratch; scratch is not a published certificate. Program bytes and
scratch must remain immutable and live while the view is consumed, under the
existing exclusive program/session owner. No new shared state is introduced.

All traversal, validation, scratch initialization and fixed-point propagation
charge the existing `math` work owner without resetting its counter. Even cyclic
dependency closure has finite monotone state and is bounded by the same work
limit; exhaustion is terminal. Invalid geometry and failed incidence return
typed existing input/program status, and no capacity or failure returns partial
graph success. This internal cold operation has no independent cancellation or
deadline owner: future transport must use the existing bounded worker/process
boundary before exposing it publicly.

Matching C tests exercise exact comparison relations and complementary guarded
paths, SELECT/phi dependency closure, terminal effects, a retained loop,
cross-stage roots, malformed predecessor/definition incidence, insufficient
scratch and exact cumulative work exhaustion. A supplied real compiler
program/setup path exercises the same production admission/extractor and checks
that all admitted roots and source branch predicates survive extraction. These
are graph facts, not constitutive ranges or physical flow evidence.

## Internal Conditional Chart Formula Evaluation

The internal [ConstitutiveIdentities](../ConstitutiveIdentities/DESIGN.md) consumer
may install an exclusive synchronous source observer during a complete terminal
replay. Observations identify original constants, input bindings, simultaneous
selected phi transfers, executed instructions and output roots. They do not read
back a sampled value as an exact coefficient, change source evaluation/control,
or grant root/flow authority. The consumer resets its versioned expression graph
at pipeline entry and removes the observer on every exit. Observation failure
aborts the attempt with no published result.

`Chart.h` defines an internal operation on the same exclusively owned range
program. The existing interpreter, setup/defaults, source order, simultaneous
phi transfers, raw-coordinate derivatives and topology check remain authoritative.
It introduces no public Swift/wire operation or source equation implementation.

```text
exact program + closed coordinate/time box + finite source decision tape
 -> source-order comparison/cast guards, branch and SELECT occurrences
 -> same three-stage interpreter with conditional predicate choices
 -> outward selected-formula F/Q/G/C + complete guard conjunction
 -> conditional exact-graph or finite formula-closure view, or refusal
```

The tape matches every evaluated comparison/real-to-Boolean cast and every
consumed branch/SELECT by stage, block, instruction, event kind and global
occurrence ordinal. Strict/non-strict/equality relations retain their exact
opcode and Boolean choice. A comparison with valid operands but unresolved
interval truth may consume its proposed choice; exact-graph mode rejects a
definite incompatible choice. Formula-closure mode may retain an incompatible
real predicate only if its weak closure is not excluded by the operand bounds;
the guard explicitly records this boundary extension. This is not an existence
proof for that boundary or for a one-sided approach. Discrete parameter predicates
cannot use this extension. Branch/SELECT choices must agree with their already evaluated
Boolean. No invalid/domain-failed operand or guard is overridden. Existing
inactive poison remains poison until source-observable demand or a selected
consumer discards it. Unsupported primitive guards and singular formulas retain
their existing explicit refusal.

Each guard record snapshots its operand types, integer facts and outward real
endpoints, together with source register/site identity and dynamic ordinal.
The immutable program and complete tape prefix define the exact expression even
through source loops; overlapping interval bounds never prove guard truth.
All recorded guard conditions are conjoined. A successful conditional formula
does not assert this conjunction is nonempty, that a supplied family of tapes is
exhaustive, or that any physical source continuation exists.

The record mode proposes a tape only when every demanded guard is definite; it
never guesses a branch. Record output is separate from a checked conditional
tape. The exact-graph mode conditions the bounds on actual source guards. The
formula-closure mode is separately tagged: it bounds any finite one-sided limits
of this same branch formula along points satisfying those guards and approaching
the closed box. It does not replace exact equality-branch values or prove that
such approaching points exist. Non-comparison arithmetic retains the original
smooth-neighborhood checks on the whole box, so the selected formula has a finite
continuous extension there. Division by zero, sqrt/domain boundaries and
removable AD 0/0 without a checked source identity lemma still refuse. A closure
of several guard constraints is not asserted to equal the intersection of their
individual closures.

G/C are bounds on the retained source AD-root formulas under the same tape;
they do not prove differentiability of the complete piecewise graph at a seam,
quotient derivatives for aliased raw coordinates, or physical reconstruction.
Those upper consistency obligations remain separate from enclosing the roots.

The caller preadmits tape, guard and eight endpoint-plane capacities. These
buffers are exclusive scratch and may change on failure; only the final result
view grants access on success. Any attempt invalidates previous views of the same
scratch. All input/program/tape bytes and scratch remain live and immutable while
the successful borrowed view is consumed. No per-call allocation, callback or
new shared owner is introduced. A run-local chart pointer is installed only for
the synchronous call and cleared on every normal return, including errors.

Every source block/instruction consumes the explicit finite step limit and the
existing cumulative work budget; no counter is reset by this operation. Missing,
extra or wrong-site tape entries, bounded scratch exhaustion and step exhaustion
have distinct internal statuses. Existing process/session ownership supplies the
outer cancellation/deadline boundary; this internal call creates no alternate
timeout or cancellation authority. Future public transport must preserve that
same boundary before publication. Graph extraction and formula bounds alone
grant no reset/root-existence, source-flow, entire-history, tube or event facts.

### Chart Transport Composition

[WorkerProtocol](../WorkerProtocol/DESIGN.md#version-3-conditional-charts) now exposes the existing internal chart through separate protocol 3. Internal chart records remain native-only; fixed-width wire projection and session-cumulative step aggregation belong to WorkerProtocol. The interpreter's per-call finite step cap is the remaining session quota. Its work owner is never reset by this adaptation. No public smooth-box certificate is fabricated from a chart result.

### Exact Primitive Infinity Comparands

The admitted compiler ABI permits signed-infinity REAL constants for source parameter-bound predicates. Range metadata records the immutable constant-record index when that primitive is initialized in the existing constant pass. Successful copy, phi, SELECT and OPTBARRIER preserve that fact; a fresh arithmetic value or poison clears it. No whole-program scan or dynamic allocation occurs per guard. A guard may tag an exact primitive negative/positive infinity only when both MPFR endpoints have that sign and the retained source primitive matches. The constant index binds provenance to this exact program, including copied comparands. Finite MPFR values that project beyond binary64 remain numerical projection failures; NaN and unsupported infinite expressions do not gain primitive authority. Formula closure cannot override an incompatible comparison involving an exact infinity sentinel.

## Exact Decision Frontiers

[DecisionCover](../DecisionCover/DESIGN.md) consumes a separate internal exact-prefix replay operation. It uses the same admitted program/setup, three stages, topology and demand-validity rules. A missing ambiguous prefix decision returns a frontier; definite decisions are checked and appended. Real comparisons retain less/equal/greater and the original opcode; source Boolean casts retain sign/zero. A valid contradictory prefix can prove exclusion, while malformed/extra entries fail. Arithmetic cell events retain their exact primitive identity and boundaries. Undefined or unsupported arithmetic and incomplete loops never become excluded states. All replay and validation charge the existing cumulative work/step owner. This operation does not change chart 1–3 semantics or compiler ABI 1, and returns no physical authority.

Frontier diagnostic usage is separate from the proof result and records executed steps and event writes after every started attempt, including refusal. Cover accounting consumes this telemetry before returning failure; no partial terminal/frontier certificate is published.
