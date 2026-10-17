# OSDI Model Connection

## Purpose and Scope

Parent: [LSI](../DESIGN.md). This independent Swift package connects the existing
open-source OSDI model ABI to native consumers. S1 establishes artifact admission,
library/descriptor lifetime and stable zeroed storage. S2 invokes generated
model/instance setup and point residual/charge/Jacobian evaluation. It does not
implement device physics, a Verilog-A compiler or a circuit solver.
Children: [COSDI](Sources/COSDI/DESIGN.md), [OSDIContracts](Sources/OSDIContracts/DESIGN.md),
[OSDIRuntime](Sources/OSDIRuntime/DESIGN.md), [OSDIInspect](Sources/OSDIInspect/DESIGN.md), [OSDIEvaluate](Sources/OSDIEvaluate/DESIGN.md).

## Responsibilities and Boundaries

Use the unmodified OpenVAF-Reloaded 0.4 header and compiled native model directly.
COSDI is minimal native loading/validation/allocation glue, not a replacement model
runtime. Swift owns artifact acquisition, scoped lifetimes, typed failures and the
consumer API. CoreSpice's equations/solver and swift-openvaf's compiler workflow
remain separate. The initial executable profile is macOS arm64 and exact pinned
OSDI 0.4 header/compiler fixtures. Native images are not WASM/Embedded artifacts.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [LSI](../DESIGN.md) | parent | OSDI device-model boundary | Standalone model connection | Point evaluation cannot establish all-history inclusion |
| [Foundation](../CircuiteFoundation/DESIGN.md) | depends on | ArtifactReference, ArtifactAccessing and termination | Exact content and bounded read | Location stays separate from identity |
| [OpenVAF](../swift-openvaf/DESIGN.md) | coordinates with | Compiled model artifact | Compiler is a separate preparation step | No compiler dependency or invocation during load |
| [Full-residual compiler profile](../swift-openvaf/Tools/OpenVAFCompilerProfile/DESIGN.md) | coordinates with | Source-faithful generation of all-coordinate point values | Exact compiler and profile bind numerical fixtures | Default small-signal specialization does not qualify unrestricted auxiliary F/Q/G/C |
| [CoreSpice](../CoreSpice/DESIGN.md) | used by | Library metadata and storage lifetime | Future CoreSpiceOSDI adapter | Existing CSR Array borrows are not persistent addresses |

## Architecture

```text
ArtifactReference + availability + ArtifactAccessing
  -> OSDIRuntime: bounded read / private materialization / exact revalidation
    -> COSDI: upstream ABI + system dynamic loader + descriptor admission
      -> immutable metadata / retained library owner
        -> run-local stable zeroed storage / scoped MutableSpan
        -> exclusive prepared point session / scoped F/Q/G/C and candidate state
```

The portable Contracts target depends only on Foundation Core (the LSI package,
not Apple's Foundation). Only Runtime imports host I/O/digest implementations and
COSDI. Existing Rust consumers were examined; their solver coupling or incomplete
validation/state handling does not justify adding them as production dependencies.
No new ABI definitions duplicate the upstream OsdiDescriptor.

## Contracts and Invariants

A successful load binds the bytes actually materialized to the requested exact
artifact ID before dlopen. Descriptor traversal uses the exported stride, accepting
larger entries and refusing entries shorter than the pinned header. Counts, tables,
index products, alignment and instance offsets must pass the owning C validation.
Caller-selected limits bound artifact reading, inspected metadata and requested
model/instance storage plus advertised Jacobian array capacities; mapped native-image/runtime memory is not claimed bounded
by the storage counter. Library metadata contains no callable or executable pointer.

This is a trusted native-code boundary: constructors execute at dlopen and foreign
functions/pointers require a conforming, trusted artifact. Structural validation is
not a sandbox against malicious native code or arbitrary invalid addresses. A
point model does not establish interval, all-phase or all-history guarantees.

S2 qualification preserves residuals, charges and their derivatives at every
active coordinate, including auxiliary internal nodes. The external compiler
profile retains those contributions before DAE differentiation. Consumer code
does not infer omitted model equations or exclude coordinates from validation.
An admitted OSDI image alone does not establish source-faithful numerical
qualification; that evidence binds the model source, compiler profile and exact
generated artifact separately.

## Runtime Flows

```text
bounded source session -> private file -> successful source termination
  -> verify private file identity -> C admission -> immutable publication
failure -> close read session + native handle + private file, retaining primary cause
failed native cleanup -> explicit retryable cleanup owner in the typed error
```

Ordinary execution never launches or installs a compiler. S1 CLI inspects metadata
and exercises storage. S2 CLI invokes the public preparation/evaluation protocols
and serializes raw descriptor-coordinate values plus topology and exact inputs.
Neither command claims a solved circuit, physical operating point or interval proof.

## State, Ownership, and Lifecycle

The Sendable library owner protects state and active-storage leases with one
Mutex. It does not hold the lock while dlopen/dlclose or caller callbacks execute.
Explicit close refuses while a lease exists. A noncopyable storage owns its native
buffers and a strong lease, freeing buffers before releasing the lease. No borrowed
view can be returned or retained across await. A close failure retains the native
owner for retry, rather than publishing an unloaded library as usable.

## Failure, Concurrency, and Constraints

Typed failures distinguish artifact acquisition/termination/identity, native host/
architecture/ABI/descriptor/resource/allocation failures, closed/in-use owners and
cleanup. In-process native calls have no forced-interruption guarantee. C allocation
and offset arithmetic are checked before access. Point setup/evaluation and library execution leases are owned by
[PointEvaluation](Sources/OSDIRuntime/PointEvaluation/DESIGN.md). CoreSpice matrix
binding, nonlinear solving and analyses require later contracts.

## Verification and Change Impact

S1 tests own actual valid/invalid dynamic images, larger descriptor strides, zeroed
and stable storage, exact artifact mismatch, cleanup/unload, closed access and lease
refusal. An actual pinned OpenVAF model is loaded through API and CLI without compiler
launch. Native Swift Testing, ASan and appropriate concurrency checks use external
timeouts. Negative C fixtures establish ABI/lifetime behavior, not physical model
correctness. [Runtime test design](Tests/OSDIRuntimeTests/DESIGN.md) owns test composition.
[Fixture tools](Tools/OSDIVerification/DESIGN.md) own bounded fixture production.
S2 adds analytic compiled models, an independent direct-ABI BSIM harness,
parameter/default/given/temperature/internal-variable tests and Jacobian finite
differences. These validate the consumer of the selected generated artifact, not
the underlying BSIM physical equations. [Evaluation tests](Tests/OSDIEvaluationTests/DESIGN.md)
and [evaluation tools](Tools/OSDIEvaluationVerification/DESIGN.md) own that evidence.
[Progress](PROGRESS.md) is the sole task ledger.
