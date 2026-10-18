# OSDI Model Connection

## Purpose and Scope

This is the standalone package-root design authority for the Swift consumer of
the existing open-source OSDI 0.4 model ABI. There is no parent design inside this
source distribution. Integrating systems consume its public contracts without
requiring a sibling workspace. The package owns exact artifact admission,
library/descriptor lifetime, stable storage, generated model/instance setup,
ordinary point evaluation and separate explicit precision/range sessions. It does
not implement device physics, a Verilog-A compiler or a circuit solver. Historical
S1/S2/S3/S4 labels below identify development stages, not release support levels.
Children: [COSDI](Sources/COSDI/DESIGN.md), [OSDIContracts](Sources/OSDIContracts/DESIGN.md),
[OSDIRuntime](Sources/OSDIRuntime/DESIGN.md), [OSDIInspect](Sources/OSDIInspect/DESIGN.md), [OSDIEvaluate](Sources/OSDIEvaluate/DESIGN.md).
The S4D optional native precision path adds
[COSDIPrecision](Sources/COSDIPrecision/DESIGN.md), the
[worker entrypoint](Sources/OSDIPrecisionWorker/DESIGN.md), and the host system
bindings [CMPFR](Sources/CMPFR/DESIGN.md) / [CGMP](Sources/CGMP/DESIGN.md).
Their selected contracts do not establish a qualified implementation.
Verification children include [runtime tests](Tests/OSDIRuntimeTests/DESIGN.md),
[evaluation tests](Tests/OSDIEvaluationTests/DESIGN.md),
[fixture production](Tools/OSDIVerification/DESIGN.md),
[evaluation verification](Tools/OSDIEvaluationVerification/DESIGN.md),
[precision verification](Tools/OSDIPrecisionVerification/DESIGN.md),
[compiler fixture](Tools/OpenVAFCompilerFixture/DESIGN.md),
[standalone verification](Tools/OSDIStandaloneVerification/DESIGN.md), and the
[external public-product consumer](Tools/OSDIConsumerVerification/DESIGN.md).

## Responsibilities and Boundaries

Use the unmodified OpenVAF-Reloaded 0.4 header and compiled native model directly.
COSDI is minimal native loading/validation/allocation glue, not a replacement model
runtime. Swift owns artifact acquisition, scoped lifetimes, typed failures and the
consumer API. An external compiler owns model generation; consumers such as
CoreSpice own equations, circuit solving, integration and accepted histories. None
is a runtime build dependency or a required sibling checkout. The supported native
release profile is macOS 26+ arm64, exact Swift 6.4.0 release and the pinned OSDI
0.4 header. Compiler/model qualification requires separately exact-bound fixtures.
Native images are not WASM/Embedded artifacts.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [CircuiteFoundation](https://github.com/1amageek/CircuiteFoundation) | depends on | ArtifactReference, ArtifactAccessing and termination | Exact content and bounded read | The manifest's exact revision is authoritative; public fetchability and distribution rights are release gates; location stays separate from identity |
| [Pinned upstream authority](ThirdParty/OpenVAF-Reloaded/Source.json) | depends on | Unmodified OSDI 0.4 header and its provenance | External OpenVAF-generated artifact boundary | No compiler invocation during runtime load; header admission alone does not qualify model physics or full residuals |
| [Evaluation verification](Tools/OSDIEvaluationVerification/DESIGN.md) | child | Exact source/compiler/model qualification inputs | Source-faithful all-coordinate point evidence | Default small-signal specialization does not qualify unrestricted auxiliary F/Q/G/C |
| [Standalone verification](Tools/OSDIStandaloneVerification/DESIGN.md) | child | Bounded deterministic native test execution | Independent checkout test lanes | Generic fixture success is distinct from optional real-model qualification |
| [External consumer verification](Tools/OSDIConsumerVerification/DESIGN.md) | child | Separate package consuming public products and exact revisions | API/CLI success, failure and persisted result comparison | A local immutable Git clone does not prove canonical remote availability |
| [OSDIRuntime](Sources/OSDIRuntime/DESIGN.md) | child | Model-local preparation, borrowed results and owner lifetime | External circuit consumers compose its public protocols | Consumers own persistent matrix addresses, solving, state acceptance and all-history proof |

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

Ordinary execution never launches or installs a compiler. `osdi-inspect` inspects
metadata and exercises storage. `osdi-evaluate` invokes the public preparation/evaluation protocols
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

### Independent source distribution contract

```text
package source + exact public dependency revision + approved rights/notices
  -> fresh standalone resolution / Debug / Release / native verification
  -> separate public-product consumer with exact revisions
  -> optional exact-input model qualification for claimed model profiles
  -> reviewed source-only release candidate
```

The release dependency graph must resolve every manifest URL and exact stable
version, with its full commit bound by Package.resolved, from its canonical public
remote in a fresh environment without sibling checkouts,
workspace mirrors, edited dependencies or path overrides. The checked-out graph
must match the declared and resolved identities. Stable-version products must not
depend on branch or revision requirements: SwiftPM refuses that graph when a
consumer selects a semantic version. The external-consumer gate therefore resolves
a version requirement as well as checking the exact source/dependency commits.
Historical local availability
exceptions below do not satisfy this contract. The final dependency revision and
license decisions are release admission inputs; this design does not assert that
an unpublished revision or unresolved permission is publicly distributable.

| Release invariant | Owner and required evidence |
|---|---|
| Public exact dependency graph | Package manifest and independent fresh remote resolution; standalone and external-consumer tools retain their scoped execution evidence |
| Distribution rights | Publisher confirms root/dependency permissions and preserved upstream header license/notices before publication |
| Standalone user surface | Public products, typed failures and CLI/report contracts remain usable without a consuming application or workspace |
| Verification distinction | Deterministic native and optional numeric fixtures establish only their selected paths; optional model evidence requires every exact declared input and successful checks |
| Source-only candidate | No compiled worker, CLI, model library or numeric archive release assets; binary distribution requires a separate linked-license/obligation review |
| Mathematical scope | Ordinary point and explicit qualified stateless precision/range profiles retain the limits and unfinished consumer obligations defined below |

[Standalone verification](Tools/OSDIStandaloneVerification/DESIGN.md) and
[external consumer verification](Tools/OSDIConsumerVerification/DESIGN.md) own
their commands, fixture composition and receipt schemas. [RELEASING.md](RELEASING.md)
owns the admission/publication sequence and [README.md](README.md) owns user
prerequisites and supported API/CLI use. Neither a generic CI pass nor a populated
profile index supplies missing model/source-proof evidence. This distribution
work adds no new performance guarantee and does not complete the causal/history
or all-corner obligations below.

### Independent release dependency

The release candidate requires exact stable CircuiteFoundation version
`26.1018.0`, with Package.resolved binding its publicly retrievable commit
`5f132c58209123433e0e479e1ea61096e46061ba`. It adds only an MIT license to
`ae88104c556f5802819713ed741f5b2fffffc2d9`; all implementation, manifest and test
bytes are unchanged. The prior separate SwiftPM consumer compiled, linked and
exercised the public artifact, preparation, evaluation and cleanup contracts at
`ae88104c556f5802819713ed741f5b2fffffc2d9` without client experimental flags or
dependency mirrors. Publication additionally verifies the new exact graph. This replaces the historical unpublished S3 availability input below;
the used public contracts are unchanged. The publisher approved GPL-3.0-only for this source distribution and MIT for
CircuiteFoundation. Root LICENSE/NOTICE and separately identified upstream notices
are authoritative; public Git availability alone does not imply permission.

### Historical S3 dependency compatibility — development only

The following records the historical S3 integration snapshot and its local
availability exception. It does not select the release dependency revision or
provide release permission or public dependency availability.

S3 aligned the exact CircuiteFoundation dependency with CoreSpice's then-existing
revision `c952ac5ebe3c21bf0cbd6d6ac239130efd732b3a`. The prior
`ae88104c556f5802819713ed741f5b2fffffc2d9` to this revision changes host POSIX
portability and internal directory inventory, while the Core artifact identity,
bounded read, termination and host conversion APIs used here remain unchanged.
The model-loading path still materializes through `ArtifactAccessing`, checks the
terminal receipt and source termination, and revalidates the staged artifact
before native admission. No source adapter or dependency override is required.

| Boundary | Required compatibility evidence |
|---|---|
| Exact dependency graph | Manifest, resolved pin and actual checkout agree; no edited dependency |
| Native artifact loading | Existing S1 success, digest rejection and read/close failure tests |
| Native point evaluation | Existing S2 actual compiled-model tests, independent ABI comparisons and all-coordinate finite differences |

The S3 development integration resolves the declared HTTPS identity and exact
revision through a project-local SwiftPM mirror to the local CircuiteFoundation
Git repository under the user-authorized immutable local Git exception. The
resolved checkout must be clean at the exact declared commit; mutable working-tree
state is not dependency input. The commit is not available from the canonical
remote at verification time. This mirror supplies availability, preserves the
semantic revision and is not an edited/path dependency or a publication, release
or portable release configuration. Verification is native
macOS arm64 with the fixed Swift 6.4.0 toolchain and qualified full-residual compiler,
artifact and original compiler receipt; it does not qualify WASM or Embedded.
The historical S3 integration ledger was maintained by the consuming system;
[PROGRESS.md](PROGRESS.md) is this package task's sole ledger. Historical consuming
workspace ledgers are not required source-distribution inputs.

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

## S4 Dynamic Evaluation

Historical S4 development also used the immutable exact local Git availability
exception; it does not apply to a release. External circuit consumers, including
CoreSpice, own solver, acceptance, integration and finalization. This package owns
explicit mode/time/IC context, independent noise-source facts, fresh scoped PSD and a
per-evaluation optional timestep limit. Existing DC calls and schema 1 stay fixed.

S4 additionally publishes immutable [reactive-row presence](Sources/OSDIContracts/ModelEvaluation/DESIGN.md#reactive-residual-presence)
from admitted ABI metadata. Consumers may use that public structural fact without
sampling model derivatives or accessing private descriptor offsets. This cold
metadata extension does not change evaluation, ownership or CLI schemas.

## S4D Model-Owned Precision Composition

The user requires model-derived charge differences and compensated coordinates
without relaxing the original circuit-equation tolerances. Numerical semantics
belong to [Model Evaluation](Sources/OSDIContracts/ModelEvaluation/DESIGN.md#s4d-precision-boundary--d1-proposal),
compiler program transport to [DeviceEvaluation](Sources/COSDI/DeviceEvaluation/DESIGN.md#s4d-optional-precision-extension--d1-contract),
process/resource ownership to [PointEvaluation](Sources/OSDIRuntime/PointEvaluation/DESIGN.md#s4d-precision-session--d1-contract),
and enclosed arithmetic to [COSDIPrecision](Sources/COSDIPrecision/DESIGN.md).
These child contracts are the authority; this section does not establish a
qualified implementation.

```text
OpenVAF source/setup/DAE program -> optional immutable model export
  -> explicit native precision session -> isolated persistent arithmetic worker
    -> scoped original F / model-owned DeltaQ enclosures
      -> CoreSpice compensated state and unchanged strict physical acceptance
```

The standard OSDI 0.4 image and binary64 operations retain their contracts. The
additional native-only lane must be requested and admitted explicitly. It adds
neither model equations nor a circuit solver to swift-osdi. CoreSpice retains
Newton, integration, accepted histories and trajectory storage. Unsupported
precision capability, exhausted resources and interrupted helper operations
publish no result and cannot select the ordinary lane implicitly. Its exact
compiler/program/worker/numeric-library provenance and measurements are separate
from the historical binary64 qualification.

## Four-Family Completion and Performance Qualification

The completion task first closes this package's model boundary before resuming the
parent causal/all-corner work. The ordinary lane must evaluate original F/Q/G/C,
explicit modes, scoped parameters, Kelvin temperature, internal/collapsed nodes,
noise and candidate state. Fixed-bias noise converts finite signed OSDI power
to source PSD magnitude; it does not discard nonfinite failures or establish
Pnoise/time-domain-noise semantics. The precision lane must preserve original nonlinear F
and model-owned DeltaQ with the same directed rounding, work policy, failure and
scoped-borrow/lifetime contracts. It does not inherit circuit-solver ownership.

The existing exact regression profiles are BULK 107.2.1, CMG 112.1.0, IMG 103.1.0
and SOI 4.7.0. Their emitted programs are stateless and have no time/analysis inputs;
ordinary state tests still verify the separate generic previous/next-state seam.
These facts do not extend support to a newly acquired model. At task admission,
Si2 lists SOI 101.0.0, 100.1.2 and 4.7.1 as production versions while the Berkeley
page still lists earlier releases. The authentic Si2 SOI101.0.0 archive has now been acquired, with source and NOTICE
identifying the September 15, 2026 release. Its six-terminal interface differs
from the old seven-terminal source and requires fresh setup/topology/numerical
qualification. The other three freshly acquired official source trees match the
existing source/include bytes. SOI4.7.0 remains a regression baseline, never a
substitute for the new model. Exact acquisition receipts live with task evidence.

| Owner | Completion evidence |
|---|---|
| PointEvaluation | Public real-model F/Q/G/C and PSD against direct ABI, all active-coordinate finite differences, scoped setup facts and retry/lifetime failures |
| PrecisionExecution / ProgramEvaluation | Real low-sensitive paired endpoints, independently qualified F/DeltaQ intervals, explicit approximate G/C, exact program/setup and worker binding, failure and cleanup |
| Arithmetic | Independent directed four-corner endpoint comparison for any specialized mul/div path; unchanged precision, domain/status, alias and work semantics |
| Verification tools | Same-condition Release before/after cold/first/steady timing, exact output comparison outside timed intervals, allocation/copy scope and immutable receipts |
| Package integration | Actual API and CLI with saved/reloaded bit patterns, exact source/compiler/artifact identities and no partial-success publication |

The initial speed change may specialize strictly positive finite interval
multiplication/division using monotonic endpoint selection. Zero, nonfinite and
other-sign cases retain the existing four-corner implementation. Both paths use
the same intermediate precision, directed rounding, final conversion and work
charge; temporary results preserve output/input aliasing. No setup cache, lower
precision, tangent charge approximation, omitted root or relaxed tolerance is
introduced. This local contract is owned by Arithmetic's child design.

Performance comparisons use immutable before/after worker images built with the
same compiler, optimization, static numeric libraries and semantic flags. The
same program, setup, endpoint vectors, precision, budgets and audit settings are
used in alternating fresh-process rounds. Startup, first request and continuous
steady requests are separate measurements. Heap and copy observations are scoped;
no total zero-copy or full-circuit speed claim follows from a worker measurement.
The parent must revalidate adopted exact revisions and affected circuit paths.

## Causal Range Boundary

The continued task is tracked by [PROGRESS.md](PROGRESS.md). The existing point and
compensated-point contracts remain unchanged. A separate smooth-box operation
consumes lower/upper normalized twofold coordinates, lower/upper twofold absolute
times and one fixed analysis mode, with the same immutable program, typed setup,
temperature and prepared topology. It encloses absolute F/Q and the same-source
spatial G/C over the complete closed box. Native state remains explicitly
unsupported. A smooth-box certificate is a local constitutive fact, never an
entire-history, all-corner or circuit-flow certificate.

G/C retain the raw-node derivative convention, not a silently projected active
Jacobian. Regularity tracking therefore includes distinct raw potential arguments
even when preparation aliases them or maps them to ground. Equal aliased values
can prove a zero value difference but cannot establish independent raw partials
at a nonsmooth face. Such faces remain refusal until separate source/quotient or
generalized authority is checked; same-SSA structural annihilation is distinct
from coincident numerical values.

```text
official source + exact compiler program/setup/topology
  -> bounded smooth-box interpreter and regularity checks
  -> outward F/Q/G/C bounds over every point in the box
  -> CoreSpice circuit reconstruction and Timing causal/history checker
  -> actual same-candidate corner/functional-proof/persistence evidence
```

The first range profile must certify an open smooth neighborhood of the closed
box for every demanded F/Q/G/C data/control dependency. Source-dependent switch
faces, abs/min/max ties, floor/ceil/integer-conversion boundaries and elementary
domain faces cannot inherit smoothness from compiler AD output. Such a demanded
dependency produces an explicit nonsmooth/unsupported failure, without a partial
view. Inactive pure computations retain the existing demand-validity rules. These
refusals do not exclude histories from the parent requirement: branch-cover,
continuity/generalized-derivative and continuation authority remain separate
unfinished work before the original task can complete. Time-dependent sources
must retain their complete time box; spatial G/C does not supply explicit time
partials. The first causal consumer must refuse explicit-time-dependent source
programs until that additional authority is implemented.

The existing compiler program preserves same-source F/Q and local AD roots.
No physical equation is reimplemented. The worker owns conservative regularity
tracking from retained instructions/CFG; the compiler's point derivative output
alone cannot establish it. Topology is rechecked before publication. Exact source,
program, worker/profile, setup, coordinate/time box and numerical/resource policy
must accompany the caller's retained evidence.

The new operation uses local IPC version 2, independently of OSDI 0.4 and the
unchanged version-1 point operation. Its immutable geometry has four input planes
(lower high/low, upper high/low), four vector output planes (F lower/upper, Q
lower/upper), and four row-major raw-node matrix planes (G lower/upper, C
lower/upper). Outward binary64 endpoint projection replaces midpoint projection
only in this operation. Cross-operation calls fail before a request or callback.
The same exclusive noncopyable session, Mutex lifecycle, bounded process owner,
fixed preadmitted storage and fallible cleanup are reused; borrowed result spans
remain valid only during the synchronous callback. No point API reinterprets its
inputs or outputs as a box.

The [precision contract](Sources/OSDIContracts/PrecisionEvaluation/DESIGN.md),
[runtime](Sources/OSDIRuntime/PrecisionExecution/DESIGN.md),
[process](Sources/COSDI/PrecisionProcess/DESIGN.md) and
[interpreter](Sources/COSDIPrecision/ProgramEvaluation/DESIGN.md) own their
respective implementation details. Verification must include a floor-crossing
counterexample, closed boxes touching switch/domain faces, nonlinear closed-form
F/Q/G/C bounds, point-regression parity, invalid box geometry, work/storage limits,
cancellation, atomic refusal and owner cleanup, then actual source programs.

Latest BSIM-BULK qualification does not reinterpret foundry model cards. The
retained Sky130 candidate's cards specify BSIM4.5; any future causal admission must
bind those cards to a separately qualified external matching model through OSDI.
It must not revive the superseded in-project physical reimplementation or relabel
the cards as BSIM-BULK107.2.1 calibration. Missing exact model admission is a
failure, not a substitute model or permission to narrow the original goal.
