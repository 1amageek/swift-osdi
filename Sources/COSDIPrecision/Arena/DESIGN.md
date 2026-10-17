# Arena

## Purpose and Scope
Own the sole-process GMP allocator and fixed resident arena. Parent: [COSDIPrecision](../DESIGN.md). No children.

## Responsibilities and Boundaries
Aligned blocks split, merge and reallocate inside one admitted region; exhaustion exits 80 and invariant failure exits 81. GMP hooks never return NULL or longjmp.

## Related Designs
| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [COSDIPrecision](../DESIGN.md) | parent | Isolated precision execution | Owns composition and resource contract | Failure never changes accepted circuit state |

## Architecture
```text
GMP hooks -> checked arena blocks -> fixed reserved bytes
```

## Contracts and Invariants
Aligned blocks split, merge and reallocate inside one admitted region; exhaustion exits 80 and invariant failure exits 81. GMP hooks never return NULL or longjmp.

## Verification and Change Impact
Forced cold and MPFR-internal exhaustion tests establish failure isolation; sanitizer tests cover alignment, splitting/coalescing and copies. Tests: [verification tools](../../../Tools/OSDIPrecisionVerification/DESIGN.md). Changes require the parent contract and affected runtime/compiler boundaries to remain consistent.
