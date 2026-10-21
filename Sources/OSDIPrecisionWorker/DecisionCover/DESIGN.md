# Exact Source Decision Cover

## Purpose and Scope

Parent: [OSDIPrecisionWorker](../DESIGN.md). No children. Own finite construction and independent checking of an exhaustive decision tree for one immutable admitted source program/setup/topology and one exact closed coordinate/time box. Source interpretation belongs to ProgramEvaluation and arithmetic cells to Arithmetic. A complete decision cover is not a root, nonempty-chart, flow, reset, history, tube or event certificate.

## Responsibilities and Boundaries

[ConstitutiveIdentities](../ConstitutiveIdentities/DESIGN.md) reuses the existing
independent whole-tree validation and exact terminal replay internally. It cannot
accept a leaf subset, another program/setup, or a changed original box/control
as the same cover. Its independently checked linear relations add no feasibility,
seam, circuit-rank, root or forward-flow meaning to a decision cover.

Construct bounded candidate trees using exact source frontiers, then independently replay and validate their partitions, edges, traces, exclusions and terminal formula planes. Builder success alone grants no complete-cover authority. No source physics, arbitrary supplied coefficients, circuit projection, model calibration or source feasibility assumption enters this component. Point/range/chart operations 1–3 and compiler program ABI 1 retain their meanings.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [OSDIPrecisionWorker](../DESIGN.md) | parent | Fixed arena and execution provenance | Owns native composition | No alternate allocation/work owner |
| [ProgramEvaluation](../ProgramEvaluation/DESIGN.md#exact-decision-frontiers) | depends on | Bound source prefix replay/frontier | Source order, all stages, actual equality values | No sampled tapes or guessed branch |
| [Arithmetic](../Arithmetic/DESIGN.md#decision-cell-contract) | depends on | Exact relation and integer-cell partitions | Source primitive semantics | Domain/precision failures are not exclusions |
| [WorkerProtocol](../WorkerProtocol/DESIGN.md#complete-decision-cover-transport) | used by | Atomic complete-only publication | Same exclusive process lifecycle | No pending frontier is published as complete |

## Architecture

```text
bound source program/setup + exact coordinate/time box + explicit limits
 -> source frontier replay -> bounded candidate tree
 -> independent structural and source-replay checker
 -> complete borrowed tree/partition/leaf facts
 failure/incomplete -> no complete result
```

## Contracts and Invariants

Each decision site retains stage, block, instruction, kind and dynamic global occurrence ordinal. Real source comparisons partition into less/equal/greater, mapped through the original opcode to its Boolean value; Boolean casts preserve negative/zero/positive. CFG and SELECT choices must agree with their evaluated Boolean. Definite outcomes are checked and automatically appended to the complete trace. A prefix is an exact initial occurrence sequence, including those definite decisions. A correctly addressed choice contradicted by checked operand bounds may establish an empty prefix; malformed sites, kinds, ordinals, payloads or surplus entries cannot establish exclusion.

Rounding primitives retain exact half-open source cells. Enumerating only a favorable integer or discarding an endpoint is forbidden. Every required cell and its exact boundary must fit the caller's admitted cell/integer-bit/precision/storage limits. Domain, undefined, NaN, unsupported primitive, source status, singular demanded formula or incomplete cyclic unfolding refuses the complete operation. In particular an infeasible branch whose infeasibility has not been proved is not silently removed after its formula fails. Existing inactive pure-value/control demand rules remain authoritative.

Every candidate node has either all source-allowed distinct outgoing outcomes or a terminal chart/exclusion. A complete tree is finite, rooted, acyclic, contains no pending node, has no missing, duplicate or extraneous child, and binds every terminal trace to its exact path. The independent checker replays terminal paths against the original program and box and checks the source-derived outcome partition at each visited decision; it checks all output-plane bits against replay, not caller constants. Empty-prefix leaves must replay a genuine source-bound exclusion. All nodes must be reachable from the root and independently verified through terminal replay. Completeness follows by induction from the exhaustive local partitions and complete child incidence. A finite tree cannot certify a still-open source loop.

Terminal facts retain actual source equality rules and finite selected-formula F/Q/G/C bounds. G/C retain source AD-root meaning; no seam differentiability is inferred. Exact primitive infinity comparands keep their checked provenance, while finite projection overflow, NaN and nonfinite output planes remain failures. No complete cover asserts that every retained leaf is feasible. No formula closure is promoted into an exact source value.

## Runtime Flows

1. Admit all capacities and checked byte products before writing caller storage.
2. Explore the root prefix, appending definite trace nodes until a frontier or terminal.
3. Explore every allowed frontier child with the same immutable box and remaining quotas.
4. Run the independent checker over the closed candidate tree.
5. Publish the complete view only after every structural and source-replay obligation passes.

## State, Ownership, and Lifecycle

The program and box are immutable borrowed inputs under the existing exclusive session. Tree, pending-prefix, replay, guard, integer-boundary and output-plane pools are caller-preadmitted fixed arena/slab storage. All views expire at the next attempt or close; no automatic whole-guard materialization or persistent certificate owner is introduced. Result/candidate metadata is separate typed storage: admission checks its full extent and alignment against every mutable pool, all four immutable input planes and the program/setup/control/limits/workspace/counter owners before any write. A result pointer inside a tree pool must fail, rather than overwrite a checked root during publication. Failed attempts may alter scratch but publish no complete view. The same parent cancellation, deadline and fallible cleanup authority remains in force.

## Failure, Concurrency, and Constraints

Source work, replayed source steps, traversal, validation, copies and integer-cell work consume one cumulative session owner. Counters never reset between construction and independent checking. Diagnostic frontier usage records completed source steps and published event writes on numerical/malformed/resource refusal as well, without publishing partial proof facts. The explicit copy quota counts component-owned buffer copies/initialization and source-event publication; fixed-size scalar/local-value semantics and opaque GMP internals remain under the fixed arena and arithmetic work/deadline contract. Node, pending-prefix, leaf, guard, cell, integer-bit and byte limits are explicit. Overflow, capacity, undefined formulas, exhausted precision/work/steps, malformed trees and incomplete loops are distinct refusals. Refusal does not narrow the requested box/history family; later explicit box refinement is a separate attempt under the caller's total execution accounting.

## Verification and Change Impact

The verification tools own equality-only branches (`x*x==2`), strict/non-strict faces, differing path lengths, source loop ordinals, arithmetic cell endpoints, missing/duplicate/extra children, forged exclusions, corrupted leaves/identities, undefined/singular formulas, cumulative quotas and cancellation. Actual four-family programs execute the same source-bound path. Point/range/chart regression evidence must remain valid after interpreter hooks and worker transport changes. Public transport is connected only after the internal frontier and independent checker are behaviorally qualified. Root/continuation and physical all-history obligations remain separate.
