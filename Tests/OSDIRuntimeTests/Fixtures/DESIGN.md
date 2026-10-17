# Native OSDI Fixtures

## Purpose and Scope

Own independently compiled input libraries for S1 native metadata admission and
library lifetime tests. Parent: [runtime tests](../DESIGN.md). The real Verilog-A
models contain both resistive F and reactive Q contributions; S1 establishes
metadata compatibility only. S2 model evaluation and numerical qualification are
outside this fixture contract.

## Responsibilities and Boundaries

`metadata.c` creates valid and deliberately malformed descriptors from the unchanged
pinned OSDI 0.4 header. Its callback addresses establish metadata shape; invoking
those callbacks aborts or explicitly returns fatal evaluation status. They do not
represent implemented numerical models. `linear_rc.va` and `two_models.va` are
actual input programs compiled by a recorded OpenVAF executable.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [Runtime tests](../DESIGN.md) | parent | API behavior assertions | Test ownership | Host admission only |
| [Builder](../../../Tools/OSDIVerification/DESIGN.md) | used by | Pinned header and bounded compiler invocations | Reproducible inputs | Native executable code is trusted |
| [COSDI](../../../Sources/COSDI/DESIGN.md) | verification target | Version, stride, metadata, lifetime | Success and typed refusal cases | Ground sentinel only in ABI-permitted positions |

## Architecture

```text
metadata.c + pinned header + fixture kind + unique marker path -> clang -> dylib
linear_rc.va / two_models.va + recorded OpenVAF -> actual OSDI model library
compiled libraries -> runtime loader -> metadata/storage/close assertions
```

## Contracts and Invariants

Two descriptors have distinct names and valid table references. Extended records
add exactly sixteen bytes after the full pinned header and poison extension bytes
so accidental `sizeof(header)` stepping is observable. Counts, offsets, alignment,
node indices, required symbols, required tables and callbacks each have dedicated
malformed variants. Jacobian array counts represent allocated capacities: active
flags and final table entries may be fewer after compiler optimization. Two-entry
fixtures with one active resistive/reactive entry admit both capacity two and
capacity three. Capacity below the active count is refused; capacity-byte products
must fit the caller's storage budget. The pinned generator sparsifies the table
after recording capacities, so the final entry count is not their upper bound. All unused scalar residual offsets and bound-step offsets use
the permitted UINT32_MAX sentinel. Mapping, pointer-array, collapsed and state
index offsets remain valid even for zero-length fields. Ground is permitted only
as the second collapse node in the valid ground-pair fixture.

## State, Ownership, and Lifecycle

Each build uses a fresh output directory. Every native library embeds its own
absolute marker path and appends `load` in its constructor and `unload` in its
destructor. The test owns the directory until all library/storage owners release.
No environment variable, shared global callback, or cross-test marker is used.

## Verification and Change Impact

Builder commands use thirty-second child timeouts and record source/header/compiler
and binary SHA-256 values. Loader tests inspect both descriptor names and verify
markers after success, failed admission and explicit close. Actual OpenVAF output
must also pass metadata admission. Updating the ABI source invalidates every
fixture and requires intentional pin replacement and full S1 regression.
