# Constitutive Identity Process Boundary

## Purpose and Scope

Parent: [PrecisionProcess](../DESIGN.md). No children. This component owns the
parent-side protocol-5 geometry and complete-response validation boundary. Process
creation, mapping, child lifetime and cleanup remain in the parent component.

## Responsibilities and Boundaries

The parent admits an immutable identity operation and validates the worker's
response before exposing any C result view. It trusts neither response offsets nor
coefficient shape. It relies on the worker's independently checked source facts;
wire validity does not establish algebraic truth, rank, a root or history.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [PrecisionProcess](../DESIGN.md) | parent | Exact child, mapping, admission and cleanup | Existing process owner | No second process lifecycle |
| [DecisionCover boundary](../DecisionCover/DESIGN.md) | depends on | Complete source-cover response checks | Shared cover prefix and binding | All cover records must validate |
| [Worker projection](../../../OSDIPrecisionWorker/WorkerProtocol/ConstitutiveIdentities/DESIGN.md) | coordinates with | Canonical protocol-5 wire representation | Producer/consumer ABI | Changes require both sides |
| [Swift runtime](../../../OSDIRuntime/ConstitutiveIdentities/DESIGN.md) | used by | Validated C result borrow | Public native access | No callback on failure |

## Architecture

```text
checked immutable geometry -> existing child attempt -> immutable-header/cover checks
  -> identity wire check -> complete borrowed C result
any failure -> invalidate result -> existing close/reap/retained-cleanup path
```

## Contracts and Invariants

Protocol 5 is a separate immutable operation. Versions 1–4 retain their bytes and
semantics; an older worker refuses version 5 without fallback. Geometry contains
the exact v4 cover prefix and three disjoint claim/coefficient/word regions as
specified by the worker projection. Counts, products, strides, offsets, alignment
and total mapping bytes are checked before mapping. Maximum claim capacity is
twice the original raw-row count; dense coefficients cover every row.

Admission binds the exact source program/setup, descriptor, full raw and active
topology, semantic profile and limits. Evaluation preserves the four original
coordinate planes and both contexts. Before publication the parent verifies the
immutable header, complete cover and coefficient encoding. It checks F/Q kind
ordering/counts, dense slot locations, nonzero claims, canonical signs/zero/limbs,
bit bounds, zero unused words and cumulative copied-byte coverage. The encoding's
single definition is in the linked worker projection design.

## State, Ownership, and Lifecycle

The existing process owns the slab, fixed geometry snapshot, child and cleanup.
Result pointers borrow initialized, disjoint mapping regions only until the next
attempt or close. No new allocator or process owner is introduced here.

## Failure, Concurrency, and Constraints

`maximumMappingBytes` admits all wire payload. Wire validation polls cancellation
and deadline in bounded batches, including long limb spans. A malformed response,
mutation, quota failure, cancellation or timeout publishes no partial view and
uses the parent's same-child cleanup contract. Cleanup preserves the primary
error and retryable owner. Existing cumulative work/step/guard/copy counters are
not reset for identity attempts. Native execution has no WASM/Embedded profile.
Worker arena and fixed identity workspace limits are owned by their linked designs.

## Verification and Change Impact

[C transport falsifiers](../../../../Tools/OSDIPrecisionVerification/ConstitutiveTransportTests.c)
reject forged kinds, missing coefficients, noncanonical zero/sign/limbs, oversized
products, aliases and immutable-header mutation. The linked Swift runtime tests
exercise the actual public path and refusal/lifecycle behavior. Changes to layout
or checks require worker-projection and runtime agreement; transport acceptance
alone is not source-checker or actual-model qualification.

## Immutable Coordinate Condition Extension

Unreleased protocol 5 extends its suffix with a fourth region containing one
canonical UInt32 per active coordinate. It is copied at admission, immutable at
ready and every response, and explicitly reflected in the borrowed result's
binding. Header-size checks reject earlier version-5 layouts; versions 1–4 bytes
and semantics stay fixed. Source meaning and independent checking remain owned
by the linked source algorithm. The original cover and original box are unchanged.

All new products, offsets, initialized table words, private snapshot and interval
index scratch are checked before allocation. Version-5 mapping admission also
accounts for the new parent private table/scratch and cover visit payload alongside
the slab, without changing older operations' limit interpretation. Parent scans
poll cancellation/deadline in bounded batches; worker table copies, interval
checks, source substitutions and independent replay share the existing cumulative
work owner. Parent wire validation independently checks nonempty class interval
intersections and exact table equality against its admission snapshot. The worker
validates only available ABI facts (including is_flow); actual units/natures are
Swift admission's responsibility. Failure never calls a body, drops a source leaf,
changes the admitted table, or adds another cleanup owner.
