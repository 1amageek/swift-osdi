# Source Constitutive Linear Identities

## Purpose and Scope

Parent: [COSDIPrecision](../DESIGN.md). No children. This internal component
constructs candidate linear relations between every raw F or Q output and checks
them independently against the original admitted source program. It owns no
circuit equations, charge-coordinate choice, numerical rank decision, root,
continuation, physical reset, trajectory, or entire-history certificate.

## Responsibilities and Boundaries

The compiler root table, immutable setup, complete source execution and exact
decision-cover binding remain authoritative. An absent F/Q root means literal
zero; every raw row occurs in the identity inventory, including absent roots.
Reactive-storage metadata can prove absent storage, but an offset, a sampled zero,
a Jacobian rank, or equal interval endpoints never proves row dependence. Source
identities establish neither physical rank nor independent charge coordinates,
guard feasibility, root nonemptiness, smoothness, continuity, flow or history.
Circuit assembly and original-equation equivalence proofs remain consumer owned.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [COSDIPrecision](../DESIGN.md) | parent | Exclusive worker and fixed arena | Existing allocation/lifecycle owner | No independent deadline or cancellation owner |
| [ProgramEvaluation](../ProgramEvaluation/DESIGN.md) | depends on | Actual SSA/input/phi/root observations | Original source replay | Observation cannot change values or source decisions |
| [DecisionCover](../DecisionCover/DESIGN.md) | depends on | Independently checked complete tree and terminal replay | Whole-cover proof binding | One terminal does not establish a whole-cover identity |
| [CompilerPrecision](../../COSDI/CompilerPrecision/DESIGN.md) | depends on | Exact literal bits, F/Q replacement roots and full raw axes | Immutable source/setup authority | Native cached values are not source constants |
| [Wire projection](../WorkerProtocol/ConstitutiveIdentities/DESIGN.md) | used by | Checked claims and exclusive workspace | Protocol-5 adapter | Encoding/publication are adapter owned |
| [Runtime qualification](../../OSDIRuntime/ConstitutiveIdentities/DESIGN.md#verification-and-change-impact) | coordinates with | Public actual-model transport join | Separate qualification owner | Four original singleton captures only |

## Architecture

```text
validated complete cover + original program/setup/box/context
  -> terminal source replay + observed immutable SSA expression DAG
    -> bounded atom normalization -> fraction-free common-kernel intersection
  -> second terminal source replay + fresh observed DAG
    -> weighted original-root expansion -> exact zero check
  -> same claims checked on EVERY terminal, or no whole-cover result
```

## Contracts and Invariants

Coefficients use the existing GMP `mpq_t` implementation. They represent exact
dyadic real numbers, never MPFR midpoints, native caches, or sampled constants.
Finite primitive binary64 literals are imported exactly. Potential inputs (kind
10) are canonicalized from the exact immutable setup map: raw `index` minus raw
`aux`, where `NONE` or a raw coordinate mapped to `NONE` is exact ground zero.
Each actual active coordinate has one canonical atom within a fresh source replay.
Repeated/reversed probes therefore share the same signed coordinate form; equal
active endpoints cancel to literal zero even when their raw indices differ.
With the identity additional condition, distinct active coordinates remain
independent; explicit added equalities are scoped as specified below. No singleton or range numeric
value becomes a constant, and opaque nonlinear occurrences are not merged by this
rule. Copy, selected phi and SELECT preserve their actual source expression.
FNEG/FADD/FSUB and FMUL by a provably constant dyadic expression admit linear
expansion. Remaining operations are opaque atoms identified by the exact source
instruction and its operand versions.
Repeated execution with different operands cannot merge atoms. Undefined source
arithmetic is still refused by the original evaluator before any proof publishes.

Caller-supplied limits bound DAG nodes, sparse terms, claims and coefficient bits.
There is no guessed default coefficient-bit limit. Before exact add/multiply, the
component checks conservative bounds on GMP intermediate numerator/denominator
bits and charges the existing cumulative math owner. Overflow, capacity or work
exhaustion is a failure; it does not change an operation into an opaque atom.
GMP temporary storage remains bounded by the existing isolated worker arena.

Candidate search performs fraction-free elimination over the normalized F rows
and Q rows separately, intersecting every terminal constraint. It computes the
common kernel of the admitted source-linear representation, not a nonlinear
mathematical rank.
The checker does not trust the elimination, claimed basis, normalized output
matrix, or a supplied success flag: it replays the original source, traverses
the fresh expression DAG from each claimed weighted raw root, and checks an exact
zero residual. Unexpanded expressions remain conservative independent atoms.

A whole-cover claim must hold on every independently validated terminal, with
exact program/setup pointers, control, original input box and complete tree
incidence. The consumer cannot omit a terminal or move a claim to a different
trace/setup. Guard-local relations are not silently promoted to whole-cover
relations. These identities imply neither guard feasibility nor seam continuity.

## Runtime Flows

The caller reserves all C workspace before an attempt. Source observation is
installed only during synchronous terminal replay, reset before each pipeline,
and removed on success and failure. Existing source execution and full cover
checking run first. Candidate search is followed by a separate source replay and
checker pass before result publication. Failure invalidates the attempted result;
all caller-owned prior immutable input and accepted circuit state remain intact.

## State, Ownership, and Lifecycle

Program, setup, cover and input storage are immutable borrows. DAG/register/root
and exact-coefficient storage are exclusive caller workspace. Potential-input
canonicalization preadmits one `UInt32` cache slot per actual active coordinate;
the cache is reset on every fresh SSA replay, including the independent checker.
There is no cross-model, setup or trace cache. Reset, construction and membership
work use the existing cumulative math owner, and the cache consumes the same
fixed-workspace admission. Overflow and quota failures retain their refusal
semantics. GMP values are initialized once and cleared by the workspace owner;
no pointer escapes the
synchronous call. The observed DAG is valid only until the next attempt. Result
claims retain their own caller-owned coefficients and exact binding, not borrowed
register values. No shared mutable state, platform-specific lock bypass, process
or asynchronous callback is added.

## Failure, Concurrency, and Constraints

Distinct internal statuses identify malformed claims/binding, capacity, exact
coefficient growth and a nonzero residual. Source/cover/work failures propagate
unchanged. Cancellation and cleanup remain the existing worker/process owner's
responsibility. Normal workspace cleanup clears all initialized GMP objects;
arena exhaustion uses the existing terminal worker failure, never in-process
recovery or publication of partial identities.

## Verification and Change Impact

Targeted potential-input tests must distinguish repeated/reversed probes, raw
aliases of ground, equal-endpoint cancellation and distinct active coordinates.
Fresh replay/setup refusal must detect stale binding; the independent checker
rebuilds the source representation from original SSA on every terminal and never
trusts search data. These checks add alias awareness without changing the full
original F/Q root inventory or the formal common-kernel authority.

The standalone driver must exercise signed conservation, missing literal-zero
rows, nonlinear opaque atoms, phi/SELECT/loop occurrences, exact finite scalars,
forged coefficients, another setup, a missing leaf, branch-dependent identities,
coefficient growth, cumulative work and observer rollback. Real BULK/CMG/IMG/SOI
program/setup/box inputs use the same driver and original cover checker; failure
is recorded explicitly and is not replaced with synthetic success. Transport
cancellation, cleanup and public actual-model qualification are owned by the
linked runtime and process designs. Source-checker tests do not substitute
for that join. Root/circuit/flow qualification is not claimed by this component.

Candidate construction starts with the full coefficient space for each F/Q
inventory. For each complete terminal it normalizes the residuals of the current
basis, computes their exact fraction-free nullspace, and composes that nullspace
with the current original-root basis. Thus every terminal constraint is retained
and the resulting basis spans the common kernel in this admitted source-linear
representation. It does not claim all nonlinear mathematical identities. In
particular Q=(x,x,y,y) versus Q=(x,y,y,x) must retain (1,-1,1,-1), even though no
individual basis vector selected on the first branch need be global. A separate
fresh replay checks every resulting relation against every original terminal.
The extra composition matrix and all intermediate coefficient growth consume the
same caller capacities/work/arena. The constructor requires capacity for the two
initial full raw-row bases, not merely the final number of identities. Circuit
row removal still requires an explicit full original-equation equivalence proof;
a zero residual, root feasibility and descriptor rank remain different facts.

The fixed `maximum_workspace_bytes` limit covers C tables and GMP value objects.
Coefficient limbs, GMP intermediate allocation, MPFR registers and all existing
worker storage share the independently admitted arena cap. Workspace close clears
exact values; fixed cold tables live until that arena is closed. Reopening an
already-live workspace is outside the exclusive cold-admission contract.

Qualification owner:
[ConstitutiveIdentityTests.c](../../../Tools/OSDIPrecisionVerification/ConstitutiveIdentityTests.c)
and [verify_constitutive_identities.py](../../../Tools/OSDIPrecisionVerification/verify_constitutive_identities.py).
The driver preserves exact request precision, four bit-pattern coordinate planes,
source contexts and independently supplied program/setup/request SHA pins. Its
process-cancellation check qualifies only the isolated verification child lifetime.
The separate public protocol-5 transport and its four original singleton capture
qualification are documented by the linked runtime owner.

The verification driver consumes the original final d10443f binary captures
(464-byte control plus all four coordinate planes) directly, without copying or
rewriting their source input. It validates the complete hash/size/geometry,
operation, contexts, precision and original receipt chain. Experimental arena,
work and step ceilings are separately recorded from the preserved request bytes.
The actual-model driver requires an explicit host-managed-payload ceiling before
opening the arena or calling the inherited cover loader. Checked preflight sums
program/setup/request copies, every host cover pool and the concurrent immutable
input copy, then adds the arena reservation once. Identity fixed tables and all
GMP/model peaks reside inside that arena and are not double-counted. Its component
fixed-workspace ceiling remains separate and is checked before allocation using
source counts (total phi edges conservatively bounds the largest staged block).
This is a managed-payload bound, not a process RSS or allocator-header claim.
A sparse 1000-row program regression rejects the otherwise multi-gigabyte dense
cover payload under its explicit host ceiling before any owned host allocation.

### Demand-initialized exact values

All maximum-capacity C metadata and GMP-object arrays remain preadmitted and
allocated before evaluation. A separate byte bitmap for each exact-value pool
records which GMP objects have actually been initialized. Capacity is not work:
opening a workspace does not initialize maximum-node/atom ceilings as live GMP
values. Before first use, the owner charges initialization and its eventual clear,
then initializes that slot exactly once. Unused slots are never read or cleared as
GMP objects. Node values, active matrix columns, composed relations and original
claim rows are initialized only when reached. Initialized values persist across
attempts and are reset explicitly where the algorithm requires zero; source-node
IDs and atom identities are still rebuilt per exact source replay. Close clears
only initialized slots, once, and bitmap traversal is covered by fixed-pool
admission work. Partial work refusal retains initialization flags for cleanup.

This addresses the actual-model open-stage stall: the arena uses first-fit block
scans, so eagerly initializing two million unused rational denominators introduces
allocator work unrelated to the executed source graph. It changes storage lifetime,
not the admitted formal expression model, exact coefficients or search/checker
logic. Tests must show zero exact slots initialized after cold open, small actual
use under a large node ceiling, repeated-attempt stability and exhausted-work
cleanup. Actual-model stage timing must distinguish improvement from inference.

### Explicit coordinate quotient (unreleased protocol 5)

An optional additional active-coordinate quotient is fixed at workspace admission;
its identity form is explicit for public callers. A canonical table has one UInt32
per actual active coordinate: NONE denotes ground, otherwise the value is the
smallest member of its class, not a chain. Every representative points to itself.
The source checker applies setup.map first, then this table to every kind-10
endpoint. Distinct flow coordinates cannot be merged or grounded by this added
condition. The worker checks canonicality and the ABI's is_flow facts; full units
and unknown/residual nature are validated by the actual-topology Swift admission,
not claimed as independently proved by program ABI 1.

The table is an explicit correlated-domain condition E, not a change to setup or
a claim that the circuit imposes E. Identities hold on D intersect E, where D is
the original four-plane/time box. The complete source cover remains a cover of
ALL D; no guard leaf, formula failure or branch is omitted using E. Thus coverage
of D intersect E follows without assuming branch feasibility. Each class's paired
interval intersection must be nonempty; every ground class must contain zero.
Exact lexicographic comparison of normalized high/low pairs establishes this
admission. An empty condition refuses rather than publishing vacuous identities.

Workspace owns an immutable copied table plus two index scratch arrays used to
check intersections in linear space/time. Their full capacity and initialization
work are preadmitted. Every attempt and independent checker revalidates the same
condition; no setter changes a live workspace's binding. Result facts retain the
exact condition pointer/count with program/setup/cover binding. A claim from a
second table/workspace cannot be reused as if unconditional. This does not prove
root existence, chart rank, smoothness, forward flow or whole-history coverage.

The C source driver tests identical/reversed probes under the original setup and
under added D=S/ground conditions, malformed classes, distinct flow refusal,
foreign condition binding and empty paired intersections. The public quotient
tests linked by the runtime additionally test real topology units/natures,
unchanged original-box leaves, cumulative work, cancellation and failed-session
cleanup. A source branch made infeasible by E is still replayed; its omission may
not be used to strengthen the common kernel.
Setup-alias fixtures include exact preparation-only source COLLAPSE roots matching
their setup map; a table-only rewrite is not an admissible source fixture and the
original complete-cover topology check must reject it.
