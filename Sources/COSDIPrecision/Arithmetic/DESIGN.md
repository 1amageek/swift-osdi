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

Multiplication and division specialize only ordered intervals with four finite, strictly positive endpoints. Multiplication is increasing in both operands; division is increasing in its numerator and decreasing in its denominator. At the existing temporary precision, directed rounding is monotone, so the following two calls produce exactly the same selected endpoints as all four corners rounded in both directions, including exponent underflow and overflow:

| Operation | Lower endpoint, RNDD | Upper endpoint, RNDU |
|---|---|---|
| Multiply | `a.low * b.low` | `a.high * b.high` |
| Divide | `a.low / b.high` | `a.high / b.low` |

Every other input retains the four-corner path, preserving signed-zero tie selection, nonfinite/NaN behavior and reversed-interval behavior. The division zero-domain check remains before either path. Both endpoints are computed in existing temporaries before output writes, preserving `out == a` and `out == b`, including unequal operand/output precisions. The fixed 65-unit real-operation work charge, worker shape and allocation ownership do not change. MPFR sticky flags are internal scratch state, cleared during initialization and never consumed or published; omitted corners may change their accumulated inexact flag without changing output, status or work.

## Verification and Change Impact
A separate 512-bit point evaluator samples 6237 interval points; Decimal nonlinear and strict delta fixtures check actual worker publication. `ArithmeticTests.c` independently evaluates all eight directed multiply/divide corners and checks exact endpoint equality (including signed zero), domain/status, initialized state and exhausted-work output preservation over 31,740 cases: five math precisions, matching/mixed operand/output precisions, three output-alias modes, signed zeros, nonfinite/NaN values, reversed intervals and exponent extremes. These fixtures do not qualify a complete model. Tests: [verification tools](../../../Tools/OSDIPrecisionVerification/DESIGN.md). Changes require the parent contract and affected runtime/compiler boundaries to remain consistent.
