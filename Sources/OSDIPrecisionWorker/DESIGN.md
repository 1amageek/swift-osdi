# Precision Worker Entry

## Purpose and Scope
Parent: [swift-osdi](../../DESIGN.md). No children. Own the C executable composition entrypoint.

## Responsibilities and Boundaries
Pass inherited descriptors 3, 4 and 5 to COSDIPrecision. The numeric module owns child execution; COSDI runtime owns parent process lifetime.

## Related Designs
| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [COSDIPrecision](../COSDIPrecision/DESIGN.md) | depends on | cs_osdi_precision_worker_run | Actual worker implementation | Native host only |

## Architecture
```text
main -> cs_osdi_precision_worker_run(3, 4, 5) -> bounded child arithmetic
```

## Contracts and Invariants
The executable performs no model arithmetic or process launch outside its numeric module.

## Verification and Change Impact
The real static executable is exercised by the precision verification tools. Compiler/library/source identities are recorded before qualification.
