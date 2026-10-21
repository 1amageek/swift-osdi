# Complete Cover Worker Transport

## Purpose and Scope

Parent: [WorkerProtocol](../DESIGN.md). No children. Own fixed-arena protocol-4 workspace admission and field-wise complete-cover projection. [DecisionCover](../../DecisionCover/DESIGN.md) remains the sole finite coverage/checker authority.

## Responsibilities and Boundaries

Reserve every maximum tree, pending, prefix, replay-event, leaf, domain and visit pool before ready. Invoke the existing source constructor and independent checker against one validated private range-control view, then project the full checked tree and terminal pool. No public range/chart fallback or partial frontier response.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [WorkerProtocol](../DESIGN.md) | parent | Admitted slab, exact sequence, fixed arena | Sole child lifecycle | Atomic response follows all projection |
| [DecisionCover](../../DecisionCover/DESIGN.md) | depends on | Constructor plus independent source replay checker | Complete finite tree | No root/nonempty/flow authority |
| [Parent Projection](../../../COSDI/PrecisionProcess/DecisionCover/DESIGN.md) | coordinates with | Protocol-4 records and limits | Field-wise projection | Versions 1–3 and program ABI 1 unchanged |

## Architecture

```text
validated v4 geometry -> all maximum native pools -> ready
 -> exact box/private range view -> constructor -> independent checker
 -> bounded field projection -> success publication
 failure -> status only
```

## Contracts and Invariants

Copy/work/step/guard counters are cumulative, including failed attempts and independent replay. The explicit copied-byte counter additionally charges the parent input-plane transfer and the worker's field projection/tree/leaf transfer before publication. Program-owned register/replay-plane initialization and opaque GMP movement retain existing Program work/fixed-arena/deadline authority. No counter is reset across cover requests.

The wire carries the original box/context through immutable input regions and request context fields. All output records have canonical padding/reserved words and every terminal leaf retains exact actual-source formula planes. Last-leaf bound-step state is not an aggregate claim and is cleared from the cover response. Projection failure/overflow/cancellation/deadline/resource exhaustion never publishes a complete view.

## Verification and Change Impact

Native source tests already own construction/checker semantics. Actual C public-process tests and malformed-wire fixtures own transport, source binding, counters, atomic failure and all four source families. Parent/runtime compatibility checks run after this scope stabilizes.
