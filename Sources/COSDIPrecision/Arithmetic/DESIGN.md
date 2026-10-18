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

## Smooth-Box Arithmetic

The [package range boundary](../../../DESIGN.md#causal-range-boundary) defines the
separate version-2 operation. Directed interval operations are shared with version
1. Only the range interpreter requests regularity checks and outward binary64
lower/upper projection; point work charges and midpoint/radius projection remain
unchanged.

Regularity requires an open smooth neighborhood of every demanded coordinate/time
dependency, including zero-width boxes. Active independent operands with equal
point values do not prove source identity. Comparisons require a strict separation
unless the interpreter proves the same source operand. Active floor/ceil and
real/integer conversion reject their discontinuity faces; elementary operations
reject demanded domain faces. Fixed setup facts do not acquire spatial/time
dependence. A regularity refusal uses `NONSMOOTH`, distinct from invalid domains,
unsupported operations and resource exhaustion. The interpreter preserves inactive
demand semantics when carrying this status.

Projection uses RNDD for the lower binary64 endpoint and RNDU for the upper endpoint
and rejects nonfinite results. Tests must distinguish floor crossings and boundary
touches from smooth interiors, independent equal-valued variables from identical
operands, and outward bounds from midpoint approximations.

### Shared Positive Sum

After the interpreter proves the exact shared source shape and its successful
ordinary evaluation, `x+sqrt(x*x+c)` with finite x and an inactive finite exact
singleton `c>0` can tighten its existing enclosure. The real function is strictly
positive and increases in x. At each endpoint with x nonnegative, directed
square/add/sqrt/add gives a bound. At a negative endpoint, the equivalent form
`c/(sqrt(x*x+c)-x)` avoids subtractive cancellation: an upward denominator and
downward division give the lower bound; a downward denominator and upward
division give the upper bound. The result is intersected with the ordinary
enclosure. A contradictory intersection is a numeric invariant failure.

All proof arithmetic is charged, uses existing fixed temporaries, and verifies
finite positive denominators and finite bounds. If finite arithmetic cannot
establish this refinement, the ordinary enclosure remains authoritative; no
branch choice, epsilon, midpoint or domain/poison fallback is introduced. The
helper does not infer source sharing from interval equality. Negative-x
cancellation, mixed-sign x boxes and unequal-but-equal-valued inputs distinguish
the proof from ordinary interval evaluation and unsound numeric matching.

## Decision Cell Contract

[DecisionCover](../DecisionCover/DESIGN.md) uses source-faithful comparison trichotomies and exact FIcast/FItrunc/floor/ceil integer cells in its separate interpreter mode. Cell endpoint inclusion follows the original primitive, including ties away from zero and the truncation zero cell. Candidate envelopes are outward and every required cell is retained. Exact boundaries, cell counts, integer bits, numeric precision and copying must fit preadmitted cumulative limits. Failure to represent or enclose all cells refuses the complete cover. Domain/regularity/undefined failures for other primitives remain explicit refusals; no equality facet or singular formula is removed. Ordinary arithmetic and operations 1–3 remain unchanged.
