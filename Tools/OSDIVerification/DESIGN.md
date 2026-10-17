# OSDI Verification Builder

## Purpose and Scope

Own bounded compilation, provenance and cleanup-custody fault probes for S1. Parent:
[swift-osdi](../../DESIGN.md). Input semantics belong to the
[fixtures](../../Tests/OSDIRuntimeTests/Fixtures/DESIGN.md).

## Responsibilities and Boundaries

Compile local trusted test source with clang and optionally a caller-selected real
OpenVAF compiler. Verify the unchanged OSDI header SHA-256 before compiling. Produce
native libraries and a machine-readable manifest in a unique caller-owned output
directory. Do not install tools, download dependencies, sign shared executables,
or claim model numerical qualification.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [swift-osdi](../../DESIGN.md) | parent | S1 qualification scope | Reproducible native admission | macOS arm64 only |
| [Fixtures](../../Tests/OSDIRuntimeTests/Fixtures/DESIGN.md) | depends on | Metadata variants and actual VA programs | Compilation inputs | Metadata callbacks refuse evaluation |
| [Fixture support](../../Tests/OSDIRuntimeTests/FixtureSupport/DESIGN.md) | used by | Fresh output directory and bounded process execution | Shared serialized test setup | Remove only after owners close |

## Architecture

```text
--output + --clang + --header [+ --openvaf]
 -> host and header-digest gate
 -> bounded clang commands -> metadata libraries + unique lifetime markers
 -> optional bounded OpenVAF commands -> actual F/Q libraries
 -> source/compiler/binary hashes + command receipts -> manifest.json
```

## Contracts and Invariants

Every subprocess has a thirty-second timeout and nonzero exit status fails the
build. A manifest is written only after every requested library is generated.
Existing manifests and lifetime markers are rejected rather than reused. Paths
are passed as subprocess argument arrays, including the compiled marker constant.
The manifest records the builder, header, fixture sources, compiler executables,
versions, commands, outputs, and binary digests. No static binaries are committed.

## Failure, Concurrency, and Constraints

Each test fixture build owns a unique temporary directory. The calling actor
serializes setup and caches its completed manifest; no environment mutation is
required. Missing compiler, unsupported host, changed header, compile failure and
timeout remain explicit failures. Generated native code is trusted test code;
these tests do not prove safety against malicious native addresses.

## Verification and Change Impact

Run the builder with the pinned header and known signed OpenVAF executable, then
inspect every generated library through the actual COSDI/Swift loader path. A
successful compiler invocation alone does not establish admission correctness.
`fault_probe.py --help` describes the separate initial/nested cleanup-custody
regression. It compiles the actual owner/storage sources against resolved real
Contracts/Foundation products, replacing only the C boundary with `FaultProbe`
test resources. Persistent close failure must reach the outer catch with the same
cleanup owner, then release successfully after the fault is removed. This test
adds no production hook and does not qualify native descriptor validation; the
ordinary native-image tests own that contract. Every probe process uses the same
bounded process runner and the receipt binds source, tool and dependency hashes.

Model invocation and F/Q numeric comparisons belong to S2.
