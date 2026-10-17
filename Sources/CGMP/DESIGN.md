# GMP System Binding

## Purpose and Scope

Parent: [swift-osdi](../../DESIGN.md). This host system-library module exposes
the installed upstream GMP C declarations to the optional arithmetic worker.
It has no children and defines no numerical algorithm or replacement ABI.

## Responsibilities and Boundaries

SwiftPM pkg-config supplies include and link paths. Installation and version
selection are explicit environment preparation; the package does not install GMP.
The ordinary OSDI runtime and portable value targets do not depend on this module.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [Package](../../DESIGN.md) | parent | Optional native worker composition | System binding | No portable GMP capability is inferred |
| [COSDIPrecision](../COSDIPrecision/DESIGN.md) | used by | Upstream GMP allocation API | Isolated arithmetic | The worker is the sole allocator authority |

## Architecture

```text
COSDIPrecision -> CGMP shim -> installed upstream GMP
```

## Contracts and Invariants

The header and linked library must belong to the admitted native environment.
The binding does not change allocator hooks or global state. Only the isolated
worker may establish that authority before its first GMP/MPFR operation.

## Verification and Change Impact

Worker compile/link/runtime tests and exact numeric-library provenance qualify
this binding. A header/library version change requires those checks and enclosed
arithmetic evidence again; header import alone does not prove numerical behavior.
