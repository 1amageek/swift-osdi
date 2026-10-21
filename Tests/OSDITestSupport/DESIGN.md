# OSDI Test Support

## Purpose and Scope

Parent: [package](../../DESIGN.md). Child: [BuildCache](BuildCache/DESIGN.md).
Own shared host-only native fixture preparation used by both existing test targets.
This target has no public product and is not a dependency of production targets.

## Responsibilities and Boundaries

Expose typed immutable build requests/results and a bounded content-keyed artifact
cache. Existing test targets own fixture variants, test expectations and run-local
lifetime observation. Clang owns C compilation. Optional model/reference
qualification remains explicitly selected and may retain independent Python tools.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [BuildCache](BuildCache/DESIGN.md) | child | Exact request to validated artifact | Sole cache/publication owner | Cached artifacts contain no mutable run state |
| [Runtime support](../OSDIRuntimeTests/FixtureSupport/DESIGN.md) | used by | Metadata fixture compilation | Run-local lifetime owner | Marker-sensitive tests remain serialized |
| [Evaluation support](../OSDIEvaluationTests/FixtureSupport/DESIGN.md) | used by | Protocol/transport compilation | Ordinary point-test inputs | Optional oracle captures retain their own evidence |
| [Process support](https://github.com/1amageek/SignoffToolSupport) | depends on | TimedProcessRunner and typed cleanup failures | Fixed public stable version 26.1018.0 in Package.swift | No sibling override or process controller duplication |

## Architecture

```text
swift test -> existing fixture actors -> BuildCache -> bounded Clang process
 -> complete artifact/receipt publication -> test-owned materialization -> Runtime
```

## Contracts and Invariants

Normal test preparation requires no Python command or manual preparation step.
SwiftPM owns Swift/module incremental builds. BuildCache owns only external native
fixture artifacts. Every test execution runs its assertions against fresh native
owners; a cached test result never replaces execution.

## Verification and Change Impact

Runtime tests own real hit/miss, invalidation, corruption, concurrent build,
process failure/cancellation and lifecycle regressions. Task integration records
Python-denied cold/warm swift test runs and actual compiler invocation counts.
