# Controlled Precision Snapshot Construction

## Purpose and Scope

Parent: [OSDIRuntime](../DESIGN.md). No children. Own the additive ASCII controlled snapshot profile, exact cold layouts, managed payload admission and lifetime. Legacy snapshot behavior remains owned by [Precision Execution](../PrecisionExecution/DESIGN.md).

## Responsibilities and Boundaries

SDK validates its private program/setup format and charges actual construction loops, copies and hashes to the supplied caller control. The caller owns one cumulative work/storage policy, including cancellation. No caller reproduces SDK ABI layouts or parameter lookup costs. This managed-payload contract excludes allocator rounding/metadata, stack, process RSS and CryptoKit internals. Public byte-array extraction is a new caller-owned copy for controlled backing; the caller must admit and retain it. SDK does not track arbitrary external Array copies.

## Related Designs

| Design | Relationship | Contract Used | Cautions |
|---|---|---|---|
| [Precision Execution](../PrecisionExecution/DESIGN.md) | coordinates with | Snapshot and session ownership | Process creation uses scoped internal borrows |
| [Library Loading](../LibraryLoading/DESIGN.md) | depends on | Scoped image lease | No foreign pointer survives capture |

## Architecture

```text
same caller control -> checked image/header -> fixed program buffer
 -> charged ASCII lookup + exact setup plan -> fixed setup buffer
 -> sequential admitted CF hash Array scratch -> immutable shared backing
 -> program/session copies retain admission -> final release frees buffers/lease
```

## Contracts and Invariants

`OSDIPrecisionSnapshotControlling` requires synchronous `checkpoint(workUnits:)` and `reserveStorage(byteCount:)`. Zero-work checkpoints check cancellation. Returned Sendable reservation values retain admission until destruction. Controlled snapshot is a protocol requirement; its default explicitly refuses and never calls the unaccounted legacy method. Native control failures retain the original error under `OSDIError.host`; invalid format, limits and identity retain existing typed errors.

Controlled names must be ASCII. ASCII folding is bytewise A-Z only and matches the legacy lowercasing result for this profile. Non-ASCII parameter, alias and simulation names explicitly fail; legacy Unicode snapshot remains unchanged. String parameter VALUES remain validated UTF-8 and preserve exact bytes. Controlled preparation identity compares these original UTF-8 bytes directly, without Unicode canonical-equivalence normalization; a differently encoded spelling explicitly fails identity. Primitive defaults, Given, scope, node aliases and source names preserve original semantics.

Let P be validated exported bytes, A assignments, K source parameters, S simulation facts, R raw nodes, T serialized UTF-8 string bytes. Allocate P exactly after admission. Resolve A assignments in exact fixed slots; comparison and text traversal are charged before execution. Validate all header sections, names, duplicate keys and assignment consumption before setup publication. U is checked aligned header + K parameter facts + S simulation facts + 4R raw-map bytes + T string bytes; enforce maximumMappingBytes before any U allocation. Write facts/map/strings directly into U without growth arrays or old/new overlap. All arithmetic is checked.

CF hashing requires a cold Array copy. While fixed P and U are live, admit requested initialized byte payload P plus Array value, copy/hash P and release scratch; repeat for U. This explicitly required copy is not zero copy. Pinned CF fixed conversion payload (16 digit bytes + 64 hex bytes + 64 string bytes + 64 normalized bytes), digest values and shared owner value metadata are admitted. Allocator rounding and cryptographic backend internals remain outside this policy. Hash/copy/text/comparison work is charged before execution and never refunded.

## State, Ownership, and Lifecycle

Controlled backing is immutable after publication. A Mutex owns exact fixed pointer storage; borrowing reads its pointers under the lock, then runs callbacks outside the lock while retaining the backing. No pointer crosses await or escapes the internal scoped borrow. Bytes are fully initialized before any load. Allocation byte count/alignment/offset/stride are checked; unaligned ABI values are copied to local values. Exactly one owner deallocates each buffer, outside locks, before releasing admissions. Program and session copies share this owner. Ordinary model/library can close after extraction.

## Failure, Concurrency, and Constraints

Any resource, cancellation, identity or encoding failure destroys unpublished buffers and temporary leases; no partial program is returned. Work remains consumed. Metadata/preparation are borrowed immutable values, and shared backing supports concurrent read-only snapshots. No target has weaker synchronization. No worker, numeric kernel or model law changes.

## Verification and Change Impact

Dedicated `OSDIControlledSnapshotTests` compare legacy and controlled bytes/digests against actual exported programs; exercise scoped address reuse, copies/session retention after library close, exact pre-allocation refusal, mid-build cancellation, duplicate/scoped assignments, ASCII refusal, same-caller cumulative work and final-copy refund. Actual four-model fixtures establish the public path, not merely synthetic layout. Root owns frozen build/test execution. Parent source, Package excludes and Core integration are coordinated separately.
