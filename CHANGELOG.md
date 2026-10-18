# Changelog

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
