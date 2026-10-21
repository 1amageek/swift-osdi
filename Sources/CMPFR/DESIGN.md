# MPFR System Binding

## Purpose and Scope

Parent: [swift-osdi](../../DESIGN.md). This host system-library module exposes
the installed upstream MPFR C declarations to the optional arithmetic worker.
It has no children and defines no numerical algorithm or replacement ABI.

## Responsibilities and Boundaries

SwiftPM pkg-config supplies include and link paths. The caller prepares the
installed library; the package does not install it or select a fallback backend.
Ordinary OSDI operations and portable value targets do not depend on this module.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [Package](../../DESIGN.md) | parent | Optional native worker composition | System binding | Native qualification is explicit |
| [OSDIPrecisionWorker](../OSDIPrecisionWorker/DESIGN.md) | used by | Directed-rounding MPFR API | Enclosed arithmetic | The worker owns precision, caches and resource admission |

## Architecture

```text
OSDIPrecisionWorker -> CMPFR shim -> installed upstream MPFR -> GMP
```

## Contracts and Invariants

The binding preserves upstream declarations. It neither changes rounding state
nor chooses interval semantics. The arithmetic owner checks supported versions
and binds actual linked-library bytes to its worker qualification profile.

## Verification and Change Impact

Worker compile/link/runtime and containment tests qualify the actual installed
libraries. Changing MPFR or its GMP dependency requires numerical, allocation and
failure evidence to be rechecked. Successful import alone is insufficient.
