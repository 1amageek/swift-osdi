# Complete Cover Process Projection

## Purpose and Scope

Parent: [PrecisionProcess](../DESIGN.md). No children. Own protocol-4 tree/cell/leaf geometry and parent-side complete-response structural validation. Parent owns the sole process/slab/pipe lifecycle.

## Responsibilities and Boundaries

Project a successful checked native cover into fixed pointer-free wire records and admit the complete response before borrowing. The numeric worker's [independent checker](../../../COSDIPrecision/DecisionCover/DESIGN.md) owns source replay; parent validation is additional wire/partition/incidence/provenance validation, not an independent physical proof. Compiler program ABI 1 and protocols 1–3 remain exact.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [PrecisionProcess](../DESIGN.md) | parent | Exact child, geometry, sequence, timeout/cancel, close | Sole lifecycle | No duplicate spawn/reap implementation |
| [Worker Cover Transport](../../../COSDIPrecision/WorkerProtocol/DecisionCover/DESIGN.md) | coordinates with | Atomic complete-only wire | Shared record contract | Native padding is never serialized |
| [Runtime DecisionCover](../../../OSDIRuntime/DecisionCover/DESIGN.md) | used by | Borrowed validated tree/leaf pool | Scoped public API | No partial view |

## Architecture

```text
original box/context -> existing request sequence
 -> independently checked source cover -> projected v4 records
 -> immutable header/counters + complete tree/cells/leaf checks
 -> complete-only borrowed result
```

## Contracts and Invariants

Protocol 4 has a separate header beginning with the unchanged 464-byte control. Regions 0–13 keep range layout; regions 6–13 are replay scratch and carry no complete-cover result authority. Added tree and terminal-plane regions have explicit caller-admitted maximum counts. Fixed records contain canonical 17-limb signed cells, source decisions/events and child/leaf incidence. Records are field-projected, including zero reserved words; no native struct memcpy defines the wire.

All counts, byte products, alignment, offsets, disjointness and mapping size are validated before access. The complete immutable suffix is checked at ready and every response. Successful counts are bounded; pending and exclusion kinds are refused because this wire has no contradictory-edge witness. No invalid branch is dropped. parent-before-child structure and exact child partitions are checked, leaf references are unique and exhaustive, all operand facts (including indexed exact primitive infinities) are validated, and all terminal planes are finite and ordered. Program/setup, original input plane bits and requested contexts/precision remain bound to the same request. Source replay success inside the qualified worker remains necessary; a builder success flag cannot replace it.

Work/steps/guard writes/explicit copied bytes are session-cumulative and monotone. All parent validation scratch is allocated once before ready from checked maximum tree/leaf counts and released only with the same process owner. Parent tree/plane scans check cancellation/deadline at bounded intervals. Worker/native capacity/status failures retain typed causes; unknown versions/statuses and malformed results are protocol failures, never partial success. Old workers cannot admit protocol 4.

## State, Ownership, and Lifecycle

Parent-provided original planes are lexical borrows and may alias each other. Response and diagnostic metadata must be aligned, separate from one another and from input/context/session/slab storage; invalid aliases are rejected without writing that metadata. Input planes cannot borrow a previous result slab, whose lifetime expires at the new attempt. Fixed slab output expires at the next operation or close. No persistent certificate, extra lease, child or retry owner exists. Failed response invalidates the same session; retained cleanup keeps all pools until exact child reap/unmap succeeds.

## Verification and Change Impact

Actual-worker C tests cover complete trees, all leaves, re-use, cumulative quotas, unsupported old worker, cancel/close and actual four-family source fixtures. Wire and invalid-response fixtures falsify geometry, binding, partial/pending trees, child/leaf incidence, cell canonicalization, exact primitive provenance and nonfinite planes. Existing point/range/chart regression paths retain their meanings.
