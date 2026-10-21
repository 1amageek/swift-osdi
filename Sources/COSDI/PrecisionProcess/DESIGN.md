# Precision Process

## Purpose and Scope

Parent: [COSDI](../DESIGN.md). Own bounded native child/slab/pipe
resources and local IPC for the separate precision session. This component does
not evaluate model arithmetic or own solver/history state. One immutable operation
selects compensated-point protocol 1 or smooth-box protocol 2 at cold creation.

Child: [DecisionCover](DecisionCover/DESIGN.md) owns protocol-4 record projection and complete-response checking.
Child: [ConstitutiveIdentities](ConstitutiveIdentities/DESIGN.md) owns protocol-5 exact source-relation projection and complete-response checking.

## Responsibilities and Boundaries

Admit exact worker/program/setup identities, create one isolated persistent helper,
transport explicit endpoints and expose only complete matching results. Own
cancellation, deadlines, process failure, cleanup and exactly-once resource release.
The numeric worker alone owns MPFR/GMP arithmetic and its fixed arena.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [COSDI](../DESIGN.md) | parent | Public native module | C process boundary | Upstream model ABI stays fixed |
| [CompilerPrecision](../CompilerPrecision/DESIGN.md) | depends on | Immutable program/setup format | Original model function | No rounded native-cache constants |
| [Numeric worker](../../OSDIPrecisionWorker/DESIGN.md) | coordinates with | Fixed-arena interval interpreter | Model arithmetic only | Worker failure publishes no candidate |
| [PointEvaluation](../../OSDIRuntime/PrecisionExecution/DESIGN.md) | used by | Noncopyable precision owner and scoped views | Swift lifetime | All I/O outside Mutex |
| [Causal Range Boundary](../../../DESIGN.md#causal-range-boundary) | parent requirement | Separate smooth-box constitutive operation | Preserves the complete spatial/time box | Spatial G/C does not certify explicit time partials |

## Architecture

```text
verified image program + setup facts + exact worker
 -> separate precision session -> shared slabs + bounded pipe wake
 -> isolated arithmetic worker -> matching acquire-observed response
 -> scoped raw centers/radii -> consumer
 failure -> invalidate -> kill/reap -> retained cleanup if incomplete
```

## Contracts and Invariants

### S4D Precision Execution — D1 Contract

The parent selects an isolated persistent C helper for model arithmetic only.
Swift MNA, Newton, integration and acceptance remain in process. Public numerical
semantics are owned by [Model Evaluation](../../OSDIContracts/PrecisionEvaluation/DESIGN.md).
The parent D1 contract is frozen. Behavioral evidence qualifies the implementation separately. The upstream
OSDI 0.4 header and ordinary binary64 entry points retain their existing meaning.

The compiler emits versioned immutable precision program DATA from source-level
setup and full DAE/derivative SSA before binary64 folding/cache splitting. It owns
`OSDICompilerPrecision.h` and optional `OSDI_PRECISION_V1` descriptor-indexed program
exports. The worker does not dlopen the model. The parent binds extracted program
bytes to the already verified image/descriptor and compiler/source-profile identity.
The artifact cannot embed its own final digest; loader/receipt provenance owns that
binding. Unsupported opcodes/callbacks and mismatched program identities are failures.

The helper is the sole GMP/MPFR allocator authority. It installs fixed-arena hooks
before constructing any numeric object. Arena exhaustion terminates the helper
through a dedicated exit code; no NULL allocator return, longjmp or exception
recovery is used. The parent discards every partial result, kills/reaps an invalid
worker and reports typed failure. This resolves the unsafe in-process recovery
assumption described by [GMP](https://gmplib.org/manual/Custom-Allocation) and
[MPFR 4.2.2](https://www.mpfr.org/mpfr-4.2.2/mpfr.html). Numeric libraries and their
profile are exact admitted artifacts, not guessed libm-error allowances.

The helper implements correctly rounded directed intervals, including setup,
nonlinear operations and predicates. V1 supports stateless descriptors only and
explicit transient initialization/transient phases. Each endpoint has its original
mode/time. Ambiguous branch, domain, precision/work exhaustion and arena failure
cannot publish a candidate. G/C are midpoint approximations from original
derivative SSA at the same compensated candidate. F and delta-Q retain high/low
centers plus conservative radii through transport.

## Runtime Flows and IPC Layout

Runtime owns `COSDI/include/OSDIPrecisionProtocol.h`. Numeric worker and compiler
consume that header; compiler alone owns the program-format header. These wire
records are proposed until the parent cross-owner layout freeze. They are local
host transport, not the upstream model ABI or an unversioned serialization format.

| Slab region ID | Content / element count | Mutating owner |
|---|---|---|
| 0 | Program bytes, compiler-format byte count | Parent before launch, immutable thereafter |
| 1 | Setup/parameter bytes, compiler-format byte count | Parent before launch, immutable thereafter |
| 2 / 3 | Candidate high / low, M binary64 values each | Parent before request publication |
| 4 / 5 | Accepted-reference high / low, M values each | Parent before request publication |
| 6 / 7 / 8 | F high / low / absolute radius, N each | Worker before response publication |
| 9 / 10 / 11 | Delta-Q high / low / absolute radius, N each | Worker before response publication |
| 12 / 13 | G / C derivative midpoints, N*N each | Worker before response publication |

Proposed control records use only fixed-width fields, with native binary64 and
explicit byte-order/version admission:

```c
typedef struct cs_osdi_precision_region_v1 {
    uint64_t byte_offset;
    uint64_t element_count;
} cs_osdi_precision_region_v1;

typedef struct cs_osdi_precision_context_v1 {
    uint32_t mode;
    uint32_t reserved;
    double time_high;
    double time_low;
} cs_osdi_precision_context_v1;

typedef struct cs_osdi_precision_control_v1 {
    uint64_t magic;
    uint32_t protocol_version;
    uint32_t header_bytes;
    uint64_t mapping_bytes;
    uint32_t byte_order;
    uint32_t descriptor_index;
    uint64_t active_count;
    uint64_t raw_node_count;
    uint64_t arena_bytes;
    uint32_t maximum_precision_bits;
    uint32_t minimum_precision_bits;
    unsigned char program_sha256[32];
    unsigned char worker_profile_sha256[32];
    cs_osdi_precision_region_v1 regions[14];
    uint64_t request_sequence;
    uint64_t response_sequence;
    cs_osdi_precision_context_v1 candidate;
    cs_osdi_precision_context_v1 reference;
    uint32_t requested_precision_bits;
    uint32_t response_status;
    uint64_t maximum_work_units;
    uint64_t completed_work_units;
    uint32_t used_precision_bits;
    uint32_t model_flags;
    uint32_t has_maximum_timestep;
    uint32_t response_detail;
    double maximum_timestep;
} cs_osdi_precision_control_v1;
```

M is prepared active count; N is raw descriptor node count. Byte regions use
stride one; numeric regions use sizeof(double). All offsets are checked for
overflow, range, alignment and overlap before mapping/publication. Mapping size
and actual fd size must agree. Reserved fields are zero. Endianness, binary64
format, struct sizes and lock-free aligned 64-bit atomics are admission conditions.
The sequence fields are plain fixed-width storage accessed exclusively through
C acquire/release atomic operations, never unsynchronized Swift loads.

Startup sets request sequence zero and response sequence UINT64_MAX. Only after
program validation and precision setup does the worker release-publish response
sequence zero/status success and send the ready wake. The parent requires both
the ready wake and acquire-observed sequence zero. Evaluation sequences run from
one through UINT64_MAX-1; wrap is refused.

One request is in flight per exclusive session. Parent writes inputs/context,
release-publishes a strictly increasing nonzero request sequence and writes a
wake byte. Worker acquire-loads that sequence, computes into result regions,
release-publishes matching response sequence only after all output/status fields,
then writes its wake byte. Parent acquire-loads and validates the matching response
before publication. Stale, duplicate, mismatched or partially written responses
are protocol failures. Pipes are notifications, not a substitute for memory ordering.
No per-point JSON or rematerialized program/model data is used.

## State, Ownership, Lifecycle and Failure

The numeric library exposes only
`int cs_osdi_precision_worker_run(int slab_fd, int request_read_fd, int response_write_fd)`.
A separate worker executable calls it with inherited child fds 3 (slab), 4
(parent-to-child read) and 5 (child-to-parent write); argv contains only argv0.
The parent uses a close-on-exec whitelist and explicit dup2 actions for those fds.

C uses posix_spawn with explicit fd actions, private exact worker path, a controlled
environment and a separately owned child/process group. No fork-only Swift state,
global SIGCHLD handler or waitpid(-1) is introduced. Shared slabs and pipes are
created once and retained for the helper lifetime. Program/worker/numeric-library
digests, versions, configuration and process outcomes belong to preparation/run
provenance. Dynamic numeric-library availability must be bound if not statically
incorporated into the qualified worker.

Precision evaluation stays synchronous for the existing solver contract. Bounded
poll slices inspect the monotonic deadline and a Swift Task cancellation C probe.
The new Swift bridge uses fixed-toolchain @c/@implementation matching. Spawn, wait,
probe, foreign evaluation, client callbacks and destruction all run outside Mutex.
Timeout/cancel/crash/resource/protocol failure invalidates only the precision
session, publishes no view and never implicitly respawns. Ordinary outer model
ownership remains until explicit close.

Parent kills and reaps only its exact owned child/group. Cleanup itself has a
bound; incomplete cleanup retains a retryable owner/identity and dependent
resources, rather than forgetting an unreaped helper. Slab views cannot outlive
that owner. Successful close reaps the worker before unmapping/releasing its lease.

The requested precision is within the program/profile minimum and configured
maximum. The configured work cap is fixed for the session and must be positive.
Startup only validates/allocates; it does not evaluate model setup under zeroed
contexts. Each evaluation has a fresh counter covering model setup, instance setup
and evaluation for both original endpoint contexts, plus derivatives and projection.
No compound setup result is reused across endpoints or precision settings. response_detail is an
offending instruction ordinal, or UINT32_MAX when not applicable.

The program/setup data includes prepared raw-to-active mapping and complete
parameter/default/given, temperature and simulation-input semantics. A program
identity cannot stand in for those resolved preparation inputs. The worker-profile
digest binds numeric algorithm/profile and exact library identities; the separate
external receipt binds the final worker binary digest, avoiding a self-digest cycle.

Known parent allocations and all geometry/bit arithmetic are admitted against
explicit budgets. The worker arena has a hard byte limit and its own exit-on-
exhaustion authority. Work units count admitted program operations; the parent
deadline bounds opaque library execution through helper termination. Zero total
per-point allocation is not assumed: slab copies, parent bookkeeping and numeric
allocator behavior require measurement. A bound result uses a proven positive lower
endpoint or explicit absence, and never a value from a different branch/context.

Proposed numeric status values are success=0, invalid-input=1, invalid-program=2,
unsupported-profile-or-opcode=3, domain=4, unresolved-branch=5,
precision-exhausted=6, instruction-budget=7, nonfinite-projection=8 and
numeric-invariant=9. Only success authorizes result-span publication. The dedicated
worker exit range reserves 80 for fixed-arena exhaustion, 81 for internal arena/
size invariant failure and 82 for protocol/program handshake failure before a
response is possible. Exit 80 maps to typed resource exhaustion; other unknown
exits/signals preserve their backend-failure cause. Structured nonzero statuses
invalidate the session and are not ignored simply because the process survives.

Numeric owner agrees with these regions, control/status/exit fields, startup
sequence and work semantics. Before implementation, compiler and parent must freeze
magic, byte program/setup semantics and exact header layout above.

## Verification and Change Impact

[Evaluation tests](../../../Tests/OSDIEvaluationTests/DESIGN.md) own
version/geometry/refusal, same-sequence publication, state/phase identity,
partial worker failure, arena/work/precision exhaustion, timeout/cancel, reap/retry,
lease lifetime, nonlinear sub-ULP reference comparison and unchanged-tolerance
four-family integration.

Darwin shared-memory allocation rounds the complete slab to its host page size before budget admission. `shm_open` uses supported flags and FD_CLOEXEC is applied afterward. Parent request writes use fd-local F_SETNOSIGPIPE; EOF exit classification waits only within the existing deadline, then exact child cleanup remains mandatory.

## Smooth-Box Process Operation

`cs_osdi_range_process_create` has the same arguments and admitted resources as
`cs_osdi_precision_process_create`, but fixes local IPC protocol version 2 before
mapping geometry is computed. Both return the same opaque exclusive owner and
use the same close/retained-cleanup path. No second spawn, mapping, wait or reap
implementation is introduced. Protocol 1 retains its existing header bytes,
regions, numeric semantics and publication behavior.

`cs_osdi_range_process_evaluate` receives lower-high/lower-low and
upper-high/upper-low active-coordinate planes, a count, lower/upper absolute-time
contexts, requested precision and cancellation probe. One internal request path
uses an explicit operation to select admission and result validation. Calling
either operation on an owner created for the other fails before copying inputs,
publishing a sequence, writing a wake byte or invoking a cancellation probe. As
with other invalid requests, the session is invalidated and remains closeable.

| Protocol-2 slab ID | Elements | Meaning |
|---|---|---|
| 0 / 1 | Program/setup bytes | Existing immutable inputs |
| 2 / 3 | M doubles each | Lower high/low coordinates |
| 4 / 5 | M doubles each | Upper high/low coordinates |
| 6 / 7 | N doubles each | F lower/upper |
| 8 / 9 | N doubles each | Absolute Q lower/upper |
| 10 / 11 | N*N doubles each | Spatial G lower/upper, row major |
| 12 / 13 | N*N doubles each | Spatial C lower/upper, row major |

The control layout remains 464 bytes with 14 regions. Protocol version is the
immutable discriminator; candidate/reference context storage carries lower/upper
contexts only for version 2. All coordinate/time components must be finite and
normalized, lower must not exceed upper as an exact twofold value, both contexts
must have the same admitted mode, and requested precision/counts must match the
existing session limits. Normalized twofold ordering compares high components,
then low components when highs agree; monotone binary64 rounding guarantees that
different normalized highs preserve the order of their exact sums. Aliased input
planes are permitted and do not change the mathematical box. Prepared raw-node
aliases and topology remain the validated program/setup/worker authority.

```text
immutable operation + geometry
 -> validate operation, box, mode/time, count and precision
 -> copy four planes -> release request -> wake -> acquire matching response
 -> validate immutable header, finite ordered F/Q/G/C and resource facts
 -> publish one complete scoped range view
 failure -> empty view + invalid owner -> exact-child retained cleanup
```

Every published lower/upper plane must be finite and ordered for all N or N*N
entries. Existing flags, maximum-timestep, precision and work checks remain in
force. Worker status 10 maps to the appended process issue code 12 (nonsmooth)
for version 2 only and never authorizes a partial view; protocol 1 continues to
reject unknown statuses. Existing process IO=10 and unsupported=11 remain fixed.
The same startup/evaluation deadlines, cancellation observations, fixed storage,
sequence validation, invalidation and retained cleanup apply to both operations.
No client callback, foreign operation, I/O or destruction is performed under a
lock by this component.

The range result exposes named `f_lower/f_upper`, `q_lower/q_upper`,
`g_lower/g_upper`, `c_lower/c_upper` borrowed pointers, raw-node count, used
precision, completed work, request sequence and the existing maximum-timestep
facts. After the worker's full program validation and ready handshake succeed,
the parent safely bounds-checks and scans the immutable compiler input section
for the TIME input kind. It retains the conservative `program_has_time_input`
fact in the owner and every successful range view, including inactive TIME
inputs. No additional wire field or claimed explicit time partial is created.
The causal consumer must refuse a true fact until explicit-time derivative
authority exists.

Verification adds distinct version/geometry checks, operation-mixing refusal
before cancellation, finite and reversed coordinate/time boxes, mode mismatch,
ordered matrix validation, explicit-time program facts, nonsmooth atomic refusal,
timeouts/cancellation/resource failures and retained owner cleanup. Existing
version-1 API/worker fixtures remain regression evidence; successful range
fixtures certify only the stated local constitutive boxes.

## Conditional Chart Process Operation

`cs_osdi_chart_process_create/evaluate` selects protocol 3 in the existing exclusive owner. The extended control begins with the unchanged 464-byte v1 record, uses the range interpretation for regions 0..13, and appends immutable tape/guard region geometry and limits followed by mutable chart request/response fields. Old versions keep their exact header size. Complete immutable suffix geometry is checked at startup and every response; no prefix-only comparison admits a changed limit or region.

Wire decisions and guards are fixed-width, pointer-free records with explicit zero reserved fields and compile-time size/offset checks. Native interpreter records are field-projected, never copied by native struct layout. All sixteen regions are aligned, in bounds and pairwise disjoint. Checked count/stride arithmetic precedes slab/arena allocation; tape/guard capacity contributes to mapping policy and native scratch to the fixed worker arena.

Chart work counts are cumulative from program admission, and source steps are cumulative across every successful request in that session. Replay consumes remaining quotas; recording does not reset them. The parent verifies monotonic counters and exact limits. The worker uses an explicit validated local range-control view solely to call the existing conditional interpreter; public protocol 3 is never silently treated as a smooth-box request. Statuses 100/101/102 are chart-only tape/capacity/step refusals; existing statuses and issue values remain unchanged. No unsuccessful response authorizes plane/guard borrowing. Existing deadline, cancellation, atomic sequence publication and exact child cleanup remain the sole lifecycle.

Verification covers full suffix/region/version validation, source-ordered tape replay, typed refusals and cumulative quotas, fixed borrowed region lifetime, failure atomicity and old wire regression.

### Tagged Primitive Infinity Guard Values

Chart operand value kinds 5/6 denote exact source primitive negative/positive infinity. The existing integer payload holds the immutable compiler constant-record index for those kinds; their binary64 endpoints must both be the matching signed infinity. Finite REAL kind 1 still requires finite ordered endpoints. Non-real kinds keep their previous meanings. The parent retains the validated constant section at admission and checks each tagged payload by bounded O(1) indexed access to its exact type/bits; no per-guard program search or copied provenance graph is introduced. NaN, forged primitive indices/signs, finite projection overflow and nonfinite F/Q/G/C remain refusals. Wire record/header sizes, old versions and cumulative quota semantics stay unchanged.
