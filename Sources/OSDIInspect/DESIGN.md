# OSDIInspect

## Purpose and Scope

Parent: [package](../../DESIGN.md). No children. Own a host-only inspection CLI for
S1's public artifact/library/storage path; it is not a circuit simulator.

## Responsibilities and Boundaries

Decode a versioned request, compose an existing local artifact source, call Runtime,
close owners and atomically write a structured report. Never start a compiler,
replace a digest, initialize model physics or select a circuit solver.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [Runtime](../OSDIRuntime/DESIGN.md) | depends on | load/storage/close | Actual public path | Metadata/storage success is not model evaluation |
| [Foundation](../../../CircuiteFoundation/DESIGN.md) | depends on | Local exact artifact source | Bounded acquisition | Caller supplies expected digest/size and budgets |

## Architecture

```text
JSON request(schema1) -> exact local source -> load -> optional storage check
  -> explicit library/source close -> atomic JSON report
failure -> structured report + nonzero exit
```

## Contracts and Invariants

All budgets and the expected identity come from the request. Reports bind digest,
size and descriptor stride. verifyStorage checks zeroing through public borrowed
views; it does not execute model equations. No result pointer is serialized.

## Failure, Concurrency, and Constraints

The CLI owns one run. Source and library cleanup failures retain failure status.
Invalid request/output failure exits nonzero; no fallback digest/model is selected.
External invocation tests supply a process timeout; native constructors cannot be
forcibly interrupted in process.

## Verification and Change Impact

Execute valid real OpenVAF artifacts and malformed/tampered images through this
executable, parse both reports and exit codes, and verify no compiler launch is
required. Runtime tests own detailed lifetime/concurrency behavior.
