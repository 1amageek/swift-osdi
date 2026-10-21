# OSDI Precision Worker

## Purpose and Scope

Parent: [swift-osdi](../../DESIGN.md). Children: [Arena](Arena/DESIGN.md),
[Arithmetic](Arithmetic/DESIGN.md), [ProgramEvaluation](ProgramEvaluation/DESIGN.md),
[WorkerProtocol](WorkerProtocol/DESIGN.md), [DecisionCover](DecisionCover/DESIGN.md), and [ConstitutiveIdentities](ConstitutiveIdentities/DESIGN.md). This native C worker evaluates the optional,
versioned compiler-owned precision program for model setup, F, Q and model-owned
charge differences. CoreSpice retains circuit equations, MNA assembly, Newton,
time integration, acceptance and accepted-state ownership in its Swift process.
The initial precision profile admits stateless models only. Ordinary OSDI 0.4 and
its stateful point-evaluation contract remain separate.

D1 freezes the compiler program ABI, wire protocol, primitive-constant semantics
and compiler-owned derivative roots. This document is the selected implementation
contract. Completion remains conditional on its behavioral verification evidence.

## Responsibilities and Boundaries

The worker owns correctly rounded interval arithmetic, a fixed-capacity private
allocation arena, reusable registers, process-local MPFR caches, program validation
and arithmetic diagnostics. The runtime adapter owns worker/program/library exact
identities, launch, shared-memory and pipe leases, timeout/cancellation, child
termination/reaping and public typed failures. The compiler owns source semantics,
complete setup/evaluation dependencies, control flow, output rows and the precision
program schema. No model equation is reimplemented in the worker.

The worker is the sole GMP user in its process. It installs GMP memory hooks before
any GMP or MPFR initialization. It never changes an application's GMP allocator,
rounding mode, exponent range, flags or caches. Symbol-renamed library copies and
nonlocal OOM recovery are unnecessary and unsupported.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [swift-osdi](../../DESIGN.md) | parent | Native optional model profile | Package composition | Precision success requires actual model evidence |
| [ConstitutiveIdentities](ConstitutiveIdentities/DESIGN.md) | child | Exact checked F/Q source-linear relations | All original rows and complete terminal scopes are retained | Relations grant no numerical rank, root, flow or history authority |
| [PointEvaluation](../OSDIRuntime/PointEvaluation/DESIGN.md) | coordinates with | Exclusive session and library lifetime | Runtime owns the process adapter and public borrow | No raw pointer or borrowed span crosses process boundaries |
| [OpenVAF compiler profile](../../../swift-openvaf/Tools/OpenVAFCompilerProfile/DESIGN.md) | depends on | Exact compiler and source-faithful generated program | Model equations remain compiler-owned | Program ABI and primitive-constant semantics are fixed by the qualified compiler profile |
| [CoreSpice compensated transient](../../../CoreSpice/Sources/CoreSpiceAnalysis/CompensatedTransient/DESIGN.md) | used by | Compensated coordinates and strict residual gate | Circuit integration remains in-process | Newton aids do not establish final residual acceptance |

## Architecture

```text
Swift circuit solver and accepted history
  -> runtime-owned exact worker/process lease
    -> fixed shared request/result slabs + bounded wake pipe
      -> C precision worker
        -> validated compiler program and output map
        -> directed MPFR interval registers
          -> fixed arena -> process-owned GMP allocation hooks
        -> complete F / Q / DeltaQ enclosures
        -> precision derivative midpoints as inexact Newton aids
  -> Swift original physical residual enclosure gate
```

One `OSDIPrecisionWorker` executable target owns the composition entrypoint and
all arithmetic, program and transport components. The runtime C process adapter
remains a child of COSDI. Runtime owns `COSDI/include/OSDIPrecisionProtocol.h`, and the
compiler owns `COSDI/include/OSDICompilerPrecision.h`. These headers contain no
MPFR types. Worker/library use COSDI only for public protocol declarations.
The worker performs no native model callbacks, model compilation, circuit solve or
persistent canonical-state mutation. Preparation reserves the complete owned
workspace before publishing a ready session.

## Contracts and Invariants

| Input or result | Required contract |
|---|---|
| Program | Exact version, full source/setup/eval cone, checked counts, indices, ranges, opcodes and output rows |
| Constants | Primitive frontend-decoded binary64 bit patterns imported exactly; compound expressions remain in the pre-fold program |
| Coordinates and time | Normalized binary64 high/low pairs are validated, then imported separately and summed with directed rounding |
| Registers | Valid demanded results have MPFR RNDD/RNDU enclosures; inactive arithmetic validity follows [ProgramEvaluation](ProgramEvaluation/DESIGN.md) |
| Control flow | Definite branches execute their selected arm; inactive invalid control follows the locally proved pure-region contract in [ProgramEvaluation](ProgramEvaluation/DESIGN.md) |
| Charge difference | Candidate and accepted Q are evaluated by the precision model and subtracted before binary64 projection |
| Derivatives | Precision-program/AD G and C midpoint values are explicitly inexact Newton aids; final F/DeltaQ enclosure decides acceptance |
| Publication | Result generation and success status are published only after the entire request succeeds |
| Failure | No stale, partial, nonfinite, guessed-branch or budget-exhausted result is a success |

Supplied real parameters are exact binary64 facts, integer parameters are exact
integers, and strings/given flags retain their typed meaning. Primitive literal
decode is distinct from binary64 folding of compound expressions or setup caches.
Original decimal lexeme precision is not claimed unless the compiler explicitly
retains and qualifies that authority.

Arithmetic supports only opcodes admitted by the frozen program profile. Addition
and subtraction use directed endpoints. Multiplication and division consider all
endpoint combinations, and division rejects a denominator containing zero.
Monotone functions evaluate the appropriate endpoints; log and sqrt require their
full input domain. Abs/min/max preserve enclosures. Positive-base real powers may
be enclosed through log, multiplication and exp; integer powers require a distinct
validated exponent contract for zero/negative bases. Periodic or nonmonotone
functions require explicit range/extremum handling or typed unsupported admission.
NaN, invalid domains, unresolved comparisons, exponent overflow and insufficient
precision are distinguishable failures. A bounded precision ladder, if selected,
reuses storage reserved for its maximum precision and never silently changes the
program or acceptance tolerance.

F and Q endpoint projection must retain outward enclosure. DeltaQ is never formed
from independently rounded binary64 raw-Q values. The wire result contains a binary64 high/low center plus a nonnegative outward
radius per F and DeltaQ row. The radius includes endpoint and high/low projection
error. G/C contain binary64 midpoint Newton aids. The wire result must preserve
sufficient high/low and outward error information for the runtime's complete
physical residual gate; a midpoint alone is not an enclosure.

## Interpreter Implementation Contract

The composition executable delegates its three inherited descriptors (shared slab,
request notification read end, response notification write end) to the C worker
loop. It parses no model equations and owns no additional process controller.
The executable has one engine per worker, with these internal operations:

| Operation | Inputs and effect | Failure/publication |
|---|---|---|
| Prepare | Borrow immutable program/setup bytes, validate every table and binding, admit limits and reserve arena/registers | Publish engine after program/setup-input validation succeeds |
| Evaluate | Borrow candidate/reference high/low arrays and contexts; run complete model setup, instance setup and evaluation for each endpoint; execute original derivative roots; form DeltaQ; project outputs | Publish all output buffers only on status zero |
| Destroy | After ordinary completed operations, clear caches and release arena | No callback into Swift or model image |
| Worker loop | Validate shared geometry, call prepare/evaluate, enforce sequence and output publication | Resource allocator termination never resumes an engine |

The frozen compiler schema owns table layouts and type/opcode identifiers. Numeric
validation checks every byte count/range before access, table products, string
ranges, register IDs and types, block targets, phi predecessor/source pairs,
function bindings and F/Q/G/C row/column bounds. Unknown opcodes/types/callbacks
are explicit admission failures. Function evaluation begins with admitted named
inputs and complete setup bindings; uninitialized register reads are rejected.
Root records define replacement semantics explicitly rather than relying on
their ordering. Collapse guards may be serialized in the evaluation function;
the qualified compiler proves their complete value/control dependency cones use
preparation facts only. Equivalence is checked after each complete three-stage
endpoint and before output publication. The worker never infers a map from sampled
voltage values.

A charged work unit is an interpreter operation or a conservative fixed upper
bound on the explicit MPFR primitives in an arithmetic/projection operation.
It does not count opaque internal GMP iterations; the process deadline covers them.

Each block executes all phi transfers in parallel using preallocated staging
storage before ordinary instructions. The predecessor block selects each phi
source; missing/duplicate predecessor definitions fail. Every executed instruction
and every explicit MPFR primitive is charged to the request work counter, including
setup, both coordinate lanes, derivative roots and projection. A work refusal
occurs before the operation that would exceed the limit. Compiler-provided cached
setup values are represented as the results of the original setup program, never
as binary64 replacement constants.

The fixed arena is the only dynamic allocator after preparation. Program/register,
phi-stage, interval scratch, roots and projection buffers belong to that one
engine. The runtime controls process deadlines and the worker never calls a Swift
or C++ frame; allocator exhaustion terminates its process with exit 80. Exit 81
identifies an internal arena/size invariant violation; exit 82 identifies startup
protocol/handshake failure before a structured response is available. Structured
statuses 0 through 9 are owned by the runtime wire header. Failure invalidates the
whole precision session and preserves the runtime's last accepted state.

## Runtime Flows

```text
validate exact artifacts and limits -> allocate/touch fixed slabs and arena
  -> spawn dedicated worker -> install allocator -> validate program
    -> initialize register storage and validate setup inputs -> publish ready generation
      -> write bounded request -> wake worker -> execute complete program
        -> publish result generation -> signal completion -> scoped parent read
failure or timeout -> invalidate session -> terminate/reap child -> typed failure
```

Startup performs no model arithmetic under a synthetic context. Each endpoint
executes model setup, instance setup and evaluation at its explicit context and
requested precision; the work counter covers both complete pipelines.

The runtime's accepted coordinates and history are read-only inputs. A failed
worker cannot commit candidate history. The runtime rejects mismatched response
generations and requests after worker death. Restart, if requested, reconstructs
from exact program/setup/accepted inputs; it does not reuse interrupted MPFR state.

## State, Ownership, and Lifecycle

| State | Owner and isolation | Lifetime and release |
|---|---|---|
| Program and immutable setup facts | Exclusive worker process | Preparation through process exit |
| Arena, registers, MPFR caches and flags | Sole worker thread/process | Normal cleanup or OS reclamation on worker exit |
| Shared request/result bytes and pipes | Runtime process lease | Child termination/reap precedes unmap/close |
| Accepted coordinates/history | Swift solver | Updated only after the complete circuit acceptance gate |
| Candidate enclosures | Result generation | Expire on the next request or process teardown |

One session admits one outstanding request. Independent sessions use independent
workers and arenas. MPFR caches remain attached to their owning process/arena and
may be reused. Normal shutdown frees caches before arena release. Exhaustion never
tries to traverse or repair possibly interrupted MPFR objects.

## Failure, Concurrency, and Constraints

Caller-owned caps bound program bytes/instructions/registers/output counts,
precision, arena bytes, request/result slab bytes, execution work and time. All
products, alignment and offsets are checked before allocation/access. Stack and
process-image accounting must be stated separately from the owned arena; an arena
limit alone does not prove a complete resident-memory bound. The worker's external
execution timeout also covers a single MPFR call whose internal work exceeds the
program instruction budget. Timeout and cancellation are runtime-owned failures.

GMP's custom allocator must return valid memory or terminate; upstream explicitly
states that returning NULL or recovering by longjmp is unsupported. The fixed
arena services requests without further OS heap allocation. Its aligned in-band
block headers and free list support checked splitting/coalescing; reallocation
reuses an adjacent free block when possible, otherwise copies within the arena.
Fragmentation may exhaust the admitted arena before requested live bytes reach its
capacity. Register endpoints use custom MPFR significands reserved at the admitted
maximum precision; internal MPFR temporaries and caches use the same arena hooks. Arena exhaustion
terminates the worker with a reserved exit code. Parent code maps that exit to a
typed resource failure and exposes no result. Unknown signals/exits preserve their
cause as backend failures. Normal arena deallocation/reallocation handles size,
alignment, ownership and copy limits inside the admitted capacity.

Preallocated MPFR significands do not eliminate internal temporary allocation or
constant caches. A cold pilot at 192 bits on installed MPFR 4.2.2/GMP 6.3.0 observed
352 allocations and 644 reallocations for 100 exp/log/pow triples; this is only a
concrete counterexample to a zero-allocation claim, not a worst-case bound. Allocation
requests, arena high-water/copy bytes and steady end-to-end IPC/evaluation time
must be measured on the actual profile. Zero OS-heap activity may be claimed only
when instrumentation establishes it for the measured execution path.

Primary authority: [GMP 6.3 Custom Allocation](https://gmplib.org/manual/Custom-Allocation)
section 13; [MPFR 4.2.2 manual](https://www.mpfr.org/mpfr-4.2.2/mpfr.html)
sections 4.2, 4.7, 5.14 and 5.16. The installed MPFR library reports TLS enabled and
shared caches disabled; process isolation does not depend on those build options.

## Verification and Change Impact

The owning tests must cover interval containment against higher-precision
independent evaluation, difficult cancellation, exact high/low import, charge
differences below binary64 raw-Q spacing, all supported opcode domains and branch
outcomes. Generated analytic models verify complete setup-to-output flow. Exact
BSIM model programs and the IMG thermal circuit verify the actual precision path
and strict physical residual gate; arithmetic unit success is not model success.

Resource/lifetime tests force arena exhaustion during preparation and repeated
transcendentals, worker crash, timeout/cancel, malformed program/slab/generation,
normal shutdown and concurrent independent sessions. The parent must retain its
last accepted state, expose no failed result and reap every failed worker. The qualified helper build links the installed MPFR/GMP static archives, recording
archive/header/compiler/source/worker identities in a reproducible receipt. The
ordinary SwiftPM product uses pkg-config dynamic linking and requires separate
qualification if selected. Native
sanitizer checks and actual allocation/copy/IPC measurements cover the C ownership
boundary. All test commands have external timeouts. Compiler opcode or constant
changes require numeric, runtime and CoreSpice gate evidence to be rechecked
together. The task ledger is [CoreSpice Progress](../../../CoreSpice/PROGRESS.md).

The canonical numeric-profile.json is sorted compact UTF-8 without availability
paths. It binds numeric/wire/program versions, target ABI, numeric archive/header
identities, repository-relative source/header identities, compiler identity and
normalized semantic flags, and worker.sha256. Its SHA is distinct from the full
path-bearing build receipt and compiler semantic profile. External qualification
checks both profile bytes and the embedded worker image identity; a caller-supplied
digest alone does not establish qualification.
