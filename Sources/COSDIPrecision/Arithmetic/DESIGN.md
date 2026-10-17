# Arithmetic

## Purpose and Scope
Own directed MPFR intervals, definite predicates and enclosure-preserving output projection. Parent: [COSDIPrecision](../DESIGN.md). No children.

## Responsibilities and Boundaries
The closed opcode subset preserves decoded primitive facts and encloses every supported operation. Periodic range extrema and tangent poles are checked; unresolved predicates/domain/projection failures remain explicit. Output high/low centers are renormalized with TwoSum before the outward radius is recomputed against the original interval.

## Related Designs
| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [COSDIPrecision](../DESIGN.md) | parent | Isolated precision execution | Owns composition and resource contract | Failure never changes accepted circuit state |

## Architecture
```text
Typed operands -> directed interval operation -> hi/lo center plus outward radius
```

## Contracts and Invariants
The closed opcode subset preserves decoded primitive facts and encloses every supported operation. Periodic range extrema and tangent poles are checked; unresolved predicates/domain/projection failures remain explicit. Output high/low centers are renormalized with TwoSum before the outward radius is recomputed against the original interval.

## Verification and Change Impact
A separate 512-bit point evaluator samples 6237 interval points; Decimal nonlinear and strict delta fixtures check actual worker publication. These fixtures do not qualify a complete model. Tests: [verification tools](../../../Tools/OSDIPrecisionVerification/DESIGN.md). Changes require the parent contract and affected runtime/compiler boundaries to remain consistent.
