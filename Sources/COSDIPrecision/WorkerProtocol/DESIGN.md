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
