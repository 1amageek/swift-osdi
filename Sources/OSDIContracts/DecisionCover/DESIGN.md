# Decision Cover Values

## Purpose and Scope

Parent: [OSDIContracts](../DESIGN.md). No children. Own portable bounds and finite source-decision/tree record vocabulary. No process, source evaluation, arithmetic proof or complete-result owner.

## Responsibilities and Boundaries

Validated caller limits bound steps, explicit cover copies, guard publication, integer cells/bits and all tree/leaf/pending/depth storage. Value records retain exact source location/opcode/occurrence and signed integer capsules without heap-backed per-record arrays. Constructing values never creates a complete cover, feasible guard, root or flow certificate.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [OSDIContracts](../DESIGN.md) | parent | Portable Sendable values | Module composition | No host imports |
| [Runtime DecisionCover](../../OSDIRuntime/DecisionCover/DESIGN.md) | used by | Scoped complete-only publication | Values become inspectable inside a borrow | Standalone values carry no completeness authority |
| [Native DecisionCover](../../COSDIPrecision/DecisionCover/DESIGN.md) | coordinates with | Exact partition semantics | Original source operations define the cells | No locally invented source lemma |

## Architecture

```text
caller policy -> validated limits
checked wire record -> one portable node value -> explicit caller inspection
```

## Contracts and Invariants

A signed integer capsule is canonical sign plus at most 17 little-endian UInt64 words, bounded to 1025 bits; zero has no words and a nonnegative sign. InlineArray storage avoids a heap allocation for each indexed record. Source choices retain Boolean, less/equal/greater or exact integer-cell semantics. An ambiguous event template has no selected outcome; child incoming decisions supply the exact selected outcome. Opcode and source occurrence are preserved. The v4 tree exposes decision/terminal only. Pending and exclusion are not successful public kinds: the current tree has no contradictory-edge witness, and every enumerated child is locally allowed. A future excluded-leaf proof requires a separately admitted witness kind, not a supplied success flag.

The explicit copied-byte quota owns cover storage copies/initialization, source-event publication and v4 input/output transport projection. Program-owned register/replay-plane initialization and opaque numeric-library movement retain their existing arithmetic work, fixed arena and deadline contracts. Limits remain session-cumulative. Mapping bytes bound the shared complete tree/leaf pool; arena bytes independently bound every native construction/checking pool.

## Verification and Change Impact

Portable limit/capsule rejection and actual scoped runtime tests own these values. Native independent replay owns mathematical coverage. Changes require the runtime adapter and protocol-4 projection to be checked together; versions 1–3 remain unchanged.
