# Constitutive Identity Wire Projection

## Purpose and Scope

Parent: [WorkerProtocol](../DESIGN.md). No children. This component projects checked
source-linear identities into protocol-5 shared records. It owns canonical wire
encoding and projection work accounting, not the search/checker algorithm.

## Responsibilities and Boundaries

The existing complete-cover transport runs first. The adapter invokes the
[source algorithm](../../ConstitutiveIdentities/DESIGN.md) with that exact program,
setup, cover and four coordinate planes. Only independently checked claims are
projected; no supplied success flag, sampled value or partial proof substitutes
for that call. The parent boundary independently validates the resulting wire.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [WorkerProtocol](../DESIGN.md) | parent | Shared geometry, sequencing and publication | Existing worker lifecycle | No second publication owner |
| [Cover transport](../DecisionCover/DESIGN.md) | depends on | Complete original cover and cumulative counters | Input to identity construction | Preserve original planes/contexts |
| [Source algorithm](../../ConstitutiveIdentities/DESIGN.md) | depends on | Independently checked exact claims | Algebra authority | Formal source-linear scope only |
| [Parent C boundary](../../../COSDI/PrecisionProcess/ConstitutiveIdentities/DESIGN.md) | coordinates with | Geometry and canonical response checking | Wire consumer | Encoding changes require both sides |

## Architecture

```text
complete-cover transport -> source constructor + independent checker
  -> charged dense canonical projection -> existing release publication
failure -> no usable response -> existing worker/process cleanup
```

## Contracts and Invariants

Protocol 5 embeds the exact v4 cover prefix, then separate claim-kind, dense
coefficient and numerator-word regions with immutable identity limits. Every claim
has F/Q kind and every original raw-row coefficient; omission never implies zero.

A coefficient represents signed numerator / 2^p. Numerator magnitude is unsigned,
least-significant-first 64-bit words. Zero has no words, positive sign and p=0.
Nonzero magnitude has no leading zero word and is odd when p>0. Numerator and
denominator bit lengths fit the caller's coefficient capability. A coefficient
with a non-power-of-two denominator is refused, never rounded. Fixed slots use
checked ceil(bitLimit/64) words per coefficient; unused words are zero. Claims are
ordered F then Q, each with at most one raw-row-count worth of basis vectors.

The adapter preserves the complete cover's original program/setup, topology,
semantic profile, planes and contexts. It charges every projected initialized
slot plus the parent's bounded scan before output writes and updates cumulative
steps, guard and copied-byte counters on exit. No counter is reset per request.
Claim count becomes visible only after complete projection; failure invalidates
the response under the parent's existing publication protocol.

## State, Ownership, and Lifecycle

Identity fixed tables are admitted before evaluation inside the existing isolated
worker arena. `maximumWorkspaceBytes` bounds those fixed pools; it is not another
arena reservation. GMP limbs/intermediates, source interpreter and cover storage
share the existing arena cap. Proofs and mapping pointers are synchronous borrows;
no storage or callback escapes the current attempt.

## Failure, Concurrency, and Constraints

Source/cover/checker failures propagate unchanged. Coefficient growth,
non-dyadic projection, copy/work exhaustion and arithmetic overflow explicitly
refuse. The existing WorkerProtocol owner retains cancellation, sequence/release
publication and terminal arena-failure cleanup. Parent geometry supplies checked
maximum products and disjoint regions before this adapter writes them.

## Verification and Change Impact

[C transport tests](../../../../Tools/OSDIPrecisionVerification/ConstitutiveTransportTests.c)
and the linked runtime's public tests cover projection, quota and failure
publication. Source algebra/checker tests belong to the source component; the
four original singleton actual-model transport captures belong to the Swift
runtime qualification. None establishes root, flow or history authority. Wire
changes require the parent C checker and Swift borrowed views to remain consistent.

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
work owner. The successful projection also prepays two active-count index arrays
of parent interval-check writes in its copied-byte and work counters. Parent wire validation independently checks nonempty class interval
intersections and exact table equality against its admission snapshot. The worker
validates only available ABI facts (including is_flow); actual units/natures are
Swift admission's responsibility. Failure never calls a body, drops a source leaf,
changes the admitted table, or adds another cleanup owner.
