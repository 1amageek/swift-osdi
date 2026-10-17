# Actual Point Evaluation Fixtures

## Purpose and Scope

Own small real Verilog-A inputs for S2 F/Q/G/C and typed preparation behavior.
Parent: [S2 tests](../DESIGN.md). Native metadata-only S1 fixtures are not reused.

## Responsibilities and Boundaries

Linear RC models supply independent analytic matrices and distinct descriptor values.
A typed RC model exercises real/integer/string parameters, aliases, model defaults
for instance parameters, explicit instance overrides, given facts and temperature.
A selectable internal connection exercises setup-time collapse. A voltage-defined
branch supplies a flow unknown. `auxiliary_coupling.va` supplies one internal noise
auxiliary with physical-bias-dependent F/Q coefficients and physical residual/charge
coupling, evaluated at nonzero auxiliary bias. Its unchanged source is compiled in
both default and full-residual profiles to discriminate the compiler behavior. These are actual OpenVAF programs; successful source
compilation and runtime observation are required before using their references.
`protocol.c` is a separate, explicitly labeled ABI transport oracle for previous/
next state, native outcome flags, allocated/literal logger messages and nonfinite
output refusal. It is not a physical model or independent BSIM numeric reference.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [S2 tests](../DESIGN.md) | parent | Public behavioral comparisons | Test ownership | Same input and artifact identities |
| [Oracle builder](../../../Tools/OSDIEvaluationVerification/DESIGN.md) | used by | Pinned compiler/header and direct ABI | Reproducible capture | No production mapper dependency |

## Architecture

```text
real VA source -> explicit pinned OpenVAF -> native library
 -> direct ABI oracle and public evaluator -> all raw F/Q/G/C + topology + parameters
```

## Contracts and Invariants

Linear RC obeys F=g*(Vp-Vn), Q=c*(Vp-Vn), with paired opposite residuals and
matrices [[k,-k],[-k,k]]. Distinct models and parameters must produce discriminating
values. Internal collapse and flow-node ordering are read independently from the
native descriptor and generated setup state. The BSIM artifact itself is external,
explicitly supplied and hash-pinned; no sibling-workspace fallback is committed.
Qualified VA inputs use `--full-residuals`; the default auxiliary build is only a
negative compiler-profile control. Analytic auxiliary derivatives include both
physical-voltage and auxiliary-variable chain terms.

## Verification and Change Impact

The bounded builder records each source, compiler and generated binary hash. Cold
compilation proves actual syntax support. C observations and finite differences
establish the exact native behavior; source equations alone do not establish success.
