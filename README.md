# swift-osdi

A Swift consumer of **OSDI 0.4** model libraries, with an ordinary native point
API and separate, explicitly qualified precision profiles. Verilog-A physics and
model compilation remain owned by the model source and external compiler.

```text
Trusted Verilog-A source -> external OpenVAF -> exact native model artifact
                                                   |
ArtifactAccessing + exact identity + caller budgets -> OSDIRuntime -> COSDI
                                                   |                 |
                                                   |          ordinary point values
                                                   |
                         qualified program + exact worker/profile
                                                   |
                                    optional precision child process
```

## Supported scope

The executable profile is **macOS 26 or later, Apple arm64, Swift 6.4.0 release**,
and thin Mach-O OSDI 0.4 libraries. Native model artifacts and process workers are
not WASM or Embedded artifacts. Portable contract values do not imply a portable
native runtime.

| Product | Responsibility |
|---|---|
| `OSDIContracts` | Immutable metadata, preparation, contexts, budgets and typed failures |
| `OSDIRuntime` | Exact artifact loading, library lifetime, stable storage, point and optional precision sessions |
| `COSDI` | Pinned upstream ABI and native loading/evaluation/process boundary |
| `osdi-inspect` | Descriptor inspection and optional zeroed-storage verification |
| `osdi-evaluate` | Preparation, ordinary point evaluation and explicit precision JSON requests/reports |
| `osdi-precision-worker` | Optional GMP/MPFR arithmetic worker; requires separately qualified identity/profile |

| Profile | Result and caller obligation |
|---|---|
| Ordinary OSDI point | Prepared topology/parameters; local F/Q/G/C, candidate next state, source noise densities and timestep advice in explicit supported contexts |
| `stateless-precision-v1` | Qualified source-program F and Delta-Q enclosures at explicit high/low endpoints; G/C are approximate Newton aids |
| `stateless-smooth-box-v2` | Qualified smooth-box outward F/Q and spatial G/C bounds; unsupported nonsmooth faces fail explicitly |

The caller supplies every active unknown, including internal variables, and every
previous state. The caller owns circuit assembly/solution, integration, accepted
history and rejected-step restoration. The precision profiles require stateless
source programs and exact semantic/numeric qualification inputs. Their finite
point/box results do not establish circuit trajectories, a complete operating
cover, time partials, all-history/all-phase/all-corner guarantees or foundry
qualification. This release makes no new performance claim.

Load only **trusted ABI-conforming native binaries**. Loading runs native
constructors and foreign functions. Digest verification establishes exact bytes,
not publisher trust, a malicious-code sandbox or physical correctness. An
in-process model call cannot be forcibly interrupted; use an outer process
deadline for unbounded native code.

## Build from a standalone checkout

Install the exact [Swift 6.4.0 release toolchain](https://www.swift.org/install/macos/)
and an Xcode/macOS SDK supporting macOS 26. Use the
[official signed package installation instructions](https://www.swift.org/install/macos/package_installer/),
then select its absolute executable:

```sh
SWIFT="$HOME/Library/Developer/Toolchains/swift-6.4.0-RELEASE.xctoolchain/usr/bin/swift"
"$SWIFT" --version
```

The package resolves its revision-pinned CircuiteFoundation dependency from its
public Git URL. No sibling workspace checkout, SwiftPM mirror or local override
is required. Release admission must verify the selected revision from a fresh
checkout; see [RELEASING.md](RELEASING.md).

Python 3 and Apple Clang are required by the tracked verification tools. Ordinary
runtime products do not link GMP or MPFR. Building all package products, or the
precision worker, additionally requires installed **GMP, MPFR and pkg-config**,
with their `.pc` files discoverable through `PKG_CONFIG_PATH` when necessary.
For a Homebrew environment, install those prerequisites explicitly:

```sh
brew install gmp mpfr pkgconf
pkg-config --modversion gmp mpfr
```

The independent precision worker builder requires `libgmp.a` and `libmpfr.a` as
well as headers; it records their identities and refuses missing archives. Other
installed prefixes can be selected with `--gmp-prefix`, `--mpfr-prefix` and
`--pkg-config`. Ordinary library/CLI users need no OpenVAF compiler at runtime.

Use a process-group deadline around ad hoc commands:

```sh
bounded() {
  python3 - "$@" <<'PYTIMEOUT'
import os
import signal
import subprocess
import sys
child = subprocess.Popen(sys.argv[2:], start_new_session=True)
try:
    code = child.wait(timeout=float(sys.argv[1]))
except BaseException:
    try:
        os.killpg(child.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    child.wait(timeout=5)
    raise
raise SystemExit(code)
PYTIMEOUT
}
bounded 600 "$SWIFT" build --product osdi-inspect --jobs 2
bounded 600 "$SWIFT" build --product osdi-evaluate --jobs 2
```

## Verification lanes

The current all-target SwiftPM test build also requires installed GMP/MPFR and
pkg-config, even when the precision qualification lane is not selected. The
[tracked runner](Tools/OSDIStandaloneVerification/DESIGN.md) starts from C
fixtures and the production API/CLI. Every child
process has a deadline, and each run uses a fresh output directory:

```sh
python3 Tools/OSDIStandaloneVerification/run.py \
  --swift "$SWIFT" --clang /usr/bin/clang \
  --output /absolute/path/to/new/ordinary-run --configuration debug --timeout-seconds 600
python3 Tools/OSDIStandaloneVerification/run.py \
  --swift "$SWIFT" --clang /usr/bin/clang \
  --output /absolute/path/to/new/precision-run --configuration release \
  --precision --timeout-seconds 600
```

| Lane | Evidence | External inputs |
|---|---|---|
| Ordinary | Admission, lifecycle, point API/CLI and precision transport contracts using tracked C fixtures | Swift, SDK, Clang, Python; GMP/MPFR/pkg-config for the test build |
| Optional deterministic precision | Actual numeric worker and tracked source-program/range fixtures | Ordinary prerequisites plus GMP/MPFR static archives and pkg-config |
| Model qualification | Exact compiler/model/source/reference comparisons | Explicit retained model artifacts and qualification receipts |
| External consumer | A separate package using only public products; exact persisted API/CLI comparison | Public source and dependency URL/revision bindings |

An ordinary/deterministic pass is not a real-model qualification pass. The runner
reports which lane ran; model-dependent Swift tests are explicitly disabled
unless model qualification is selected. Selecting model qualification requires
all of its declared inputs and missing evidence fails setup. CI does not download
proprietary models, use credentials or silently replace missing model evidence.

The [external consumer tool](Tools/OSDIConsumerVerification/DESIGN.md) performs a
fresh source/dependency resolution with explicit URL and revision inputs; its
`--help` documents the exact invocation. It requires no experimental Swift flags
in the consuming package. The source and dependency licenses must permit that
use.

## Public API

Depend on the public `OSDIContracts` and `OSDIRuntime` products. The main seams are
`OSDILibraryLoading`, `OSDIModelPreparing`, `OSDIModelEvaluating`,
`OSDIPrecisionSessionPreparing`/`OSDIPrecisionEvaluating`, and the separate range
preparation/evaluation protocols. Artifact access is injected through
CircuiteFoundation's `ArtifactAccessing`; runtime verifies the expected content
identity before loading staged bytes.

```text
exact artifact reference + source + read budget + load limits
  -> OSDINativeLibraryLoader.load
  -> OSDINativeModelPreparer.prepare(options)
  -> immutable topology / resolved parameters + exclusive prepared model
  -> OSDINativeModelEvaluator.evaluate(context, unknowns, previousState)
  -> synchronous scoped output view
  -> close prepared model -> close library/source owners
```

Once a library has been loaded, a point consumer can prepare and evaluate with
caller-owned options and complete vectors:

```swift
import OSDIContracts
import OSDIRuntime

func residuals(
    library: borrowing OSDILibrary,
    options: OSDIPreparationOptions,
    context: OSDIEvaluationContext,
    unknowns: [Double],
    previousState: [Double]
) throws -> [Double] {
    var model = try OSDINativeModelPreparer().prepare(
        library: library, descriptorIndex: 0, options: options)
    defer { model.close() }
    return try OSDINativeModelEvaluator().evaluate(
        model: &model, context: context, unknowns: unknowns.span,
        previousState: previousState.span
    ) { view in
        var values: [Double] = []
        values.reserveCapacity(view.f.count)
        for index in 0..<view.f.count { values.append(view.f[index]) }
        return values
    }
}
```

The output spans borrow prepared storage and cannot escape the callback. The
copy above is an explicit retained-output boundary. Noncopyable prepared models
and precision sessions own exclusive mutation; closing a library with live
leases fails. Fallible source/library/precision cleanup remains observable and
retryable through its retained cleanup owner. See the executable
[consumer source](Tools/OSDIConsumerVerification/ConsumerEntry.swift)
for complete artifact binding, API use and failure handling.

## JSON CLI

```sh
bounded 10 .build/debug/osdi-inspect --request request.json --output report.json
bounded 10 .build/debug/osdi-evaluate --request point-request.json --output point-report.json
bounded 10 .build/debug/osdi-evaluate --precision-request precision-request.json --output precision-report.json
```

Check both process exit status and report `succeeded`. Reports retain exact
request/artifact identities, stage-specific outputs and structured failure
facts. A preparation success means setup only. Evaluation reports are atomically
saved and strictly reloaded before success is reported.

All ordinary requests bind these required fields:

| Field | Contract |
|---|---|
| `libraryPath`, `expectedSHA256`, `expectedByteCount` | Trusted native image path and exact lowercase SHA-256/byte count |
| `limits` | Explicit `maximumDescriptors`, `maximumMetadataBytes`, `maximumStorageBytes` |
| `readBudget` | Explicit `maximumPageByteCount`, `maximumTotalByteCount`, `maximumPageCount`, `maximumWorkUnitCount`, `maximumDurationNanoseconds` |

Inspection uses schema `1` and `verifyStorage`. Its storage limit covers admitted
per-descriptor model/instance storage and advertised Jacobian capacities, not
aggregate process/image memory.

Ordinary evaluation adds:

| Field | Contract |
|---|---|
| `operation` | `prepare` or `evaluate` |
| `descriptorIndex` | Zero-based descriptor index |
| `parameters` | Name, model/instance scope and typed real/integer/string value |
| `simulationParameters` | Explicit name and typed real/string value |
| `temperatureKelvin`, `connectedTerminalCount` | Finite positive Kelvin and connected terminal count |
| `evaluationLimits` | Explicit workspace, diagnostic count/bytes and string byte limits |
| `localUnknowns`, `previousState` | Both required for evaluate; complete prepared-order vectors |

Schema `1` selects `profile: dc-static-point`. Schema `2` selects
`profile: dynamic-point` and requires an explicit `context` with `mode`,
`timeSeconds`, and `noiseFrequencyHz` for noise. Supported contexts expose DC, AC,
transient, transient initialization and fixed-bias source-noise evaluation;
source noise is not output-circuit noise, Pnoise or stochastic transient noise.

Prepare first to discover raw-to-active/ground mappings, internal unknowns,
parameter values and state length. Generated setup owns defaults and given flags;
the CLI does not invent missing simulation parameters. Changes to parameters or
temperature can change the prepared topology. Array parameter assignments are an
explicit unsupported capability.

F/Q have raw node length N and G/C are raw row-major N by N matrices, including
collapsed raw nodes. `nextState` is a candidate, with no implicit acceptance.
Workspace limits exclude mapped libraries, Swift/report serialization and total
RSS. Ordinary JSON canonicalizes signed zero; API comparisons preserve its bits.

## Optional precision and model qualification

Precision requests use `--precision-request`. Schema `1` profile
`stateless-precision-v1` selects `snapshot` or `evaluate`. Schema `2` profile
`stateless-smooth-box-v2` selects `enclose`, with lower/upper normalized high/low
vectors and same-mode endpoint contexts. Vector JSON carries both `values` and
matching binary64 `bitPatterns`; time contexts retain component bits.

Both profiles bind exact model, program/setup, worker, semantic profile and
numeric profile identities plus explicit precision, memory, work, startup,
evaluation, cleanup and report budgets. A profile digest is a caller-declared
qualification assumption; runtime artifact admission alone does not establish
its mathematical validity. Unsupported state/opcode/nonsmooth input, exhaustion,
cancellation and child failure are explicit failures with no partial success or
silent ordinary-point fallback.

Use the tracked request generation and verification tools:

```sh
bounded 30 python3 Tools/OSDIPrecisionVerification/build_worker.py --help
bounded 30 python3 Tools/OSDIPrecisionVerification/verify_cli.py --help
bounded 30 python3 Tools/OSDIPrecisionVerification/verify_range.py --help
bounded 30 python3 Tools/OSDIPrecisionVerification/verify_models.py --help
bounded 30 python3 Tools/OSDIPrecisionVerification/verify_soi101.py --help
bounded 30 python3 Tools/OSDIPrecisionVerification/independent_replay.py --help
```

The pinned upstream compiler/header authority is recorded in
[Source.json](ThirdParty/OpenVAF-Reloaded/Source.json). Ordinary numerical model
qualification requires a source-faithful full-residuals compiler profile and
exact model source/include/compiler/artifact receipts, beyond ABI compatibility.
Do not substitute an arbitrary ABI-compatible compiler output for that profile.
The source-only [compiler fixture](Tools/OpenVAFCompilerFixture/DESIGN.md)
retains an exact upstream patch, producer driver, semantic profile and provenance
for explicitly reproducing this qualification lane. It is not a product runtime
dependency. Its adapted profile has a new identity; historical compiler receipts
remain bound to their original profile and do not certify a new build.

| Retained model profile | Scope |
|---|---|
| BSIM-BULK 107.2.1 | Finite ordinary/precision model cases with exact retained inputs |
| BSIM-CMG 112.1.0 | Same bounded point/program scope |
| BSIM-IMG 103.1.0 | Same bounded point/program scope |
| BSIM-SOI 101.0.0 | Latest retained six-terminal `BODYMOD` profile; old SOI4.7 remains regression-only |

`verify_models.py --manifest` accepts explicit exact-bound programs/setup and
caller vectors/contexts. `verify_soi101.py` builds the package-owned independent
replay interpreter and runs its controls. `independent_replay.py` provides separate build, control
and compare commands with exact build provenance; no sibling checkout is
required. Model-dependent Swift suites require
the compiler, library, digest, current-family capture and precision inputs
listed in the [runner contract](Tools/OSDIStandaloneVerification/DESIGN.md) and
reported by setup diagnostics. `--legacy-soi-qualification` separately selects
the original four-family receipt regression and implies model qualification.
Retain those exact inputs and successful receipts; generated or failed checks are not qualification evidence. Historical
receipts only apply while their bound source/profile assumptions remain valid.

See [RELEASING.md](RELEASING.md) for the source-only release gate,
[CHANGELOG.md](CHANGELOG.md) for candidate changes, and [DESIGN.md](DESIGN.md) for
contract ownership. Third-party license text and exact header provenance are
retained under [ThirdParty/OpenVAF-Reloaded](ThirdParty/OpenVAF-Reloaded).
