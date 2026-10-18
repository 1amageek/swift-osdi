# External Consumer Verification

## Purpose and Scope

Parent: [swift-osdi](../../DESIGN.md). Own an independent SwiftPM consumer smoke
check for the public OSDIRuntime/OSDIContracts products and ordinary public CLI.
This tool has no production responsibility or child design.

## Responsibilities and Boundaries

Create a fresh package in caller-selected scratch, resolve exact Git revisions,
and compile, link and call public borrowed Span APIs without `@testable` or client
experimental settings. Compile the existing tracked C protocol oracle; this proves
ABI transport and consumer integration, not physical model qualification.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [Package](../../DESIGN.md) | parent | Public product boundaries | Release consumer evidence | Fixed native baseline only |
| [Point evaluation](../../Sources/OSDIRuntime/PointEvaluation/DESIGN.md) | depends on | Scoped F/Q/G/C and explicit history | Actual public calls | No solver or implicit acceptance |
| [Evaluation fixtures](../../Tests/OSDIEvaluationTests/Fixtures/DESIGN.md) | depends on | `protocol.c` modes 12 and 1 | Success and fatal refusal oracle | Transport only |
| [CLI](../../Sources/OSDIEvaluate/DESIGN.md) | depends on | Schema 2 request/report | Persisted JSON comparison | Read reports from disk |

## Architecture

```text
source URL/revision + Foundation URL/revision + new scratch + fixed tools
  -> isolated HOME/environment and detached exact Git checkouts
  -> tracked header/protocol source -> bounded clang -> two native fixtures
  -> URL/revision-only generated consumer package -> build -> public API probe
  -> exact source checkout -> osdi-evaluate build -> persisted success/refusal reports
  -> API/CLI numeric equality + binding/persistence/cleanup checks -> receipt.json
```

## Contracts and Invariants

Require macOS 26 or later arm64 and the fixed Swift 6.4.0 release. Accept an
explicit 6.4.0 version or a 6.4 version from a `swift-6.4.0-RELEASE` toolchain path,
matching the standalone runner; development toolchains are refused.
Both revisions are full Git object
identities; the direct Foundation revision must equal the source manifest pin.
The generated manifest uses the original Foundation URL. Canonically equal
Foundation URLs create no mirror configuration. An explicitly supplied different
local Git URL may create a Foundation availability mirror for local preflight
only; a different remote mirror is refused. The receipt records the resolution
classification and every configured mirror. Public remote verification requires
remote source availability and zero mirrors/overrides. Existing checkout mirror
configuration is refused before either mode begins.
All generated source, caches and dependencies live under fresh scratch. No
SwiftPM edits, inherited package configuration, sibling source imports, client
feature flags, or modified production source are used. Local Git URLs are explicit
caller inputs, not implicit sibling discovery.

The consumer uses actual public SHA256ContentDigester, LocalArtifactReferencer,
ArtifactRootCapability, ArtifactAccessIntent and typed close completion. It checks
F/Q/G/C, explicit previous/next state, repeatability, library-in-use refusal,
dimension refusal, closed-session refusal and native fatal-flag refusal. The API
snapshot crosses an owned JSON boundary only after the borrowed callback ends.
The runner reloads both reports, checks request/artifact identity and all values,
and persists/reloads another JSON copy for equality. No successful receipt is
written before these checks pass.

## State, Ownership, and Lifecycle

The caller owns scratch and its evidence. The runner never deletes it. Every
subprocess owns a new process group and bounded timeout; failure/interruption
kills and reaps the group. Runtime roots are explicitly drained, model buffers
close before the library, and staging must be empty after completion.

## Failure, Concurrency, and Constraints

Execution is serial. Unsupported hosts/toolchains, changed ABI header, dependency
pin mismatch, timeout, compilation failure, unexpected public errors, nonmatching
reports, retained staging and failed close terminate the run. Failure evidence is
written as `failure.json`; it is not a passing receipt. Default per-command timeout
is 600 seconds and is caller-selectable in [1, 3600]. Command output is written to
scratch logs rather than inherited terminal buffers.

## Verification and Change Impact

Run `verify_consumer.py --help` for inputs. Run against a committed release
candidate (published URL or an explicitly supplied local Git URL/revision), with
the exact Foundation pin and absolute Swift/clang paths. Inspect `receipt.json`,
`consumer/Package.swift`, `consumer/Package.resolved`, command logs and persisted
API/CLI reports. A build failure without client experimental flags is a consumer
contract failure; adding flags requires an explicit public compatibility decision.
Changes to public lifetime annotations, artifact contracts, package products,
ordinary CLI schema or fixture semantics require this probe again.
