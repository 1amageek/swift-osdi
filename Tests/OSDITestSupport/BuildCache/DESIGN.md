# Native Fixture Build Cache

## Purpose and Scope

Parent: [OSDITestSupport](../DESIGN.md). No children. Own exact fixture compilation,
validated persistent reuse and cross-process publication in the package build area.

## Responsibilities and Boundaries

The cache owns source/header/tool/SDK identities, normalized command arguments,
per-key locks, temporary builds and immutable receipts. Callers declare the complete
non-system include closure and fixture variant. System headers/libraries are bound
to the selected SDK path/settings. SDK installations are immutable for that
identity; in-place edits to system headers/libraries without changing the SDK
identity are outside this cache contract. The selected compiler and SDK are also
assumed immutable during one short preparation batch. Source/header changes during a build
are checked before publication. Test cases own run directories and lifetime logs.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [Test support](../DESIGN.md) | parent | Host-only shared preparation | No production dependency | No new public product |
| [Runtime fixtures](../../OSDIRuntimeTests/Fixtures/DESIGN.md) | used by | Cached metadata image with runtime-bound marker | Real constructor/destructor evidence | No build-directory marker may be embedded |

## Architecture

```text
compiler + SDK + source/include digests + target/flags/output kind
 -> SHA-256 request key -> bounded per-key flock
 -> validate receipt and artifact digest
    -> hit: return immutable image
    -> miss/invalid: build private sibling -> verify inputs and bytes
       -> atomic directory rename -> ready artifact
```

## Contracts and Invariants

Default cache: `<package>/.build/osdi-fixture-cache-v1`; no user setup is required.
`OSDI_TEST_CACHE_DIRECTORY` is an optional acceptance-test/CI isolation override.
SHA-256 input identities, not timestamps, decide reuse. Artifact hits verify content
against a bounded receipt. Invalid cache data is diagnosed and rebuilt under the
same lock; compilation failure never becomes a hit. Different source/header/flags,
compiler/backend, SDK or architecture identities select different keys.

## State, Ownership, and Lifecycle

Each batch admits at most min(4, active processor count) independent builds. One actor coordinates each cache instance; OS file locks coordinate independent
actors/processes. Lock descriptors are scoped across the awaited build and closed
on every exit. Immutable entries remain owned by the build area. Every run owns
separate writable observation files; no log or native owner is shared across runs.
A failed/cancelled temporary build is removed after bounded child cleanup.

Metadata fixtures locate a test-owned `.osdi-fixture-run` ancestor of their mapped
image via dladdr and write `fixture-<kind>.lifetime` there. Test staging is beneath
that run directory. The fixture knows no private Runtime directory name/depth.
Legacy qualification builders create the same sentinel and marker schema.

## Failure, Concurrency, and Constraints

Default lock wait and compile deadlines are 30 seconds; output capture is capped
at 1 MiB, artifact validation at 16 MiB and receipt decoding at 1 MiB. Tests can
supply shorter finite process/lock limits. Pending cleanup retains its directory and the existing typed process error/handle; no active child output is deleted before confirmed cleanup. Only completed, validated directories are
published. Same-key waiters reread after lock acquisition; unrelated keys proceed
independently. A failed batch cancels and joins every submitted child and retains
all error/cleanup owners before returning failure. No external callback or I/O occurs inside a Swift Mutex.

## Verification and Change Impact

Focused cache regressions use controlled process results for storage/lifecycle contracts; task acceptance uses real Clang launch records: unchanged
requests do not compile; a changed header/variant rebuilds only its dependents;
corrupt bytes are rejected; same-key contenders publish one build; failed/cancelled
builds publish no receipt and preserve process cleanup. Native loader tests retain
all constructor/destructor, malformed ABI and owner-lifetime assertions.
