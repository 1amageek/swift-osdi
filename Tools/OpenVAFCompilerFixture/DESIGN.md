# OpenVAF Compiler Verification Fixture

## Purpose and Scope

Parent: [swift-osdi](../../DESIGN.md). This verification-only directory captures
the external compiler producer from swift-openvaf commit
`174310120990a492be23407a6e310848c1177290`. It has no children. The original
`Tools/OpenVAFCompilerProfile` at that commit remains the source authority; this
directory is an immutable capture for reproducing swift-osdi qualification inputs.

## Responsibilities and Boundaries

The fixture retains the exact patch, semantic profile, explicit build driver and
program inspector. It does not vendor a compiler executable, upstream checkout,
model source, model binary, or Swift package dependency. Product execution does
not invoke it. The caller explicitly prepares and invokes the external compiler.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [swift-osdi](../../DESIGN.md) | parent | Qualification scope | Verification input capture | No product compiler dependency |
| [CompilerPrecision](../../Sources/COSDI/CompilerPrecision/DESIGN.md) | coordinates with | Versioned program header | Compiler/runtime schema boundary | Exact header digest must match |
| [Original producer](https://github.com/1amageek/swift-openvaf/tree/174310120990a492be23407a6e310848c1177290/Tools/OpenVAFCompilerProfile) | source authority | Captured producer bytes | Original local commit identity | The capture does not assert remote availability |

## Architecture

```text
captured producer + exact upstream source + caller-provided Rust/LLVM
  -> explicit external compiler preparation
    -> newly identified compiler/model artifacts and receipts
      -> independent swift-osdi qualification
```

## Contracts and Invariants

[PROVENANCE.json](PROVENANCE.json) binds original repository, commit, paths,
source hashes and captured hashes. The only producer adaptation is the
`Source.json` header path, resolved inside this checkout. Patch and semantic bytes
remain unchanged. [NOTICE](NOTICE) retains the original attribution license and
identifies the GPL-covered upstream patch and changed-source notice.

The capture establishes source availability and identity only. Existing receipts
are not evidence of executing this relocated profile: its profile digest differs.
A new qualification claim requires new compiler/model identities, receipts and
behavioral verification. Future producer changes require an explicitly identified
new capture and dependent qualification.

## Runtime Flows

The caller supplies an exact upstream checkout, LLVM prefix, fresh output and
process/resource budgets. `build.py` checks source identity and exact patch state,
explicitly applies that patch to the caller-owned checkout, builds externally and
runs its focused compiler regressions. Its `--self-check` exercises process
ownership; it is not a source-only producer check. The retained inspector loads
only a caller-supplied trusted model image after checking its digest.

## Failure, Concurrency, and Constraints

Preparation requires Rust 1.92.0 and LLVM 22.1.8 as checked by the original driver.
Source/patch/header mismatch, failed regressions, timeouts and insufficient free
storage remain failures. No build or qualification success follows from capture
alone. Compiler outputs and caches remain outside the upstream checkout and this
fixture.

## Verification and Change Impact

Capture review compares each file against its exact original Git object and the
recorded hashes, allowing only the declared header path replacement. Changes to
the patch, semantics, header or driver invalidate dependent producer receipts.
Actual producer execution and qualification remain separately reported evidence;
this capture adds no execution result.
