# Constitutive Identity Values

## Purpose and Scope

Parent: [OSDIContracts](../DESIGN.md). No children. This portable component owns
the F/Q identity kind and explicit construction-limit values. It performs no model
evaluation and imports no native runtime or process implementation.

## Responsibilities and Boundaries

`OSDIPrecisionConstitutiveKind` identifies the complete original resistive or
reactive raw-row axis. `OSDIPrecisionConstitutiveLimits` carries caller-selected
node, atom, coefficient-bit and fixed-workspace capabilities. The meaning and
proof authority of an identity belong to the [source algorithm](../../COSDIPrecision/ConstitutiveIdentities/DESIGN.md#contracts-and-invariants);
these values alone grant no identity, rank, root or history authority.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [OSDIContracts](../DESIGN.md) | parent | Portable value composition | Module owner | No host imports |
| [Source algorithm](../../COSDIPrecision/ConstitutiveIdentities/DESIGN.md) | semantic reference | Exact source-linear identity scope | Owns algebra and checker authority | A value is not a proof |
| [Runtime](../../OSDIRuntime/ConstitutiveIdentities/DESIGN.md) | used by | Preparation and borrowed result protocols | Consumes these values | Native execution is separate from portable representation |

## Architecture

```text
caller-selected capabilities -> checked immutable limits
F/Q kind + limits -> runtime preparation protocol
```

## Contracts and Invariants

- Kind raw values are resistive = 1 and reactive = 2.
- Nodes and atoms are positive and strictly below `UInt32.max`.
- Coefficient-bit and workspace-byte ceilings are positive `UInt64` values.
- Invalid limits throw `OSDIContractError.invalidLimits`; there are no guessed
  defaults or silent reductions of a requested capability.
- Values are immutable, `Sendable`, and platform independent. Native support is
  not inferred from the ability to construct them.

## Verification and Change Impact

[Public identity tests](../../../Tests/OSDIEvaluationTests/OSDIPrecisionConstitutiveTests.swift)
exercise these capabilities through preparation and refusal paths. Changing a
kind encoding or limit domain requires coordinated runtime, process geometry and
worker projection checks through their linked designs. It does not change the
source algorithm's proof scope by itself.

## Explicit Coordinate Condition

The owning `OSDIPrecisionCoordinateQuotient` value retains a canonical UInt32 table
with NONE ground and minimal-index representatives. Its initializer checks the
actual topology, all aliases' units, unknown/residual natures and isFlow, and
rejects new ground/alias conditions on flow coordinates. Identity construction
accepts only the same topology at preparation. No model evaluation or equality
of sampled numbers determines the table. Caller-owned immutable Array storage is
retained; session admission accounts for its copies before native acquisition.
The mathematical scope is owned by the linked source algorithm, not this value.
