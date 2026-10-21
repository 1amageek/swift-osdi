# Source release procedure

The current working source contains next-major compatibility changes: the public
COSDI product is removed, precision arithmetic is part of its executable target,
and CLI entrypoints are `osdi inspect`, `osdi evaluate` and `osdi precision`.
The 1.2.0 release record and checklist below remain historical authority for that
immutable release. A future release must select a new major version and freshly
qualify the changed source, CLI and exact worker/profile; this task creates no tag,
release or deployment.

The source release is prepared as **1.2.0**, dated **2026-10-19**. This
document describes admission and manual publication; prepared version metadata
does not mean a tag or release has been published.

```text
exact source + stable dependency version/resolved commit + license approval
  -> fresh Debug / Release / sanitizer verification
  -> independent downstream exact-version consumer
  -> bound model qualification evidence for any model claims
  -> human review of source tag and notes
  -> explicitly authorized tag / source-only GitHub release
```

## Release contract

| Item | Required scope |
|---|---|
| Host/toolchain | macOS 26+, arm64, exact Swift 6.4.0 release and recorded SDK/Clang |
| ABI | OSDI 0.4, trusted thin Mach-O native libraries |
| Ordinary API | Exact artifact admission, lifecycle, setup, explicit-context point F/Q/G/C and candidate state/noise/timestep facts |
| Precision | Explicit point v1, smooth box v2, guard/chart v3, complete source decision cover v4 and conditional source-linear identity v5; exact program/setup/topology, worker, semantic and numeric bindings |
| Typed REAL extension | Input kinds 13/14/15 require a supporting worker, new numeric profile and producer semantic/model/program identity; kinds 8/12, Program ABI 1, header/setup layouts and protocol 1–5 wire layouts remain unchanged |
| Controlled snapshot | Additive ASCII-name profile with exact UTF-8 string values, fixed buffers, caller-owned cumulative work/managed-payload admission and cancellation; program/session copies share the retained lease, while external byte-array copies require caller admission |
| Excluded claims | Circuit solution, accepted history, complete operating cover, all-history/all-corner guarantees, physical/foundry qualification and new performance claims |
| Distribution | Source tag/archive only; no compiled CLI, worker, model library or numeric archive assets |
| SDK license | GPL-3.0-only under the root LICENSE |
| Dependency license | MIT-licensed CircuiteFoundation at exact stable version `26.1018.0`, resolved to `5f132c58209123433e0e479e1ea61096e46061ba` |
| Retained exceptions | OpenVAFCompilerFixture support materials retain OpenVAFSupport Attribution License 1.0; upstream-derived patch material retains upstream GPL terms |

The [README](README.md) owns user-facing setup and API/CLI use. The
[system design](DESIGN.md) owns implementation contracts; this procedure owns the
release admission sequence.

The [controlled snapshot contract](Sources/OSDIRuntime/SnapshotConstruction/DESIGN.md)
defines this new capture profile. It preserves the legacy overload and explicitly
refuses an unsupported controlled implementation instead of using unaccounted
legacy capture. Preparation identity compares original UTF-8 bytes; Unicode
canonical equivalence does not replace byte identity. Its resource accounting
covers managed initialized payload and required simultaneous scratch, not allocator
rounding/metadata, stack, cryptographic backend internals or hard process RSS.

## 1.2.0 qualification boundary

The initial kinds 13/14 local affected-owner qualification passed 38 native Release tests without
skips and seven independent reference comparisons. New typed-REAL fixtures cover
all three stages and five operations, exact immutable setup-bit checking, missing
and wrong-type lookup, explicit-zero fatal behavior, old-worker refusal, cold
snapshot fidelity and cancellation. The four retained original-model captures
were replayed through the new worker and independent conditional-identity checker
with their original program/setup/source/condition identities.

Combined Address/Undefined Behavior Sanitizer C qualification passed the new
fixtures, four retained model captures, work/capacity refusal and cleanup paths.
The sanitizer-worker Swift evidence comprises 37 tests passing together and the
remaining existing 200-ms startup-bound test passing in isolation with the same
limit. Preserve the concurrent timeout receipt. This is scoped sanitizer coverage,
not successful execution of the entire concurrent lane or a performance claim.

These results do not qualify a completed Official C three-stage producer, circuit
roots, forward flow, accepted history or foundry models. Existing 1.1.0 tags and
historical receipts remain immutable. Generic CI and public-consumer checks must
be reported separately from these explicitly selected local fixtures. Final
candidate admission has separately qualified the raw own-scope kind 15,
controlled snapshots and mandatory generated-fixture selection. The final fresh
Debug/ASan lanes each passed 73 actual tests with two existing model gates skipped;
a separately selected test passed the current four-family identity joins. All
five controlled-snapshot and eight typed-binding tests executed. Kind 15 also
passed four independent positive cases, four malformed cases and actual old-worker
refusal. These results are recorded in
`.verification/controlled-snapshot-120-20261019/combined-r3/receipt.json` and the
sibling `actual-four-focused-r1/receipt.json`, with the independent kind-15 proof retained
under `combined-r1/kind15-proof-final-r3/receipt.json`. The old parallel and isolated
startup failures remain evidence; the corrected fixture isolates its intentional
200-ms startup hang from other IPC failures without changing product deadlines.
Three core controlled-snapshot tests are ungated and run
in generic CI. Two additional model-gated tests require the exact original
four-family request captures and compiler precision fixture. Central qualification
must execute all five with their real input provenance; generic CI skips of the
model gates do not qualify those paths or permit fabricated model inputs.

## Admission checklist

1. Confirm repository owner, branch, exact source commit and proposed `1.2.0` tag.
   Inspect existing tags/remotes and pending changes. Never overwrite a tag or
   rewrite history as part of this procedure.
2. Confirm the root GPL-3.0-only LICENSE and the MIT permission at the exact
   CircuiteFoundation revision. Preserve its copyright/license notice, the
   unmodified upstream OSDI header/license/notice and exact third-party provenance.
   Retain the complete OpenVAFCompilerFixture NOTICE, including OpenVAFSupport
   Attribution License 1.0 for support materials and visible attribution. Its
   upstream-derived patch material keeps its upstream GPL terms. A missing or
   unresolved permission blocks publication. Preserve applicable notices and
   corresponding source in the source archive.
3. Confirm the SDK manifest uses a public dependency URL and exact published
   stable version `26.1018.0`. Verify that version tag and Package.resolved both
   bind CircuiteFoundation to commit `5f132c58209123433e0e479e1ea61096e46061ba`.
   Resolve the complete stable version graph from a fresh clone with isolated
   SwiftPM state, no workspace sibling, local mirror or `.package(path:)`
   dependency. Record every resolved version and commit. A revision-based direct
   dependency is not an acceptable substitute for this stable-release contract.
4. Run the standalone tool from the README in fresh Debug and Release output
   directories: ordinary Debug, deterministic precision Release, and precision
   Debug with Address Sanitizer. Precision lanes freshly generate every required
   range/chart/cover/identity fixture and require exact public-probe parity with
   the independent C driver. Run its supported Address Sanitizer lane once for affected native consumer paths and worker
   Address/Undefined Behavior Sanitizers where supported. Preserve the scope limit
   for noninstrumented numeric libraries or external model code. Existing behavioral
   evidence may be reused for a license-only dependency revision after confirming
   exact non-license source equality and unchanged verification assumptions.
   Preserve the original tested revision and the equality evidence; do not report
   reused results as newly executed tests of the new revision.
5. Run [the external consumer](Tools/OSDIConsumerVerification/verify_consumer.py)
   against the public candidate source commit, with the canonical public
   CircuiteFoundation exact version and resolved commit bindings. It must resolve and build independently,
   exercise API and CLI success/failure, preserve exact report values after reload,
   and close owners. Require `--precision` to exercise the public snapshot-to-v5
   conditional identity path, wrong-operation/limit refusals and cleanup. A CI `file://` source clone tests package consumption only;
   it does not prove the release revision is publicly fetchable. After the reviewed
   source tag exists, the separate public remote run must use
   `--source-version 1.2.0` and the exact tagged `--source-revision`, producing the downstream
   `exact: "1.2.0"` requirement. A revision-only candidate check does not replace it.
6. For model claims, retain exact source/include/compiler/model/program/setup,
   numeric profile, worker and independent reference identities with successful
   receipts. Record selected and excluded families/cases explicitly. Missing,
   disabled or failed model checks do not become qualification successes. The four
   retained actual-model protocol-5 joins are separate local release evidence;
   generic CI fixtures do not replace them. Reuse
   prior evidence only if its bound source and assumptions remain applicable.
   The controlled-snapshot model gates additionally require the explicit
   `OSDI_REAL_PRECISION_REQUEST_DIRECTORY` four-family captures and the exact
   `OSDI_PRECISION_MODEL` / `OSDI_PRECISION_SEMANTIC_SHA256` fixture bindings.
   Pin the actual request files and referenced artifacts before/after execution,
   and report their results separately from the three ungated construction tests.
7. Review the complete candidate diff once against the declared contracts and
   update [CHANGELOG.md](CHANGELOG.md). Confirm source archives contain no secrets,
   model/compiler binaries, local absolute paths as configuration, or generated
   qualification working directories. Build/test receipts may retain their actual
   execution paths as provenance.
8. Commit the coherent source/docs change, confirm commit message and branch,
   then verify the final integrated source snapshot. A passing CI run is useful
   evidence; its generic lanes do not supply optional external-model evidence.
9. Present the exact tag, source commit, release title/notes, source-only assets
   and remaining qualifications. Retain publication authorization already given
   by the repository owner; obtain it only when absent. Tag creation/push and GitHub
   release publication are separate from routine commits.
10. After authorization, publish the reviewed source tag. Verify its exact commit,
    run the fresh public `exact: "1.2.0"` consumer, and inspect the tagged source
    archive before creating the source-only GitHub release. Then verify that the
    remote tag still resolves to the reviewed source commit and the published
    assets match the source-only contract. Record the actual publication result.

## Superseded 1.0.0

The 1.0.0 source tag is immutable and remains available, with its GitHub release
marked prerelease. It declared CircuiteFoundation by Git revision; SwiftPM rejects
that dependency when resolving a downstream stable `exact: "1.0.0"` requirement.
Direct revision-based smoke consumption did not establish stable-version package
compatibility. Version 1.0.1 changes the dependency requirement to exact stable
version `26.1018.0`, whose tag and resolved graph bind the same Foundation commit.
Do not move the old tag or reuse its package-resolution result as evidence for the
corrected stable-version path. The 1.0.1 release must pass that concrete path.

## Reproducible evidence

Keep the tool-generated receipts and logs outside tracked source. A release
summary should link immutable evidence and report the following separate outcomes:

| Evidence | Required identity or outcome |
|---|---|
| Source/dependencies | Exact source commit, stable manifest version requirements, resolved version/commit graph and fresh downstream exact-version remote resolution |
| Build/tests | Toolchain/SDK/compiler, Debug/Release results, test lanes selected/disabled, command timeouts |
| Native sanitizers | Exact instrumented paths, pass/failure and noninstrumented scope |
| External consumer | Public URL/versions/resolved commits, downstream SDK exact-version resolution, client source, API/CLI result and persistence comparison |
| Optional models | Exact family/version/case manifests and independent reference receipts |
| License/distribution | Root GPL-3.0-only, exact dependency MIT notice, retained compiler-fixture attribution/upstream terms and source-only asset inventory |

The compact profile manifest in
[.github/qualification-profiles.json](.github/qualification-profiles.json)
records supported lanes and their obligations. It is an index, not a generated
success receipt or a substitute for exact per-run evidence.

## CI policy and maintenance

[CI](.github/workflows/ci.yml) runs bounded standalone verification and external
consumer checks on `macos-26` arm64, with `contents: read`, no deployment token and
no publishing job. Main-branch source changes, pull requests and manual dispatch
run qualification; progress-ledger-only pushes and tag pushes do not repeat an
already qualified source build. It installs the exact signed Swift release in the runner's
user directory and verifies compiler identity before use. Generated receipts and
logs are CI evidence; compiled binaries are not release assets.

The runner label, installation process and action revisions were checked against
these primary sources on 2026-10-18:

- [GitHub runner-images supported labels](https://github.com/actions/runner-images#available-images)
- [Swift macOS downloads](https://www.swift.org/install/macos/)
- [Swift package installation and signing identity](https://www.swift.org/install/macos/package_installer/)
- [actions/checkout v7.0.1](https://github.com/actions/checkout/releases/tag/v7.0.1)
- [actions/upload-artifact v7.0.2](https://github.com/actions/upload-artifact/releases/tag/v7.0.2)

Recheck these authorities when updating CI. Pin action revisions and the Swift
release; refuse an unexpected host/toolchain rather than selecting a snapshot or
older Swift version to make CI pass.

## Binary distribution is a separate decision

Building locally is supported. Shipping a compiled worker/CLI/model package is
outside this first source release. Before any binary distribution, review all
linked code and numeric-library licenses, provide applicable notices and license
texts, and satisfy corresponding-source/relinking obligations for the actual
static/dynamic link configuration. Do not infer that source-release approval
covers binary packaging.
