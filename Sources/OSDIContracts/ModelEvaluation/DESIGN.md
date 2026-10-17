# Model Evaluation Values

## Purpose and Scope

Parent: [OSDIContracts](../DESIGN.md). No children. Own typed preparation inputs,
local topology, parameter facts, explicit phase/time context and noise-source facts.

## Responsibilities and Boundaries

Represent explicit assignments, temperature in Kelvin, connected terminals,
caller budgets and prepared local coordinates. Preserve parameter kinds, types,
aliases and explicit-given facts without inferring device defaults. No native
pointer, model equation, solver, serialization or implicit internal-node solution.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [Point Evaluation](../../OSDIRuntime/PointEvaluation/DESIGN.md) | used by | Typed preparation and local facts | Foreign execution composition | Runtime publishes facts only after successful setup |
| [Device Evaluation](../../COSDI/DeviceEvaluation/DESIGN.md) | coordinates with | Native setup and raw F/Q/G/C | ABI meaning | Numeric defaults come from generated setup/access functions |

## Architecture

```text
scoped typed assignments + simulation parameters + Kelvin + terminal count + limits
 -> validated preparation options
native setup -> parameter metadata/effective values/given flags + local topology
local topology: raw descriptor node -> dense active unknown or ground
```

## Contracts and Invariants

Scalar real, Int32 and string assignments retain exact types. An instance
parameter can also be supplied as a model default; a model parameter cannot be
supplied as an instance override. Alias resolution and duplicate/ambiguous names
are checked at preparation. Arrays are an explicit unsupported assignment
capability in the initial profile, not truncated to a scalar. Unspecified device
and simulation parameters retain model-defined behavior; no guessed defaults are
written by these values.

Temperature must be finite and positive. Every active unknown, including flow or
thermal/internal variables, must be supplied explicitly in prepared dense order.
Node units, residual units, flow classification and nature references remain
metadata. Ground has no active index. Raw node indices are contiguous, active
indices cover the prepared dimension and state counts are nonnegative.

The fixed S2 point profile requests resistive/reactive residuals and Jacobians,
operating-point calculation, DC/static analysis at time zero, with limiting off.
Raw F/Q have N entries and raw G/C are row-major N by N in descriptor order.
They are not a reduced circuit equation. Previous state is an explicit input;
next state is a candidate result with no automatic acceptance. Delay/history
analyses and circuit solving are outside this profile.

Workspace, diagnostic and string limits are caller policy. The native workspace
limit covers its checked persistent allocations and temporary peak; sublimits
are included in that total. These limits do not claim to bound mapped libraries,
Swift runtime overhead or all process memory. The existing load metadata/read
budgets continue to apply independently.

## Verification and Change Impact

[OSDIEvaluationTests](../../../Tests/OSDIEvaluationTests/DESIGN.md) owns the
behavioral verification of these values and refusals.

Actual generated parameter fixtures check defaults, model/instance scope, aliases,
types and given flags. Analytic RC and an independent direct-ABI BSIM harness check
all raw values and topology. Finite differences use alias-consistent active
coordinates and independently reduced Jacobians. Value and dimensional refusals
must reach API and CLI as failures. Changes affect both owning execution designs.

## S4 Context and Noise Values

`OSDIEvaluationContext` owns a validated mode, nonnegative finite absolute seconds
and a positive finite noise frequency only for noise mode. Modes are DC, AC,
transient, explicit transient initialization, and noise. Initialization is at zero
and is independent of how the caller selected OP or UIC initial values.
`OSDINoiseSourceInfo` retains stable source index/name/kind and raw positive/negative
node indices; nil negative index is ground. Equation units/natures come from the
prepared node metadata. Sources are independent PSD channels, not correlated pairs.
