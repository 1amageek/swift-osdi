# OSDI Runtime Regression Tests

## Purpose and Scope

Own S1 public API behavioral evidence on macOS arm64. Parent:
[swift-osdi](../../DESIGN.md). Children:
[compiled fixtures](Fixtures/DESIGN.md) and [fixture support](FixtureSupport/DESIGN.md).
This target establishes artifact-backed native admission, copied metadata, stable
zeroed storage and library lifetime. It does not qualify numeric F/Q evaluation.

## Responsibilities and Boundaries

Exercise the public OSDINativeLibraryLoader through SHA-256 LocalArtifactReferencer,
ArtifactAccessIntent and the real ArtifactRootCapability. Use direct C fixtures only
as independent ABI/lifetime inputs. Explicitly selected qualification also admits actual OpenVAF-generated F/Q
model libraries. Inject read/close failures through protocol wrappers of
real artifact sessions, preserving real root-session cleanup.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [Runtime](../../Sources/OSDIRuntime/DESIGN.md) | verification target | Loader, library, storage | Public behavioral evidence | Native-only S1 scope |
| [COSDI](../../Sources/COSDI/DESIGN.md) | verification target | ABI and bounded validation | Typed malformed-image refusals | Native code is trusted |
| [Fixtures](Fixtures/DESIGN.md) | child | Valid, extended and malformed libraries | Independent metadata inputs | Numeric callbacks refuse evaluation |
| [Support](FixtureSupport/DESIGN.md) | child | Serialized asynchronous setup | Shared compiler/cache ownership | Lifetime-sensitive tests share one serialized suite |

## Architecture

```text
fixture path -> LocalArtifactReferencer + SHA256 -> exact ArtifactAccessIntent
 -> ArtifactRootCapability -> public native loader -> copied descriptors
 -> stable model/instance storage lease -> scoped MutableSpan callbacks
 -> explicit close gate / final owner release -> native destructor marker
```

## Contracts and Invariants

A valid two-descriptor image must retain both names; a sixteen-byte extended stride
must step to the second descriptor correctly. Malformed ABI versions, exports,
strides, counts, offsets, alignment, table pointers, indices and callbacks must
produce the expected typed native code/field. Jacobian array capacities accept
optimized inactive entries, including capacities above the final sparsified table
count. They refuse underprovided capacities and capacity bytes above the caller's
metadata budget. Digest tampering must be rejected
before any constructor event. Requested resource limits must be enforced.

Both model and instance blocks start entirely zeroed, retain their addresses and
preserve writes across independent scoped borrows. Callback errors retain their
specific error type. An active storage lease blocks library close and keeps the
image loaded after the public library value is dropped. Final storage release
unloads the image. Successful close is idempotent and rejects new storage; copied
metadata remains usable afterward.

## Failure, Concurrency, and Constraints

All marker-sensitive tests belong to one serialized suite. The shared setup actor
prevents duplicated concurrent compilation. Tests compare event deltas, so distinct
loads of the same input do not depend on suite execution order. A continuation
barrier proves a lease is live before another task attempts close; a second barrier
releases it before final close. No sleep or scheduler luck establishes the race.
Read and close errors remain independently observable, including their combined
cleanup failure. Temporary directories retain diagnostic artifacts.

## Verification and Change Impact

Root runs this target with the pinned Swift toolchain and an external timeout.
Each test has a one-minute Swift Testing limit. The ordinary native regression
run and sanitizer run own library/storage behavior; fixture compilation and helper
type checking alone do not establish completion. Changes to artifact validation,
metadata publication, storage borrows or owner shutdown invalidate corresponding
regressions. Unsupported hosts remain outside successful qualification.

## Standalone Selection

The [standalone verification contract](../../Tools/OSDIStandaloneVerification/DESIGN.md)
owns explicit qualification selectors. Default metadata/protocol inputs need only
the C compiler; requested actual-model qualification retains the original input
validation and fails on missing compiler/artifact evidence.
