# Runtime Fixture Support

## Purpose and Scope

Own one asynchronous test-process fixture preparation and typed manifest access.
Parent: [runtime tests](../DESIGN.md). Shared native build owner: [BuildCache](../../OSDITestSupport/BuildCache/DESIGN.md). Test expectations and production native
loading remain outside this helper.

## Responsibilities and Boundaries

`OSDIFixtureCache` serializes publication of one preparation Task, so concurrent
test suites share the same immutable build result and failed setup is explicit.
`OSDIFixtureSet` resolves recorded paths and reads per-library lifetime events.
Tests that inspect the same lifetime marker must remain in one serialized suite.
No test-local path becomes a production loader assumption.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [Runtime tests](../DESIGN.md) | parent | Shared test setup | One cache for all suites | Marker-sensitive cases serialized |
| [Builder](../../../Tools/OSDIVerification/DESIGN.md) | depends on | Explicit model qualification manifest | Optional real-model preparation | macOS arm64 with explicit OSDI_OPENVAF_COMPILER |
| [Fixtures](../Fixtures/DESIGN.md) | depends on | Per-library marker and named variants | Immutable test inputs | Metadata callbacks refuse execution |

## Architecture

```text
concurrent test setup -> cache actor -> one retained preparation Task
 -> normal Swift build cache or explicit qualification builder
 -> fresh sentinel-owned run directory -> native load under that directory
 -> decoded immutable manifest -> fixture/model paths + lifetime event reads
```

## Contracts and Invariants

All test suites use the same actor and preparation task. Normal preparation uses
the shared [build cache deadlines and cleanup contract](../../OSDITestSupport/BuildCache/DESIGN.md).
Explicit model qualification requires an existing executable selected by `OSDI_OPENVAF_COMPILER`;
there is no sibling-workspace compiler fallback. The builder receives explicit
source/header/compiler/output paths and does not modify global environment settings. That explicit builder path polls a monotonic twenty-five-second deadline, terminates on
failure/cancellation and uses an independent task for its bounded cleanup window.
The builder handles SIGTERM by killing its active compiler process group, including
linker children, before exiting. Every child compiler command has its own
thirty-second timeout. A missing actual compiler is a setup failure, not a passing
substitute fixture. Temporary directories remain available for failure diagnosis;
no owner deletes a directory while a native library could still be loaded.

## State, Ownership, and Lifecycle

The actor owns the preparation Task; its immutable result owns only paths and
manifest values. Production libraries/storage retain their own owners. Constructor
and destructor events belong to each compiled library, not to the cache. Tests
must close storage and library owners before asserting the final unload event.

## Verification and Change Impact

Builder-only qualification covers compilation of twenty-seven metadata libraries
and two real OpenVAF models on the native host. Root-owned runtime tests establish the
actual loader, storage and close behavior. Updating builder schema or root paths
requires the manifest decoder and test setup to change together.

## Standalone Selection

The [standalone verification contract](../../../Tools/OSDIStandaloneVerification/DESIGN.md)
owns explicit qualification selectors. Default metadata/protocol inputs need only
the C compiler; requested actual-model qualification retains the original input
validation and fails on missing compiler/artifact evidence.

## Ordinary Cached Preparation

Normal tests call the shared Swift native build cache and require no Python.
Each fixture variant declares exact source/header/compiler/SDK/target/flags and
receives a validated immutable image. The test actor copies images into one fresh
run directory with `.osdi-fixture-run`; each ArtifactInput stages beneath that
root. Constructors/destructors resolve the sentinel and fixed variant ID at run
time. Cached bytes never embed a run-specific observation path. Existing marker
assertions and serialized ownership remain unchanged. Explicit model qualification
retains the tracked legacy builder and its original compiler/source requirements.
