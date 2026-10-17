# COSDI

## Purpose and Scope

COSDI is the native C module of swift-osdi. Parent: [swift-osdi](../../DESIGN.md).
Its child [Loading](Loading/DESIGN.md) implements the public native owner boundary.
The ABI directory holds an unchanged third-party declaration, not another runtime.

## Responsibilities and Boundaries

Own the pinned OSDI 0.4 declaration and the public opaque C boundary. The unchanged
OpenVAFReloaded [header](ABI/osdi_0_4.h) is the sole ABI struct definition; its SHA-256
is `0f3eeb9ef3c9f0bea9b3c59f34c632555e2367f6e28722e219d8c3f1d655a16f`.
The [public header](include/COSDI.h) exposes owner, limits, diagnostics and borrowed
inspection values. The Swift runtime owns serialized lifetime and storage leases.
Model physics, compiler implementation and simulator policy are external.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [swift-osdi](../../DESIGN.md) | parent | Native module composition | Swift-to-C boundary | Native-host qualification is explicit |
| [Loading](Loading/DESIGN.md) | child | Open/inspect/close, zeroed storage | Native owner implementation | Borrowed metadata follows owner lifetime |

## Architecture

```text
Swift runtime -> public COSDI opaque owner API
  -> Loading component -> pinned upstream ABI -> POSIX native library
```

## Contracts and Invariants

Public ABI types are declared only once. OSDI structs remain private to C source.
Loading supplies typed admission and explicit close outcomes; its detailed budget,
sentinel, retry and unsafe ownership contracts are authoritative in the child design.
The C module does not call any model setup or evaluation callback in this stage.

## Verification and Change Impact

The child design owns native admission and failure evidence. Swift tests exercise
the exported contract, storage isolation and lifetime using real generated models
and bounded metadata fixtures. An ABI/header change requires the child validator,
Swift bindings and fixture compiler to be rechecked together.
