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

## Stable 1.1.0 Source-Fact Lane Composition

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
byte/semantic parity. The three existing CI lanes remain ordinary Debug,
precision Release, and precision Debug ASan with worker/C ASan+UBSan. Selecting a
lane never disables a required new suite to make it pass. These finite generic
facts establish no circuit rank, roots, forward flow or all-history guarantee.
