# Constitutive Identity Runtime

## Purpose and Scope

Parent: [OSDIRuntime](../DESIGN.md). No children. This component owns protocol-first
preparation/evaluation and synchronous nonescapable views of checked F/Q
identities. The existing PrecisionExecution session remains the sole owner.
Execution is Native only; portable contract values remain platform independent.

## Responsibilities and Boundaries

Preparation accepts the exact program, worker artifact access, numeric profile,
process options, cover limits and identity limits. Evaluation accepts all four
original coordinate planes, both time contexts and precision bits. Runtime
exposes a result only after the [parent C boundary](../../COSDI/PrecisionProcess/ConstitutiveIdentities/DESIGN.md)
accepts the complete response; it neither reconstructs the source proof nor
promotes it to rank, root, flow or history authority.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [OSDIRuntime](../DESIGN.md) | parent | Artifact and library ownership | Module composition | No parallel session owner |
| [PrecisionExecution](../PrecisionExecution/DESIGN.md) | depends on | Noncopyable session, handle isolation and cleanup | Existing lifecycle owner | Same session across attempts |
| [Portable values](../../OSDIContracts/ConstitutiveIdentities/DESIGN.md) | depends on | Kind and immutable capability limits | Public input vocabulary | No capability defaults |
| [Parent C boundary](../../COSDI/PrecisionProcess/ConstitutiveIdentities/DESIGN.md) | depends on | Fully validated borrowed result | Native process bridge | Wire checks do not replace source checks |
| [Source algorithm](../../COSDIPrecision/ConstitutiveIdentities/DESIGN.md) | semantic reference | Exact source-linear proof scope | Owns identity meaning | No nonlinear-rank or root promotion |

## Architecture

```text
preparation protocol -> existing PrecisionExecution session -> parent C boundary
four planes + contexts -> synchronous evaluation -> checked Span-backed view -> body
```

## Contracts and Invariants

`OSDIPrecisionConstitutiveSessionPreparing` and
`OSDIPrecisionConstitutiveEvaluating` are `Sendable` protocol boundaries implemented
by the existing native preparer/evaluator. A session fixes the identity operation
and limits at preparation. Wrong operation or coordinate dimensions refuse before
submission; no partial or failed result calls the body.

`OSDIPrecisionConstitutiveEvaluationView` exposes the checked cover and indexed
claims. Each claim exposes every original raw-row coefficient through
`withCoefficient`; each dyadic view borrows its exact numerator words. Views are
`~Escapable`, use `Span`, and are available only inside synchronous callbacks.
Their index accessors require in-range indices. Wire encoding and canonicality
are owned by the linked C/worker boundaries, not redefined here.

## State, Ownership, and Lifecycle

The noncopyable session retains artifact leases, staging directory and the
Mutex-protected native handle under PrecisionExecution's existing lifecycle.
Result storage belongs to that session and expires on the next attempt or close.
No pointer crosses `await` or escapes a borrow. No callback, I/O or child wait runs
under Mutex; no target-specific synchronization or `Sendable` weakening is added.

## Failure, Concurrency, and Constraints

`OSDIPrecisionConstitutiveError` preserves runtime/cover failures, identity
capacity/coefficient/invalid-proof failures, and a primary failure paired with
cleanup failure. Submitted failure invalidates the result and closes/reaps the
same child. Failed cleanup retains the retryable cleanup owner. Cumulative quotas,
deadlines and cancellation remain those of the existing session and C boundary.

## Verification and Change Impact

[Public identity tests](../../../Tests/OSDIEvaluationTests/OSDIPrecisionConstitutiveTests.swift)
own full-row results, borrowed address reuse, repeated requests/cumulative quotas,
wrong operation, limit refusal, cancellation and close. Existing point/range/chart/
cover tests remain compatibility evidence.

[Actual-model transport tests](../../../Tests/OSDIEvaluationTests/OSDIPrecisionConstitutiveActualModelTests.swift)
own the four-family public join: verify the original capture manifest and d104
cover receipts, load each actual model with its original request, prepare real
topology, snapshot the same source/setup bytes, and submit the original singleton
coordinate/time/precision bits through protocol 5. Every dense borrowed coefficient
is compared with the independent C constructor/checker receipt. Repeated-session
work refusal and close are also checked. Qualification covers only those four
original one-terminal captures, not arbitrary boxes, roots or histories.

Historical producer digests remain historical evidence. A retired BULK source is
available only through an explicit test relocation with the original manifest SHA;
the retirement archive does not reenter package dependencies or product models.
Changes to borrowed layout, errors or preparation must recheck this public join
and the linked C boundary, without expanding its qualification scope.

## Conditional Preparation and Binding

Unreleased protocol 5 now requires an explicit coordinateQuotient argument at its
preparation requirement. Existing callers migrate to an explicit identity table.
Runtime revalidates that value against program.topology, including complete
unknown/residual natures and units. It retains the immutable condition in the same
session. Every result and claim carries a `OSDIPrecisionConstitutiveBinding` joining
source ChartBinding and this condition. Copies used by upper consumers must retain
that scope; comparing only cover.binding never authorizes conditional identities.
The cover remains the unchanged original-box cover. Empty coordinate conditions
have a typed failure. No alternate session, synchronization or cleanup path is
introduced. Version 5 is unpublished; the changed header rejects old version-5
workers without fallback, while versions 1–4 remain unchanged.

The [public quotient tests](../../../Tests/OSDIEvaluationTests/OSDIPrecisionConstitutiveQuotientTests.swift)
own projected probe/ground tests, distinct-flow and mixed-nature refusal, wrong-map
binding, empty intersections, cancellation and cumulative-work tests own this
extension. Existing synthetic and actual-model tests pass explicit identity
conditions and preserve their original source/box/precision inputs. The source
algorithm owns the precise D-intersect-E meaning and full-leaf obligations.
