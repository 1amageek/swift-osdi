# swift-osdi

A native Swift consumer of the existing open-source **OSDI 0.4** ABI and
OpenVAF-Reloaded model libraries. The upstream header is vendored unchanged;
Swift does not reimplement Verilog-A device physics or the compiler.

```text
Official Verilog-A source -> external OpenVAF-Reloaded -> native .osdi artifact
                                                        |
Exact artifact identity + caller budgets -> OSDIRuntime -> COSDI / system loader
                                                        -> metadata + stable storage
```

## S1 scope

| Product | Responsibility |
|---|---|
| `COSDI` | Pinned upstream ABI, native image/descriptor admission, zeroed allocation |
| `OSDIContracts` | Portable immutable metadata and caller-selected limits |
| `OSDIRuntime` | Bounded artifact acquisition, exact digest/size verification, library lifetime and storage ownership |
| `osdi-inspect` | JSON request/report CLI through the public runtime path |

The executable profile is **macOS 26+, arm64, thin Mach-O native libraries** and
Swift **6.4.0 release**. Other host/architecture profiles are refused. Native
artifacts are not WASM or Embedded Swift artifacts.

Load only **trusted, ABI-conforming native binaries**: `dlopen` runs constructors
and native pointers/functions are foreign code. Admission validation is not a
malicious-code sandbox. Artifact digest verification establishes exact content,
not publisher trust or physical correctness.

S1 inspects descriptors and allocates stable, zeroed model/instance storage. It
**does not call model/instance setup, evaluate equations, stamp a circuit, solve
DC/AC/transient/noise analyses, or prove interval/all-phase/all-history bounds**.
Successful inspection does not qualify a device model or foundry process.

## Build and test

Select the exact installed Swift 6.4.0 release toolchain. Define a Python-backed
process timeout helper, then build:

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
SWIFT="$HOME/Library/Developer/Toolchains/swift-6.4.0-RELEASE.xctoolchain/usr/bin/swift"
"$SWIFT" --version
bounded 300 "$SWIFT" build --build-tests --jobs 2
```

The package pins CircuiteFoundation by revision. Runtime loading requires neither
an installed compiler nor `OSDI_OPENVAF_COMPILER`.

The Swift Testing suite additionally compiles real Verilog-A fixtures. Install or
prepare a trusted macOS arm64 OpenVAF-Reloaded compiler separately (the pinned fixture profile uses
`v24.0.2mob`, source commit `fdf2522b70f42793f64b1c72f0195c96dea0cc19`) and set its
absolute executable path **only for fixture preparation/tests**:

```sh
OSDI_OPENVAF_COMPILER=/absolute/path/to/openvaf-r \
  bounded 120 "$SWIFT" test --skip-build --disable-xctest --enable-swift-testing \
    --filter OSDIRuntimeTests
```

Missing compiler configuration fails test setup. The fixtures do not silently
substitute hand-authored metadata for a real compiled model. The S1 runtime
suites share one actor-owned preparation task; S2 evaluation suites use their own
actor-owned task and explicit compiler/model inputs below. Fresh output directories
isolate lifecycle markers. To prepare fixtures independently:

```sh
FIXTURES="$(mktemp -d "${TMPDIR:-/tmp}/swift-osdi-fixtures-XXXXXX")"
bounded 120 python3 Tools/OSDIVerification/build.py \
  --output "$FIXTURES" --clang /usr/bin/clang \
  --header Sources/COSDI/ABI/osdi_0_4.h \
  --openvaf /absolute/path/to/openvaf-r
```

`manifest.json` records compiler/source/header/output SHA-256 values and commands.
The builder bounds each compiler/linker process and cleans its process group on
interruption. It refuses an output directory containing an existing manifest.
The `bounded` helper enforces a process deadline and kills its process group on
interruption. Add the built executable directory to `PATH`, or invoke its absolute
path from the build output.

## CLI

```sh
osdi-inspect --request request.json --output report.json
```

The request uses schema version `1`. All fields below are required; budgets are
explicit caller policy rather than runtime defaults.

| Field | Meaning |
|---|---|
| `schemaVersion` | `1` |
| `libraryPath` | Local native artifact path |
| `expectedSHA256` | Expected exact lowercase SHA-256 hexadecimal digest |
| `expectedByteCount` | Expected exact byte count |
| `limits.maximumDescriptors` | Maximum admitted descriptor count |
| `limits.maximumMetadataBytes` | Maximum inspected descriptor/table/string bytes |
| `limits.maximumStorageBytes` | Per-descriptor model/instance bytes plus advertised Jacobian double-array capacities |
| `readBudget.maximumPageByteCount` | Maximum bytes per read page; no greater than total budget |
| `readBudget.maximumTotalByteCount` | Maximum source artifact bytes; must cover the expected artifact |
| `readBudget.maximumPageCount` | Maximum read pages |
| `readBudget.maximumWorkUnitCount` | Maximum digest preparation plus page-read work units |
| `readBudget.maximumDurationNanoseconds` | Maximum source-session duration |
| `verifyStorage` | Allocate each descriptor's storage and inspect every byte for zeroing |

S1 allocates only model/instance buffers. The storage admission budget also bounds
advertised Jacobian array capacities without allocating those arrays.
`maximumStorageBytes` is not a bound on total mapped native-image/runtime memory
or aggregate memory across independent storage owners. In-process native calls
cannot be forcibly interrupted; enforce a process deadline around the CLI.

For a trusted known artifact, generate a request from the caller's selected policy:

```sh
export OSDI_LIBRARY=/absolute/path/to/model.osdi
export OSDI_MAX_DESCRIPTORS=64
export OSDI_MAX_METADATA_BYTES=16777216
export OSDI_MAX_STORAGE_BYTES=16777216
export OSDI_READ_DEADLINE_NS=5000000000
python3 - <<'PY'
from hashlib import sha256
from pathlib import Path
import json
import os

path = Path(os.environ['OSDI_LIBRARY']).resolve(strict=True)
size = path.stat().st_size
if not size:
    raise ValueError('Expected a nonempty native image')
page = min(size, 65536)
pages = (size + page - 1) // page
request = {
    'schemaVersion': 1,
    'libraryPath': str(path),
    'expectedSHA256': sha256(path.read_bytes()).hexdigest(),
    'expectedByteCount': size,
    'limits': {
        'maximumDescriptors': int(os.environ['OSDI_MAX_DESCRIPTORS']),
        'maximumMetadataBytes': int(os.environ['OSDI_MAX_METADATA_BYTES']),
        'maximumStorageBytes': int(os.environ['OSDI_MAX_STORAGE_BYTES']),
    },
    'readBudget': {
        'maximumPageByteCount': page,
        'maximumTotalByteCount': size,
        'maximumPageCount': pages,
        'maximumWorkUnitCount': 2 * pages,
        'maximumDurationNanoseconds': int(os.environ['OSDI_READ_DEADLINE_NS']),
    },
    'verifyStorage': True,
}
Path('request.json').write_text(json.dumps(request, indent=2) + '\n')
PY
unset OSDI_OPENVAF_COMPILER
bounded 10 osdi-inspect \
  --request request.json --output report.json
python3 -m json.tool report.json
```

The numerical policy values above are an example caller choice, not qualified
limits for every model. The local source hashes the artifact before reading;
with these page sizes, digest preparation and page reading each consume `pages`
work units. For replay, retain the approved expected identity and request rather
than regenerating identity from a replacement artifact.

Success exits `0` and atomically writes a versioned report containing
`succeeded`, `stage`, `expectedSHA256`, `expectedByteCount`, `descriptorStride`,
`descriptors`, and `storageVerified`. Each descriptor contains `index`, `name`,
`nodeCount`, `terminalCount`, `jacobianEntryCount`, `stateCount`,
`instanceByteCount`, and `modelByteCount`. No executable pointer is serialized.

Failure exits nonzero and reports `reason`, `detail`, and, for native admission,
`nativeStatus`. Expected identity remains in reports after request decoding.
Examples of classifications include `artifact_access`, `identity_mismatch`,
`native_admission`, `resource_limit`, `library_in_use`, `library_closed`, and
`cleanup_failure`. Artifact corruption or wrong size may be refused by the source
as `artifact_access` before runtime identity revalidation. Invalid arguments or
undecodable requests use `invalid_request`; failure to write a report is emitted
to stderr and remains a failed command. Consumers must check both exit status and
`succeeded`.

## API ownership

Inject `OSDILibraryLoading` and call `load(_:source:limits:)` with the exact
`ArtifactAccessIntent`. Keep immutable metadata separate from native ownership.
`makeStorage(descriptorIndex:)` returns a noncopyable owner of stable zeroed
buffers; synchronous `withModelBytes` / `withInstanceBytes` callbacks borrow
`MutableSpan<UInt8>` views. Views cannot outlive storage or cross `await`.
Destroy storage before `library.close()`: active storage leases make close fail
with `libraryInUse`. A failed native close retains an explicit retryable cleanup
owner in the typed error. Close the artifact source and await its termination.

See [package design](DESIGN.md), [runtime design](Sources/OSDIRuntime/DESIGN.md),
[CLI design](Sources/OSDIInspect/DESIGN.md), and
[verification design](Tests/OSDIRuntimeTests/DESIGN.md) for contracts and evidence
ownership. The upstream revision, header hash and license are recorded in
[ThirdParty/OpenVAF-Reloaded](ThirdParty/OpenVAF-Reloaded/Source.json).

## S2 point evaluation

`osdi-evaluate` prepares a generated native model and evaluates the fixed
`dc-static-point` profile through the public Runtime API. `osdi-inspect` retains
its S1 admission/storage behavior. Build the new product using the same fixed
Swift 6.4.0 toolchain and `bounded` helper above:

```sh
bounded 300 "$SWIFT" build --product osdi-evaluate --jobs 2
unset OSDI_OPENVAF_COMPILER
bounded 10 osdi-evaluate --request point-request.json --output point-report.json
```

```text
exact artifact + scoped parameters + Kelvin + connected terminals
  -> operation prepare -> setup facts and raw node to active/ground mapping
  -> caller supplies every active unknown and previous state
  -> operation evaluate -> point F/Q/G/C and candidate next state
```

The version-1 request retains `libraryPath`, `expectedSHA256`,
`expectedByteCount`, `limits`, and `readBudget` from the inspection schema. Replace
`verifyStorage` with these required evaluation fields:

| Field | Contract |
|---|---|
| `operation` | `prepare` for setup facts, or `evaluate` for point outputs |
| `descriptorIndex` | Zero-based admitted descriptor index |
| `profile` | Exactly `dc-static-point` |
| `parameters` | Assignments with `name`, `scope` (`model` or `instance`), and typed `value` |
| `simulationParameters` | Explicit `name` and typed real/string `value`; omitted names receive no invented values |
| `temperatureKelvin` | Finite positive absolute temperature |
| `connectedTerminalCount` | Number of connected terminals passed to generated setup |
| `localUnknowns` | Required for evaluate: every prepared active unknown in dense active order, including surviving internal variables |
| `previousState` | Required for evaluate: every previous state explicitly supplied; `[]` for a zero-state model |
| `evaluationLimits.maximumWorkspaceBytes` | Checked native workspace allocation and temporary peak budget |
| `evaluationLimits.maximumDiagnosticCount` | Maximum retained native diagnostic entries |
| `evaluationLimits.maximumDiagnosticBytes` | Diagnostic storage sublimit within the workspace budget |
| `evaluationLimits.maximumStringBytes` | String storage sublimit within the workspace budget |

A typed `value` has `kind` equal to `real`, `integer`, or `string` and exactly one
matching `real`, `integer`, or `string` member. Integer values must fit Int32.
Array assignments are an explicit unsupported capability. Parameter names and
aliases resolve against the generated descriptor; duplicates, wrong scope and
wrong types fail preparation. Model scope may set an instance parameter's model
default; instance scope supplies its override. Unspecified values and given flags
come from generated model setup/access, not CLI defaults. Simulation parameters
support real/string only; for a reproducible zero-`gmin` run, explicitly include
`{"name":"gmin","value":{"kind":"real","real":0.0}}`.

The profile requests resistive/reactive residuals and Jacobians plus operating
point calculation, with DC/static flags at time zero and limiting disabled.
`localUnknowns` are not necessarily voltages: node metadata preserves units,
residual units, flow classification and nature references. The CLI never fills
internal node values or solves their physical constraints. First send `operation: "prepare"`, omitting `localUnknowns` and `previousState`,
to discover exact prepared topology and parameter facts. Use that map to construct
an `operation: "evaluate"` request with both complete arrays. Preparation reports
use stage `osdi-preparation` and omit `evaluation`; success means setup only. Different parameters/temperature can change collapse.

Successful reports include exact raw request SHA-256/size, the decoded `input`,
exact `artifactBinding`, `toolVersion` (`swift-osdi/S2-schema1`, a schema identifier),
OSDI ABI version, descriptor identity, `prepared` topology/parameter facts and
`evaluation`. Parameter facts retain canonical names/aliases, type, scope,
effective scalar value and `isGiven` from generated access. Ground nodes have no
`activeUnknownIndex`; repeated active indices express collapsed raw nodes.

`evaluation.f` and `.q` have raw descriptor length N. `.g` and `.c` contain raw
row-major N×N matrices, including rows/columns whose raw nodes collapse. These are
model-local equations, not a reduced circuit matrix. `.nextState` is a candidate
output with no implicit acceptance; the caller must explicitly choose the next
request's `previousState`. Model return flags and bounded diagnostics remain in
the report. The command closes model/library/source owners, atomically writes the
JSON report and reads/decodes the saved bytes before reporting success.

Failure reports preserve exact request binding when available and expose
`failure.reason`, `.detail`, native status/field/descriptor, model flags and
initialization error count/code/parameter ID when a native error supplied them.
Invalid parameter, Kelvin temperature, active/state vector size and unsupported
profile/capability produce nonzero exits. Check both exit status and `succeeded`.

S2 remains a trusted native point-evaluation interface. It provides no circuit
solver, transient integration, delay/history analysis, automatic state acceptance,
interval enclosure or all-phase/all-history proof. Successful point evaluation
does not establish physical model correctness or foundry qualification. Native
workspace limits exclude mapped libraries, Swift/report serialization overhead
and total process memory.

### Reproduce S2 numerical fixtures and tests

S2 numerical fixtures require an explicitly prepared **full-residuals compiler
profile** and an unchanged-source BSIM-BULK 107.2.1 artifact generated with
`--full-residuals`. The ABI-compatible default compiler profile can specialize
small-signal auxiliary residuals; ABI admission alone does not qualify its
F/Q/G/C derivative contract. The qualified workflow retains the original checks
and demonstrates that default-profile counterexample separately.

Prepare the external compiler using the pinned source/patch driver in
[swift-openvaf](../swift-openvaf/Tools/OpenVAFCompilerProfile/DESIGN.md). It requires
a local Rust toolchain, LLVM 22 prefix and checkout at the exact revision recorded
in [Source.json](../swift-openvaf/Tools/OpenVAFCompilerProfile/Source.json). Select
fresh output directories outside that checkout; the helper verifies source/patch
identity and records process budgets, compiler identity and focused regression
results. For example, with caller-selected local paths:

```sh
OPENVAF_SOURCE=/absolute/path/to/pinned/OpenVAF-Reloaded
LLVM_PREFIX=/absolute/path/to/llvm22
PROFILE_OUTPUT=/absolute/path/to/fresh/compiler-profile-output
bounded 1200 python3 ../swift-openvaf/Tools/OpenVAFCompilerProfile/build.py \
  --source "$OPENVAF_SOURCE" --output "$PROFILE_OUTPUT" \
  --llvm-prefix "$LLVM_PREFIX" --timeout-seconds 600 \
  --test-timeout-seconds 180 --jobs 2 --minimum-free-gib 3
export OSDI_OPENVAF_COMPILER="$PROFILE_OUTPUT/products/openvaf-r"
export OSDI_COMPILER_PROFILE_RECEIPT="$PROFILE_OUTPUT/receipt.json"
```

Compile the exact BSIM source with the same compiler and retain its source,
include, compiler-profile and artifact receipt. The
[BSIM preparation helper](Tools/OSDIEvaluationVerification/compile_bsim.py)
checks the pinned 107.2.1 source and OSDI header, snapshots includes and requires
the full-residuals flag:

```sh
BSIM_SOURCE=/absolute/path/to/unchanged/bsimbulk.va
BSIM_INCLUDES=/absolute/path/to/bsim/includes
BSIM_OUTPUT=/absolute/path/to/fresh/bsim-output
bounded 120 python3 Tools/OSDIEvaluationVerification/compile_bsim.py \
  --compiler "$OSDI_OPENVAF_COMPILER" --source "$BSIM_SOURCE" \
  --includes "$BSIM_INCLUDES" --header Sources/COSDI/ABI/osdi_0_4.h \
  --compiler-profile-receipt "$OSDI_COMPILER_PROFILE_RECEIPT" \
  --output "$BSIM_OUTPUT"
export OSDI_BSIM_LIBRARY="$BSIM_OUTPUT/bsimbulk107-full-residuals.osdi"
export OSDI_BSIM_SHA256="$(python3 - "$BSIM_OUTPUT/artifact-receipt.json" <<'PY'
import json
import sys
from pathlib import Path
print(json.loads(Path(sys.argv[1]).read_text())['artifact']['sha256'])
PY
)"
```

The S2 test fixture actor requires `OSDI_OPENVAF_COMPILER`, `OSDI_BSIM_LIBRARY`
and `OSDI_BSIM_SHA256`. `OSDI_COMPILER_PROFILE_RECEIPT` additionally archives the
compiler build provenance; this workflow supplies it explicitly. The independent
fixture builder and Swift tests consume the same inputs:

```sh
EVALUATION_FIXTURES="$(mktemp -d "${TMPDIR:-/tmp}/osdi-evaluation-fixtures-XXXXXX")"
bounded 120 python3 Tools/OSDIEvaluationVerification/build.py \
  --output "$EVALUATION_FIXTURES" --clang /usr/bin/clang \
  --header Sources/COSDI/ABI/osdi_0_4.h \
  --compiler "$OSDI_OPENVAF_COMPILER" --bsim "$OSDI_BSIM_LIBRARY" \
  --bsim-sha256 "$OSDI_BSIM_SHA256" \
  --compiler-profile-receipt "$OSDI_COMPILER_PROFILE_RECEIPT"
bounded 120 "$SWIFT" test --skip-build --disable-xctest --enable-swift-testing \
  --filter OSDIEvaluationTests
```

Use the prior `build --build-tests` command first whenever test sources changed.
The independent manifest binds compiler/source/header/artifacts and records every
finite-difference outcome. A generated manifest with failed checks remains a
qualification failure. Retain its exact library path/digest and request inputs
when comparing API and CLI results. Ordinary `osdi-evaluate` execution only loads
the resulting artifact and never invokes the compiler or either fixture builder.

## Dynamic point evaluation

The context overload accepts explicit DC, AC, transient, transient initialization
and noise phases. Noise evaluates AC|NOISE at an explicit positive frequency in
hertz and returns independent source densities with descriptor-coordinate source
metadata. `noiseDensities` and `maximumTimestep` borrow prepared native storage;
copy only at an output or retained-history boundary. The caller owns accepted
state, integration and rejection. Positive infinity means no timestep bound.

The `osdi-evaluate` CLI preserves schema 1 `dc-static-point`. Schema 2 requires
`profile: dynamic-point` and `context` containing `mode`, `timeSeconds`, and
`noiseFrequencyHz` only for noise. It saves source metadata, fixed-bias source PSD magnitude and optional
maximum timestep with the existing exact artifact/request bindings. Native images
remain trusted code. These point values do not establish solved circuit results.

Dynamic runtime tests additionally require `OSDI_S4_FAMILIES_MANIFEST` pointing to
the exact four-family capture manifest produced by the CoreSpice qualification
tools. The focused `OSDIDynamicEvaluationTests` suite uses its solved raw node
values to compare AC, IC, transient and two noise frequencies with the independent
point ABI oracle. Existing S1/S2 filtered suites retain their prior inputs.


## Latest-family qualification and performance

| Official profile | Source authority |
|---|---|
| BSIM-BULK 107.2.1 | Berkeley / Si2 CMC |
| BSIM-CMG 112.1.0 | Berkeley / Si2 CMC |
| BSIM-IMG 103.1.0 | Berkeley / Si2 CMC |
| BSIM-SOI 101.0.0 | [Si2 September 2026 release](https://si2.org/wp-content/uploads/CMC/models/BSIM-SOI_101.0.0_09152026.tar.gz) |

SOI101 has six terminals and `BODYMOD`; old seven-terminal SOI4.7 inputs are
retained only as regressions. `prepare_soi101.py --help` describes explicit
latest-model point preparation. `verify_families.py --help` verifies each supplied
family through six actual CLI contexts and reloads exact-bound reports against a
direct ABI observer. These are point evaluations, not solved circuit trajectories
or foundry qualification. The ordinary JSON schema canonicalizes signed zero;
nonzero values retain binary64 bits. API comparisons retain every bit.

Fixed-bias noise checks finiteness before converting signed OSDI noise power to
its magnitude. This is not Pnoise or stochastic transient-noise support.

The `OSDIRealModelPrecisionTests` suite additionally requires the retained
`OSDI_REAL_PRECISION_REQUEST_DIRECTORY`, `OSDI_REAL_PRECISION_REFERENCE`,
`OSDI_REAL_PRECISION_INDEPENDENT_RECEIPT`, `OSDI_PRECISION_WORKER`, and
`OSDI_PRECISION_NUMERIC_PROFILE_SHA256` qualification inputs. The latest SOI
regression uses `OSDI_SOI101_PRECISION_DIRECTORY`, containing the exact inputs and
`latest-qualification.json` produced by `verify_soi101.py` (see `--help`). This tool joins
public CLI results with an independent 512-bit source-program replay. F/DeltaQ
are enclosures; G/C are approximate Newton aids. SOI101 was tested with an
explicit 256 MiB arena; the 64 MiB refusal is retained and is not silently retried.

`benchmark_models.py --help` compares immutable before/after workers on identical
programs, explicit limits and continuous request streams. `measure_program.py
--help` measures private-arena activity separately. Both default to every supplied
manifest entry and record explicit subset exclusions. Heap counters do not imply
zero private-arena allocation or zero total copying; worker latency does not
establish full-circuit acceleration.
