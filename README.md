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
  bounded 120 "$SWIFT" test --skip-build --disable-xctest --enable-swift-testing
```

Missing compiler configuration fails test setup. The fixtures do not silently
substitute hand-authored metadata for a real compiled model. All test suites share
one actor-owned preparation task; fresh output directories isolate lifecycle
markers. To prepare fixtures independently:

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
