# Precision Evaluation

## Purpose and Scope

Parent: [OSDIContracts](../DESIGN.md). No children. Own the separate optional precision boundary; ordinary evaluation remains unchanged.

## Responsibilities and Boundaries

Own portable validated precision options, contexts, and failure facts. No arithmetic, process, file or solver ownership.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [Precision Process](../../COSDI/PrecisionProcess/DESIGN.md) | coordinates with | Versioned bounded IPC | Exact twofold inputs and enclosed outputs | Failure publishes no view |

## Architecture

```text
immutable program/setup + exact worker -> exclusive precision session
 -> four input spans + endpoint contexts -> bounded helper -> scoped F/delta-Q enclosure + G/C midpoint spans
```

## Contracts and Invariants

Status: parent D1 cross-owner contract frozen; implementation and behavioral qualification follow this contract.

## S4D Precision Boundary — D1 Contract

The [native extension design](../../COSDI/PrecisionProcess/DESIGN.md#s4d-precision-execution--d1-contract)
owns compiler/callback authority. The [runtime design](../../OSDIRuntime/PrecisionExecution/DESIGN.md)
owns preparation, lifetime and failure propagation. Parent coordination is the
CoreSpice S4D contract, not a second task ledger.

### Mathematical contract

A point represents the real sum of finite, normalized binary64 components
`x = high + low` in the prepared active order. Both candidate and immutable accepted
reference have their own components and explicit mode/time. The reference denotes
the point whose Q was accepted, including its original evaluation context; it is
not recomputed under the candidate context. Both endpoints belong to the same
prepared descriptor, parameter/temperature setup and topology.

The compiler profile defines one source-faithful DAE function from retained setup
and evaluation SSA, treating primitive frontend-decoded binary64/integer literals and admitted
parameter inputs as exact leaves, and compound operations according to the
declared supported-opcode semantics. Compound expressions must never be replaced
by previously binary64-folded constants or caches.
Derived setup values must be evaluated within that precision profile; existing
rounded binary64 caches cannot silently become exact source leaves.

Arithmetic validity is required for the selected source-observable outputs and
their necessary control dependencies. An unused undefined temporary does not
invalidate an otherwise finite selected output. The bounded proof and refusal
rules for deferring invalid pure values/control belong to
[Program Evaluation](../../OSDIPrecisionWorker/ProgramEvaluation/DESIGN.md); effects,
required input facts and resource/cancellation failures cannot be discarded.

For every raw row the result provides normalized high/low centers and nonnegative finite absolute
radii satisfying `F(candidate) in [fHigh + fLow - fRadius, fHigh + fLow + fRadius]` and
`Q(candidate) - Q(reference) in [deltaQHigh + deltaQLow - deltaQRadius,
deltaQHigh + deltaQLow + deltaQRadius]`.
Enclosures include input-component formation, setup, nonlinear operations, branch
selection, both endpoint evaluations, subtraction and conversion to binary64 output.
The source result at `high` or its first-order correction is not the function at
`high + low` unless the enclosing proof covers the difference.

G/C values used for Newton are explicitly approximate linearizations. Selected v1 semantics are finite interval midpoints of the original derivative SSA
evaluated at the same compensated candidate. G/C centers are binary64 inexact
Newton values; they are not a claim of exact derivative representation and carry no
acceptance authority. A high-only native linearization is not selected. Acceptance is owned by the
consumer's unchanged physical residual gate, including propagated enclosure radii.

An affine shortcut is optional and unnecessary for the first correct implementation.
It requires compiler proof of the entire row `Q = a*x + b`, including invariant
coefficients/intercept across endpoints and absence of dynamic data/control effects.
`JACOBIAN_ENTRY_REACT_CONST` alone does not provide that proof. General interval
subtraction at sufficient precision preserves nonlinear Q instead of approximating
it with a tangent. This contract does not promise success at arbitrary timesteps:
insufficient certified precision is an explicit failure, not relaxed tolerance.

### Public values and operations

| Declaration | Owned meaning |
|---|---|
| `OSDIPrecisionOptions` | Explicit precision range, slab/arena/program byte limits, work cap, startup/evaluation/cleanup deadlines and cancellation poll bound; no inferred fallback |
| `OSDIPrecisionSession` | Noncopyable Sendable owner of cold-copied program, admitted worker, slabs and child; explicit fallible close |
| `OSDIPendingPrecisionCleanup` | Cleanup-only retained owner after bounded reap/release failure; no evaluation authority |
| `OSDIPrecisionContext` | Supported phase plus finite normalized high/low absolute seconds |
| `OSDIPrecisionEvaluationView` | Scoped F/delta-Q high/low/radius arrays, G/C midpoint arrays, precision/work/sequence and fresh bound facts |
| `OSDIPrecisionEvaluating` | Synchronous borrowing evaluation requirement over the exclusive precision session; bounded worker wait and typed failure before callback |

The selected v1 precision capability requires `stateCount == 0`.
Four targeted real families satisfy this assumption. The ordinary S4 state contract
continues unchanged. Stateful precision must be explicitly unsupported until state
representation, error propagation and accepted-history ownership also preserve its
precision; rounding a low-sensitive next state to binary64 is not permitted.

`OSDIPrecisionSessionPreparing` admits the exact worker through ArtifactAccessing,
copies the same image's compiler program while its library lease is valid, and
prepares a session from the same descriptor, typed preparation inputs and prepared
topology. Cold admission may be asynchronous; precision evaluation is synchronous
and its borrowed spans never cross await. Program/setup/profile identities become
immutable preparation provenance. Existing ordinary model creation/close APIs do
not change.

The precision lane initially covers explicit transient initialization and transient
endpoints. Ordinary DC/AC/noise calls remain the existing binary64 profile. Expanding
supported phases requires model-owned guarantees for every newly consumed output.
No result implicitly changes accepted coordinates, Q/Iq history or native state.
Endpoint absolute seconds also use normalized high/low components in the precision
lane. The existing ordinary context retains its binary64 time representation.

### Failure and evidence

Unavailable/mismatched profile, unsupported state/opcode, incompatible endpoint,
nonfinite input/output, demanded unsupported domain or ambiguous branch, workspace exhaustion,
precision exhaustion and work exhaustion are distinct typed failures. No partial
view or candidate is published. A precision failure is not a normal step-rejection
signal: shrinking a timestep may amplify uncertainty and must not silently retry it.

Evidence must distinguish exact-zero/affine, nonlinear low-sensitive F/Q, time/context
changes, branch/domain refusal, exhausted budgets and unchanged accepted references.
Comparison must use an independent higher-precision source-level authority rather
than only comparing against the same backend. Existing binary64 tests and measured
allocation counts retain their original scope. The parent owns all-four-model final
API/CLI behavior and unchanged-tolerance physical acceptance.

## Verification and Change Impact

Focused real worker transport, failure, scoped view and cleanup tests own this boundary. Numeric and compiler correctness remain separately verified by their owners.

## Causal Range Contract

The [system range boundary](../../../DESIGN.md#causal-range-boundary) fixes the
smooth-neighborhood requirement and the limits of this constitutive certificate.
The point and compensated-point operations above remain version 1 operations.
Range is a separate version 2 operation over one immutable program/setup/topology,
using the existing precision options and exact source/worker/profile provenance.

Lower and upper coordinate endpoints each use finite normalized high/low pairs
in active-unknown order. Their real sums define a complete closed coordinate box;
the lower endpoint must not exceed the upper endpoint. Lower and upper
`OSDIPrecisionContext` values use the same mode and define a closed absolute-time
box without collapsing their components to binary64. Stateful programs remain
unsupported. The process/interpreter owns exact endpoint ordering, regularity and
outward projection; portable contracts do not reproduce interval arithmetic.

| Public declaration | Owned meaning |
|---|---|
| `OSDIPrecisionRangeSessionPreparing.prepareRange` | Admit the same program/worker/source/profile/options into the existing exclusive session with immutable range operation |
| `OSDIPrecisionRangeEvaluating.evaluateRange` | Synchronously borrow lower/upper coordinate spans and contexts; publish one complete range view or typed failure |
| `OSDIPrecisionRangeEvaluationView` | Scoped absolute `fLower/fUpper/qLower/qUpper` and row-major spatial `gLower/gUpper/cLower/cUpper` outward bounds, precision/work/sequence/fresh-bound facts and `programHasTimeInput` |

The F/Q endpoints enclose their absolute same-source functions over every point in
the box. G/C endpoints enclose the corresponding same-source spatial derivatives;
they are neither midpoints nor explicit-time partial derivatives. No delta-Q,
solver step, accepted state, flow, or entire-history result is implied. Conservative
`programHasTimeInput` reports whether the retained program contains any explicit
time input, allowing upper consumers to refuse time-dependent programs when their
own authority requires it. It is not a proof of time independence for other inputs.

Wrong-operation use fails before transport or callback and preserves the session
for its admitted operation. Demanded nonsmooth dependencies produce a typed
regularity failure before any view; invalid geometry, unsupported state, exhausted
budgets, cancellation, protocol failures and bounded cleanup retain the existing
failure/ownership policy. A range failure does not silently retry a point operation.
`OSDIPrecisionIssue.Code.nonsmooth` is raw value 12, translated by the range
process from worker regularity status 10. Existing process issue values, including
`io` at 10, and all point error mappings remain unchanged.

Verification owns closed boxes touching/crossing switch/domain faces, nonlinear
closed-form F/Q/G/C bounds, exact twofold ordering, time boxes, operation mismatch,
regularity failure mapping, atomic refusal, cleanup and unchanged point behavior.

## Conditional Chart Contract

Protocol 3 adds separate chart preparation/evaluation protocols and borrowed result views. It preserves the existing point/range declarations and error raw values. Caller-owned `OSDIPrecisionChartLimits` bounds session-cumulative source steps, tape entries and output guard records; the existing options bound mapping bytes, arena bytes, cumulative work and process deadlines. Limits are explicit policy, not guessed defaults.

| Value | Authority |
|---|---|
| Chart binding | Exact model artifact, descriptor, program/setup/semantic digests and complete prepared raw topology/active order |
| Decision tape | Caller-owned finite decisions, source site/kind/choice/ordinal, bound to that immutable program |
| Record result | Definite source decisions proposed for explicit caller retention |
| Exact graph formula | Bounds conditional on the complete actual source guard conjunction |
| Formula closure | Finite extension of the selected formula, separately tagged from actual equality values |

Eight F/Q/G/C endpoint planes retain range coordinate order, but G/C are retained source AD-root formula bounds. They do not certify seam differentiability or a circuit quotient derivative. Guard records retain strict/non-strict/equality opcode, operand register/type/integer/real bounds, occurrence order and boundary-extension tag. Public guard collection indexing returns one portable guard value; no automatic collection copy or persistent guard certificate is produced. Consumers explicitly retain decisions if needed.

Binding/dimension/mode/operation/tape-count errors rejected before transport preserve the admitted session. Submitted failures publish no result and close that same child. `OSDIPrecisionChartError` wraps existing runtime errors and distinguishes tape mismatch, guard capacity and step exhaustion; cleanup wraps the complete primary chart error plus the complete existing cleanup error. Undefined formulas and removable limits without source identity authority remain explicit numerical refusals. No fallback or root/flow claim is admitted.

Verification covers actual worker record/replay, equality versus closure, wrong binding/version/tape/capacity, cumulative quotas, cancellation, lease independence, repeated borrowed addresses and explicit close, plus unchanged point/range regressions.

Chart operand kinds `negativePrimitiveInfinity` and `positivePrimitiveInfinity` explicitly retain source-bound sentinels used in parameter validation. `sourcePrimitiveConstantIndex` points into the bound program's admitted constant section; copied/phi-selected sentinel values preserve that origin. Their lower/upper fields contain the matching infinity as tagged representation, not an unconstrained finite interval or a physical variable. Ordinary REAL guards still have finite endpoints; NaN, finite-value projection overflow and nonfinite output planes fail. Upper consumers must interpret the value kind before using endpoints. This adds no equality/closure/root authority.
