# OSDI CLI

## Purpose and Scope

Parent: [package](../../DESIGN.md). Children: [Inspection](Inspection/DESIGN.md),
[PointEvaluation](PointEvaluation/DESIGN.md), and [PrecisionEvaluation](PrecisionEvaluation/DESIGN.md).
Own the single host executable `osdi` and its subcommand composition.

## Responsibilities and Boundaries

Dispatch `inspect`, `evaluate` and `precision`, report invalid command selection as
structured failure and exit nonzero on command failure. Each child owns its
request/report schema and invokes public Runtime contracts. Model physics,
compilation, circuit solving and accepted simulation history remain external.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [Runtime](../OSDIRuntime/DESIGN.md) | depends on | Artifact admission, preparation, evaluation, close | Native execution owner | Preserve cleanup failures |
| [Contracts](../OSDIContracts/DESIGN.md) | depends on | Values, budgets and diagnostics | Caller inputs | No default identity or unknowns |
| [Inspection](Inspection/DESIGN.md) | child | Schema 1 request/report | Metadata and zeroed storage | No model evaluation |
| [PointEvaluation](PointEvaluation/DESIGN.md) | child | Point schemas 1/2 | Preparation and raw point results | No circuit solution |
| [PrecisionEvaluation](PrecisionEvaluation/DESIGN.md) | child | Precision schemas 1/2 | Explicit isolated precision requests | Exact worker/profile required |

## Architecture

```text
osdi <subcommand> --request INPUT --output REPORT
 -> command-owned decoding -> public Runtime -> typed result or failure
 -> owned JSON report -> success status -> composition-owned process exit
```

## Contracts and Invariants

Subcommands preserve existing JSON schemas and exact request-byte identities.
Only the executable composition root exits the process. Child commands return a
success flag after report handling and explicit cleanup. Root and command help
must succeed; absent/unknown commands and invalid options fail. Ordinary command
products have no GMP/MPFR link dependency and never install a compiler.

## Failure, Concurrency, and Constraints

One command owns one process invocation. Model calls retain the Runtime's native
trusted-code constraints. External callers supply process deadlines. Invalid
command selection emits JSON on stderr with reason `invalid_command`.

## Verification and Change Impact

Tracked CLI verification executes each command with real native fixtures,
persists and reloads success/refusal reports, checks exit status and exact input
binding, exercises real precision IPC and confirms output storage cleanup. The
public consumer verifies the same point API and CLI output. The root package's
next-major migration contract owns removal of the old executable names.
