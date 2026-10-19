# Standalone Native Verification

## Purpose and Scope

Parent: [swift-osdi](../../DESIGN.md). Own deterministic native test preparation
and the bounded test runner from an independent clone. The supported execution
profile is macOS 26+ arm64, Swift 6.4.0 and the pinned OSDI 0.4 header.

## Responsibilities and Boundaries

`prepare.py` compiles tracked C protocol libraries and the transport-only precision
worker. These fixtures establish API, ABI and lifetime behavior; they never stand
in for a physical model. `run.py` records tool identities, prepares inputs and runs
Swift tests. Optional `--precision` builds the real generic GMP/MPFR worker and
tracked range, chart, complete-cover and conditional-identity programs. Optional `--model-qualification` selects existing actual
model tests and requires their explicit external inputs.

Precision selection also owns the typed simulation-REAL fixture suite. It emits
the tracked three-stage programs and setup facts, compiles the existing analytic
cold-snapshot library and the scoped-given analytic fixture, and supplies the freshly built numeric worker/profile via
separate `OSDI_SIMULATION_REAL_WORKER` and
`OSDI_SIMULATION_REAL_NUMERIC_PROFILE_SHA256` selectors. The generic precision
worker/profile also becomes the global pair during ordinary model qualification.
Only explicit legacy-SOI qualification preserves the caller's global pair for
its historically bound receipt; the current SOI101 test reads its own pinned
worker/profile from its qualification input. Typed fixtures always use their
separate fresh pair.
Only completed fixture preparation and independent probe parity publish the
suite's required flag and inputs; ordinary selection clears inherited typed
selectors and does not generate these fixtures.

The old-worker compatibility test requires an actual qualified 1.1.0 worker.
Explicit `OSDI_PRECISION_OLD_WORKER` and
`OSDI_PRECISION_OLD_NUMERIC_PROFILE_SHA256` inputs retain the existing offline
artifact interface; both must be supplied together, and the receipt records the
worker's actual content hash and caller-declared numeric profile. Without that
pair, precision preparation fetches the canonical public 1.1.0 source into fresh
runner output under isolated Git configuration, verifies commit
`2c9ee1f620406e7a479ac10a4fddfaa17bc23d6d`, and invokes its existing worker builder
with the selected compiler/SDK/numeric libraries. The two builds' numeric
header/archive identities must match. Fetch, build and test deadlines apply.
The public-source old worker remains uninstrumented and is used only for the
compatibility probe; sanitizer coverage belongs to the current worker/checker.
There is no fabricated old interpreter, old/new profile substitution or skipped
compatibility test. This is a verification dependency, not a runtime backend.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [Evaluation tests](../../Tests/OSDIEvaluationTests/DESIGN.md) | used by | C protocol and transport manifest | No OpenVAF dependency for deterministic tests | Protocol fixtures do not qualify arithmetic |
| [Runtime tests](../../Tests/OSDIRuntimeTests/DESIGN.md) | coordinates with | C-only metadata preparation | Separate marker owner | Model admission is explicitly selected |
| [Precision tools](../OSDIPrecisionVerification/DESIGN.md) | depends on | Generic worker and range fixture generation | Optional numeric execution | numeric execution is selected separately from model qualification |

## Architecture

```text
tracked C sources + pinned header + C compiler -> immutable fixture manifest
  -> actor-owned test cache -> real public runtime API
fixed Swift + fixture manifest -> bounded swift test -> retained logs and receipt
optional numeric libraries -> generic real worker + range programs -> range API
explicit external model inputs -> selected model qualification -> original regressions
```

## Contracts and Invariants

Default tests need no OpenVAF executable, model download or external model receipt.
The existing SwiftPM worker product can cause a full `swift test` build to require
installed GMP/MPFR headers and libraries, even when numeric tests are unselected.
The runner installs no dependencies. Swift version must report 6.4.0 release, or
report 6.4 from a resolved `swift-6.4.0-RELEASE` toolchain path; development snapshots
and other patch versions fail admission.
Each invocation selects one macOS SDK: an explicit valid `SDKROOT`, otherwise the
bounded `xcrun --sdk macosx --show-sdk-path` result. It records the resolved path
and SDK settings hash, sets that invocation's child `SDKROOT`, and supplies the
same path to numeric worker/checker builders. Invalid explicit selections refuse
without fallback; settings must remain unchanged through execution. This local
environment does not change system configuration. In particular, external
OpenVAF linking under a Swift-toolchain PATH needs this explicit selection;
compiler exit zero without an emitted model is not fixture success. Preserve the
original linker diagnostics and require successful downstream artifact admission.
Bare `swift test` prepares C fixtures once per cache. Runner output must be fresh;
all compiler/test processes have deadlines, process-group cleanup and retained
command/output records. Nonzero exit and timeout fail the runner. Qualification
traits describe why an unselected test is skipped; selecting a qualification makes
missing inputs fail. Default mode removes inherited qualification selectors and the standalone fixture
path so unrelated shell state cannot select external qualification.

| Selector | Executed evidence | Required inputs |
|---|---|---|
| Default | C metadata, protocol and transport API behavior | C compiler |
| `OSDI_PRECISION_QUALIFICATION=1` / `--precision` | Generic range/chart/cover/conditional-identity APIs | Actual GMP/MPFR worker, numeric profile and all generated fixture directories |
| `OSDI_SIMULATION_REAL_QUALIFICATION=1` / `--precision` | Typed-REAL/raw-scoped-given point/range/chart/cover/identity, cold snapshots and actual old-worker refusal | Fresh typed fixtures/library, separately bound current worker/profile and qualified old worker/profile |
| `OSDI_MODEL_QUALIFICATION=1` / `--model-qualification` | Generated VA, current device families, compound precision and SOI 101 precision | Exact compiler/model/receipt environment inputs |
| `OSDI_LEGACY_SOI_QUALIFICATION=1` / `--legacy-soi-qualification` | Original legacy four-family precision receipt regression | Model selector plus original four-family reference, independent receipt and requests |

The legacy runner flag implies model qualification. Existing legacy assertions and
receipt schemas remain intact; the CoreSpice schema prefix does not add a dependency.

Before fixture compilation, `check_qualification_python_profile` runs the SOI 101
qualification tool with `--help` under normal Python and with `-O`. Normal mode
must exit zero; optimized mode must exit one and name `assertion-enabled Python`
in stderr. Both bounded commands retain their expected/actual exit codes and
stdout/stderr hashes in the runner receipt. This regression needs no model input
and detects removal of the guard that protects assertion-based evidence binding.

## Verification and Change Impact

Root owns compile and behavioral execution. Run `run.py --swift <swift> --clang
<clang> --output <fresh-directory>` for ordinary tests, adding `--precision` for
MPFR range evidence or `--model-qualification` for the supplied exact compiler/model
receipts. `--timeout` bounds each test command (default 600 seconds). `--configuration
debug|release`, `--jobs` and `--sanitize address` select the test build profile.
Sanitized numeric preparation uses the worker builder's address/undefined sanitizer
profile. Bare tests and the runner retain actor isolation for both cache states;
no Embedded storage or conditional synchronization is introduced. A changed
manifest requires cache decoder changes; fixture C behavior remains the authority.

## Source-Fact and 1.2.0 Typed-REAL Lane Composition

Precision selection generates range, chart, frontier and constitutive fixtures in
fresh runner scratch, sets every enabled suite's explicit fixture directory, and
records their manifests. The constitutive generator's public-probe switch adds
only the direct-probe source used by public quotient tests; its default output is
unchanged. The existing bounded C source-identity verifier runs in separate fresh
scratch without actual-model inputs. Its original C-produced direct-probe bytes
must equal the Python public-probe bytes before Swift tests run. Sanitized lanes
instrument that verifier and the worker with the existing supported profiles.
Default mode removes inherited actual-identity selectors; generic CI never selects
workspace-bound model captures. Those captures remain separate local release
evidence, with their exact source/setup/box/worker receipts.

Small Python composition tests own generator selection, environment isolation and
byte/semantic parity, typed fixture/library selection and old-worker input
admission. Their mocked dispatch tests do not replace the real native lanes.
The three existing CI lanes remain ordinary Debug,
precision Release, and precision Debug ASan with worker/C ASan+UBSan. Selecting a
lane never disables a required new suite to make it pass. These finite generic
facts establish no circuit rank, roots, forward flow or all-history guarantee.
