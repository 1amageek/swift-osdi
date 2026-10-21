# Compiler Precision Program

## Purpose and Scope

This child of [COSDI](../DESIGN.md) owns the optional compiler program format in
[OSDICompilerPrecision.h](../include/OSDICompilerPrecision.h). The program is a
separate extension to the unchanged OSDI 0.4 ABI. It has no children.

## Responsibilities and Boundaries

OpenVAF owns expression lowering, parameter defaults, topology predicates and
automatic differentiation. The numeric worker owns interval evaluation of the
emitted program. The runtime owns verified-image extraction, exact preparation
facts, process lifetime and bounded transport. Swift owns circuit equations,
Newton iteration, integration, acceptance and compensated solution coordinates.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [COSDI](../DESIGN.md) | parent | Native module boundary | Optional artifact inspection | OSDI 0.4 is unchanged |
| [ProgramEvaluation](../../OSDIPrecisionWorker/ProgramEvaluation/DESIGN.md) | coordinates with | Source-observable value validity | Inactive arithmetic and proved pure control regions | Numeric owner defines and independently validates demand/failure semantics |
| [DeviceEvaluation](../DeviceEvaluation/DESIGN.md) | coordinates with | Admitted preparation and precision request | Image and preparation identity | Native caches are not precision constants |
| [Compiler profile](../../../../swift-openvaf/Tools/OpenVAFCompilerProfile/DESIGN.md) | depends on | Exact patched compiler | Source of programs and AD roots | Changes require new compiler and model identities |

## Architecture

```text
frontend MIR (decoded primitive literals, before f64 folding)
  -> parameter setup + complete DAE + original AD
    -> typed SSA/CFG program + root/mapping tables
      -> digest-verified optional image export
        -> bounded isolated interval worker
          -> F, delta-Q, G, C and error bounds
```

## Contracts and Invariants

Version 1 is stateless. Primitive real constants are the exact binary64 bits
produced by frontend literal decoding; compound operations remain instructions.
Integer constants are signed 32-bit values. String constants are exact UTF-8.
The precision path does not import compound values from native setup caches.
The header semantic-profile digest identifies a fixed canonical schema/semantics
profile supplied to compilation; external receipts bind actual compiler, patch,
source files and model image. It is not a self-hash of the generated artifact.

Programs have model-setup, instance-setup and evaluation functions. Each function
has disjoint globally indexed SSA registers, constant definitions, input bindings,
blocks and result roots. A prior-stage-root binding reads a completed setup root
without conversion. Setup fact bytes are declared in the same header;
simulation facts have explicit presence and source-level fallback control flow. Model and instance setup run in that order for each endpoint;
evaluation then reads their exact interval results. Parameter inputs retain scope,
value and given facts; model-level instance defaults precede instance overrides.

### Additive Typed Simulation-REAL Bindings

The 1.2.0 candidate adds input kinds 13 (`SIM_REAL_VALUE`, REAL destination) and
14 (`SIM_REAL_GIVEN`, BOOL destination) to the existing closed program grammar.
Both use the exact UTF-8 name offset/length and search only explicitly given REAL
simulation facts. Missing or same-name non-REAL facts yield false presence and
neutral value zero; explicit REAL zero yields true presence and zero. The source
SSA alone selects a fallback or demands its original fatal condition. No SDK
default parameter, numeric conversion or hidden model-specific value is added.
This matches an ordinary ABI callback that searches only `names`/`vals`, never
`names_str`/`vals_str`. Matching is case-sensitive exact bytes. Ordinary preparation
continues to reject duplicate exact names across types.

Kinds 8 (strict same-name typed value) and 12 (type-independent given) keep their
existing meaning. Program ABI 1, export/header/record/setup layouts, flags and all
protocol 1--5 wire layouts remain unchanged. An older worker's closed kind<=12
admission rejects a new binding with INVALID_PROGRAM before any output publishes;
there is no fallback to an old binding. Programs that do not use the new kinds
retain their old bytes and semantics. The producing compiler/source profile must
use a new semantic-profile digest and new program/model artifact identity when
adopting this grammar; an old digest cannot be reinterpreted as the new profile.

The same candidate adds kind 15 (`RAW_PARAMETER_GIVEN`, BOOL destination,
parameter-table index, aux zero). It reads only that exact scoped record's
immutable `given` bit. Model-stage use requires a model-scope record, as for
existing parameter inputs. Missing and non-given facts are false; explicit zero
or an assignment equal to a model value is still true. This query neither reads
resolved values nor consults the model peer. Existing kind 2 retains its
instance-or-model presence semantics. Both facts are necessary when original
source guards or callbacks distinguish an explicit instance assignment from a
model-scope fallback. The source snapshot already retains separate scoped facts;
no setup record or wire layout changes. The new producer profile binds use of
kind 15, and an older worker refuses it before publishing any result. The source
checker treats the BOOL as guard authority replayed from those facts, never as a
REAL coefficient obtained from an endpoint.

Setup capture retains original names, types, presence and exact value bits without
coercing or removing STRING facts. The independent constitutive source checker
uses exact immutable setup REAL bits (or the defined neutral zero) for kind 13
literal coefficients; it never imports MPFR endpoints or native resolved caches.
New bindings run in all three source stages through the same VM and existing
work/storage/cancellation/cleanup owners. Qualification must exercise the real
point/range/chart/cover/identity worker paths, legacy refusal, source fallback
versus REAL-zero fatal behavior and cold immutable snapshot fidelity.

The owned regressions are
[OSDIPrecisionSimulationRealTests](../../../Tests/OSDIEvaluationTests/OSDIPrecisionSimulationRealTests.swift)
and its bounded [fixture generator](../../../Tools/OSDIPrecisionVerification/simulation_real_fixtures.py).
They require explicitly selected new/old real workers and their matching numeric
profile digests. The snapshot library uses the existing analytic consumer fixture;
it tests cold byte/lifetime preservation, not official-model physics. Separate
producer receipts retain the original STRING/missing fallback and REAL-zero fatal
evidence. Source release qualification must not replace those receipts with the
analytic fixture.

Raw node indices identify the ordinary descriptor coordinates. Input voltage or
current differences use explicit raw positive/negative endpoints; UINT32_MAX is
ground. The runtime supplies the admitted raw-to-active mapping. The shared precision compiler path applies the upstream operating-point value
and control-dependency classifier before binary64 optimization. Only preparation-only
collapse hints and implicit-collapse outputs survive in both the ordinary image
and precision program. Voltage, time, analysis, integration and limiting dependent
hints never become static topology through later arithmetic simplification. The
program retains surviving source guards, including native auxiliary-current
collapse implications; after all three stages at each endpoint, their equivalence
classes must equal the admitted mapping before evaluation succeeds. Matching counts alone is insufficient.

Arithmetic instructions have at most two value operands. SELECT has a boolean
condition and two same-typed alternatives; its source fallback expression keeps
the original eager MIR evaluation order. Phi edges select the value associated
with the predecessor block and publish all phi destinations simultaneously.
Block terminators implement jump, boolean branch or function return. Opaque calls
are forbidden: known system facts become typed inputs, arithmetic is explicit,
and unsupported callbacks cause compiler refusal. Every live instruction and
input must belong to the closed supported profile before artifact publication.

F and Q roots replace one raw row; G and C roots replace one raw matrix entry.
Missing roots mean exact zero, never uninitialized data. Duplicate axis roots are
invalid. Compiler-owned derivative roots are evaluated at the same compensated
point and branch as F. Candidate and reference endpoints are evaluated separately;
the worker encloses their Q difference without binary64 subtraction.

## State, Ownership, and Lifecycle

The image retains the immutable exported program. Extraction copies it into
checked runtime-owned storage before the image may close. Program registers,
interval values and arithmetic temporaries belong to the isolated worker. No
program pointer, native storage offset or callback address crosses the transport.
Each request starts from explicit endpoint facts; accepted-Q history is not an
implicit worker cache.

## Failure, Concurrency, and Constraints

Invalid version, lengths, strides, types, register references, stage bindings,
phi predecessors, roots, topology, state slots, unsupported opcodes or callbacks
reject admission. Ambiguous interval predicates, domain failure, exhausted
precision/work/storage and abnormal helper termination remain explicit failures.
Arithmetic validity follows the source-observable policy owned by
[ProgramEvaluation](../../OSDIPrecisionWorker/ProgramEvaluation/DESIGN.md); inactive
undefined temporaries are not fabricated finite intervals. The semantic-profile
identity binds this policy. No failed candidate is published or retried by silently shrinking its timestep.
The helper's fixed arena is process-owned; exhaustion terminates that helper and
becomes a typed runtime failure. The ordinary zero-allocation lane is distinct.

## Verification and Change Impact

Format tests reject malformed programs and verify simultaneous phi semantics,
setup defaults/given precedence, topology equality and every emitted opcode.
Binary-exact nonlinear sub-ULP fixtures distinguish evaluation at hi+lo from
F(high)+G(high)*low. All four unchanged official models require regenerated
artifacts, exact source/compiler/program identities, precision-aware independent
residual evidence and ordinary-path regressions. Header layout changes require
compiler, numeric worker and runtime agreement before implementation.

## Source-Faithful Compiler Tap

The precision sibling of `sim_back::CompiledModule::new` begins with
`Context::new` and performs CFG construction without `optimize(Initial)`.
It runs `Topology::new(..., full_residuals=true)` and `DaeSystem::new`, including
existing `mir_autodiff::auto_diff`, then exports before post-derivative f64
optimization and `Initialization::new` cache splitting. Topology's local
`simplify_cfg_no_phi_merge` only removes constant-boolean terminators and
structurally redundant CFG/phi nodes; it does not evaluate real arithmetic.

`hir_lower::HirInterner::insert_param_init` emits given/default branches and phi
nodes using the normal expression lowerer. Separate unoptimized model and instance
setup functions preserve all default expressions and model-level instance defaults.
Two precomputed builtin compounds require the precision lowering flag:
`hir_lower/src/expr.rs` limexp's `ln(1e30)` cutoff becomes an explicit Ln instruction,
and vt's `KB/Q` becomes explicit Fdiv of the decoded primitive constants. Ordinary
lowering is unchanged. Frontend literal decoding remains the accepted baseline.

The full source inventory for BULK/CMG/IMG/SOI requires basic arithmetic, sqrt,
exp, expm1, ln, log10, pow, sin, cos, tan, atan, sinh, cosh, tanh and asinh, plus
integer/boolean casts and comparisons, control flow and phi nodes. Abs/min/max are
lowered control flow. The emitter records the actual closed opcode subset;
unused opcode numbers in the header do not imply numeric support.

Compiler-injected `ParamSysFun` leaves carry `HAS_PRIMITIVE_DEFAULT` and exact
primitive bits from upstream `hir_def::ParamSysFun::default_value`: mfactor,
hflip and vflip default to 1; position and angle default to 0. The worker applies
explicit instance value, explicit model value, then that declared primitive.
Unprovided setup facts remain zero. Original model parameter defaults never use
this flag and always retain their full source SSA/default/given branches.

## Structural Topology Preparation

Before either precision lane captures topology value handles, run the existing
CFG-only simplifier and source-output liveness pass. Removing structurally trivial
phi nodes and literal-boolean branches is allowed; evaluating floating-point
compound expressions is not. Topology stores copied MIR value handles, so a later
DFG use rewrite cannot repair already captured stale branch-nature handles.
Conditional pure-current contributions must keep discipline-flow residuals without
spurious mixed-nature auxiliary rows. A genuine voltage/current switching row
retains `Switch`/unknown residual nature; no fixed unit may be guessed from the
coordinate's unknown nature. Electrical and thermal metadata tests own this
boundary, followed by actual SOI native/program coordinate and metadata capture.

The existing HIR `Expr::is_zero` recognizes only primitive integer/real zero
literals. In precision lowering, this source-owned fact produces canonical
`F_ZERO` contributions directly. An integer-zero-to-real cast must not turn a
zero-potential collapse-only branch into a mixed-nature source equation merely
because arithmetic folding is disabled. Compound zero expressions are never
recognized by this rule and keep their full source operations. The ordinary
flag-off lowerer is unchanged.

## Analysis Fact Type Bridge

The program `ANALYSIS` input is a Boolean fact. The original OpenVAF
`CallBackKind::Analysis` returns an I32 (ordinary OSDI evaluation declares
`cx.ty_int()`), which source conditions and arithmetic subsequently convert.
The serializer binds the fact into a separate Boolean register and emits BICAST
into the original I32 result register. Original IBCAST/IFCAST consumers retain
their source types. This is an encoding correction within the existing semantic
profile and opcode set, not a worker ABI or model equation change.
