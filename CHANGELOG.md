# Changelog

## 1.2.1 — 2026-10-21 — source compatibility release candidate

- Align CircuiteFoundation with exact stable `26.1021.0`, resolving to
  `4d03b5a561c82a756aaf93860f657733371538dc`.
- Preserve all SDK API, worker/model source, numeric profiles, wire layouts and
  failure/ownership semantics from immutable `1.2.0`.
- Require fresh Native release gates and the independent public exact-version
  consumer. Historical model evidence keeps its original scope.
- Retain GPL-3.0-only, MIT dependency notices, upstream exceptions and source-only
  distribution. Old tags remain unchanged; no new circuit/history/corner claim.

## 1.2.0 — 2026-10-19 — source release candidate

- Add precision-program input kinds 13 (`SIM_REAL_VALUE`) and 14
  (`SIM_REAL_GIVEN`) for exact-name, REAL-only simulation-parameter lookup.
  Missing or non-REAL facts yield neutral zero/false; explicit REAL zero remains
  present. Source instructions retain authority for defaults and fatal conditions.
- Add kind 15 (`RAW_PARAMETER_GIVEN`) for exact own-scope assignment presence.
  Preserve kind 2 instance-or-model presence; neither explicit zero nor equal
  resolved values erase the original source distinction.
- Require a worker supporting these new kinds and an explicitly bound new numeric
  profile, producer semantic identity and model/program artifact. Older workers
  refuse the new grammar. Existing kinds 8/12, Program ABI 1, header/setup layouts
  and protocol 1–5 wire bytes retain their semantics and layout.
- Check new REAL coefficients from immutable setup bits independently of MPFR
  endpoint values. Retain the existing cumulative resource, cancellation and
  cleanup owners across all five precision operations.
- Add caller-controlled snapshot construction with fixed program/setup buffers,
  cumulative work and managed-payload admission, cancellation and shared immutable
  program/session leases. ASCII parameter, alias and simulation names are explicit
  in this profile; string values preserve exact UTF-8 bytes. Legacy capture stays
  available, and an unsupported controlled implementation refuses without falling
  back to it. External byte-array copies remain caller-owned; this contract does
  not bound allocator overhead, cryptographic internals or process RSS.
- Initial kinds 13/14 qualification passed 38 affected native Release tests without
  skips and seven independent
  reference comparisons. Combined Address/Undefined Behavior Sanitizer evidence
  covers the C checker, new bindings and four retained original-model conditional
  identity captures. Of 38 sanitizer-worker tests, 37 passed together and one
  existing 200-ms startup-bound test passed separately with the same bound; the
  concurrent timeout remains recorded, rather than claimed as a parallel pass.
  The final transport fixture isolates the intentionally unresponsive startup
  deadline from other IPC failures; production timeout policies are unchanged.
  Final combined qualification additionally passed the controlled-snapshot and
  typed-binding paths in Release, Debug and Address Sanitizer lanes: 73 actual
  tests passed per fresh Debug/ASan lane, with two explicit existing model gates
  skipped. A separate selected test passed the four current original-model
  conditional-identity joins. All five controlled-snapshot tests and eight typed
  binding tests executed. Independent kind-15 checking covered four positive
  cases, four malformed programs and actual old-worker refusal.

The new binding fixtures and cold snapshot exercise SDK semantics, not a completed
Official C three-stage producer. The four-model evidence retains its original
finite capture/condition scope. This release adds no circuit-root, flow/history,
foundry or performance guarantee. Distribution remains source-only; publication
requires the verified tag, public exact-version consumer and source archive.

## 1.1.0 — 2026-10-19 — source release candidate

- Add bounded source guard/chart facts, independently checked complete source
  decision covers, and exact source-linear F/Q identities with explicit immutable
  coordinate-quotient conditions through protocol 5. Older protocol bytes and
  operation semantics remain unchanged.
- Preserve scoped borrowed results, cumulative work/storage admission, typed
  refusal, cancellation and explicit worker/source cleanup.
- Generate all required precision fixtures in fresh standalone lanes and compare
  public probe bytes with the original independent C fixture recipe. Require a
  separate public-import snapshot/identity consumer for release and CI.
- Retain macOS 26+ arm64, Swift 6.4.0, OSDI 0.4, exact CircuiteFoundation
  `26.1018.0`, GPL-3.0-only and source-only distribution.

These finite model-local source facts do not establish circuit rank, roots,
forward flow, accepted history, all-phase/all-corner guarantees or foundry
qualification. Publication is established by the verified remote tag and release.

## 1.0.1 — 2026-10-18

- Replace the CircuiteFoundation revision requirement with exact stable version
  `26.1018.0`, retaining resolved commit
  `5f132c58209123433e0e479e1ea61096e46061ba` and its unchanged implementation.
- Require an independent downstream SwiftPM `exact: "1.0.1"` resolution check.
  Stable SDK dependencies must use stable version requirements, with the resolved
  graph retaining exact commit identities.
- Supersede 1.0.0, whose revision-based dependency makes downstream stable
  `exact: "1.0.0"` resolution fail. Its immutable tag remains and its GitHub release
  is retained as a prerelease; it is not the supported stable-consumption target.

The numerical API, finite qualification scope, licenses and source-only
asset policy remain unchanged. This entry prepares the corrected version;
publication is established by the verified remote tag and release.

## 1.0.0 — 2026-10-18 — superseded prerelease

- Package the OSDI 0.4 native consumer for macOS 26+ arm64 and Swift 6.4.0.
- Expose exact artifact admission, descriptor/storage ownership and explicit-context
  point preparation/evaluation through public products and JSON CLIs.
- Keep optional stateless precision v1 and smooth-box v2 behind explicit exact
  worker/source/profile bindings, caller budgets and typed refusal contracts.
- Add standalone deterministic verification, a separate public-product consumer,
  source-only release instructions and bounded CI lanes.
- Separate generic verification from optional exact-input real-model qualification.
- Distribute SDK source under GPL-3.0-only with an exact MIT-licensed
  CircuiteFoundation dependency; retain upstream notices and the compiler fixture's
  OpenVAFSupport Attribution License 1.0 support-material exception.

This version provides model-local results. It does not claim circuit solution,
accepted-history guarantees, complete operating/corner coverage, foundry
qualification or a new performance improvement. Publication requires the gates in
[RELEASING.md](RELEASING.md). This entry records version contents and the intended
source-release date; publication is established by the verified remote tag and
release, not this heading.
