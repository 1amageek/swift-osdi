# Precision Worker Verification

## Purpose and Scope
Parent: [swift-osdi](../../DESIGN.md). No children. Own reproducible static builds and focused behavioral/resource evidence.

## Responsibilities and Boundaries
Build the same production C sources using installed static MPFR/GMP archives. Preserve exact compiler, SDK, headers, archives, source and worker identities. Test actual child IPC and independent high-precision mathematical references; complete model/circuit qualification remains owned by CoreSpice.

## Related Designs
| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [COSDIPrecision](../../Sources/COSDIPrecision/DESIGN.md) | verifies | Arithmetic, program, resource and IPC | Exact worker path | Focused fixtures do not certify physical models |

## Architecture
```text
production C sources + static archives -> exact worker receipt -> real IPC fixtures
                                                   -> independent Decimal containment
interval operators -> separate 512-bit MPFR reference -> sampled containment
```

## Contracts and Invariants
All subprocesses have external timeouts. Worker failure exposes no successful view. The nonlinear sub-ULP fixture distinguishes complete evaluation from F(high)+G(high)*low. Source setup/phi/resource fixtures reject plausible incomplete implementations. Expression identity tests include a BULK-shaped duplicate division/power predicate, an unequal expression with overlapping enclosures, colliding hash buckets, work refusal and fixed identity-capacity exhaustion. Inactive-value/control fixtures require a disabled undefined intermediate to succeed while demanded values, unsupported control regions, effects, loops and external entries fail. Normalized candidate/reference pairs are asserted before real-model runs. The projection regression covers a rounded low component reaching an odd high component midpoint.

## Verification and Change Impact
Run build_worker.py with --output-dir, then verify_worker.py with --worker under an external timeout. ArithmeticTests.c links the same production arithmetic/arena/identity sources. verify_models.py reuses exact native snapshot bytes and records explicit raw-point vectors, both contexts, all output slabs, first-request timing and optional OS-heap interposition samples; these points are not circuit solutions. Repeat using address/undefined sanitizers. Record steady process/IPC latency separately from arithmetic allocation requests; zero total allocation is not assumed.
