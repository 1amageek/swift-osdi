# WorkerProtocol

## Purpose and Scope
Own child-side shared geometry, request sequencing and publication. Parent: [COSDIPrecision](../DESIGN.md). No children.

## Responsibilities and Boundaries
One outstanding request uses acquire/release counters and pipe notifications. A result is published only after complete success; no model callback or solver runs here.

## Related Designs
| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [COSDIPrecision](../DESIGN.md) | parent | Isolated precision execution | Owns composition and resource contract | Failure never changes accepted circuit state |

## Architecture
```text
Inherited slab/pipe descriptors -> ready handshake -> bounded requests -> atomic response publication
```

## Contracts and Invariants
One outstanding request uses acquire/release counters and pipe notifications. A result is published only after complete success; no model callback or solver runs here.

## Verification and Change Impact
Real process tests cover ready, persistent requests, invalid geometry/sequence, failures and clean EOF shutdown; runtime tests own spawning, cancellation, timeout and reaping. Tests: [verification tools](../../../Tools/OSDIPrecisionVerification/DESIGN.md). Changes require the parent contract and affected runtime/compiler boundaries to remain consistent.

## Version-2 Geometry

The 464-byte control structure and fourteen disjoint regions retain their physical
layout. Version 1 keeps every existing meaning. Explicit version 2 uses this map:

| Regions | Element count | Meaning |
|---|---|---|
| 0/1 | Bytes | Immutable program/setup |
| 2/3/4/5 | Active coordinates | Lower high/low, upper high/low |
| 6/7/8/9 | Raw nodes | F lower/upper, Q lower/upper |
| 10/11/12/13 | Raw nodes squared | G lower/upper, C lower/upper |

Candidate/reference context storage means lower/upper time in version 2. The mode
must match, normalized endpoints must be ordered, and initialization remains time
zero. Version selection is immutable at admission. `NONSMOOTH=10` extends worker
status without renumbering existing statuses. Ready/sequence/publication and
resource ownership remain unchanged. Runtime owns wrong-operation refusal and
the separate public process issue namespace.
