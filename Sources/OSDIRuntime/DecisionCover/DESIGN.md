# Complete Decision Cover Runtime

## Purpose and Scope

Parent: [OSDIRuntime](../DESIGN.md). No children. Own protocol-first complete-cover preparation/evaluation and scoped full-tree/all-leaf inspection. Reuse [PrecisionExecution](../PrecisionExecution/DESIGN.md)'s sole noncopyable session/process/cleanup owner.

## Responsibilities and Boundaries

Expose only independently checked complete covers for the original exact coordinate/time box and immutable source/setup/descriptor/topology binding. No public constructor creates a complete result, and no partial frontier result is successful. Root existence, chart nonemptiness, charge continuity/reset, model calibration, flow and history remain upper responsibilities.

## Related Designs

| Design | Relationship | Contract Used | Summary | Cautions |
|---|---|---|---|---|
| [OSDIRuntime](../DESIGN.md) | parent | Host module composition | Native execution | No WASM/Embedded process claim |
| [PrecisionExecution](../PrecisionExecution/DESIGN.md) | coordinates with | Exclusive session, staging, Mutex lifecycle, cleanup | Same owner with immutable cover operation | No second process or lock owner |
| [Cover Values](../../OSDIContracts/DecisionCover/DESIGN.md) | depends on | Limits and portable records | Pointer-free inspection | Values alone grant no certificate |
| [Cover Process](../../COSDI/PrecisionProcess/DecisionCover/DESIGN.md) | depends on | Checked protocol-4 tree/leaf borrow | Complete native response | Failure never calls the body |

## Architecture

```text
prepareDecisionCover(exact program/worker/profile/options/limits)
 -> existing exclusive session (.decisionCover)
 -> constructDecisionCover(original box/time)
 -> native independent checking + parent wire checking
 -> scoped binding + box + node collection + withLeaf(index) planes
```

## Contracts and Invariants

Preparation uses the same immutable source/setup and full raw topology captured by OSDIPrecisionChartBinding, not supplied coefficients. Result retains that binding, exact original four coordinate planes and both time contexts. Every terminal leaf exposes all eight finite ordered source F/Q/G/C formula planes. G/C keep source AD-root meaning; no seam derivative is asserted. No aggregate maximum-timestep fact is inferred from the last replayed leaf.

Node indexing copies one fixed bounded record into portable inline storage; no tree/guard collection is automatically materialized. Leaf access borrows spans directly from one fixed result pool. Every view is nonescapable and expires at the next attempt/close; the exclusive synchronous session borrow prevents either operation during a client callback.

Wrong operation/dimensions/mode/limits fail before submission. Submitted numerical/capacity/step/cell/cover/protocol/timeout/cancellation failures publish no view and close the same child; cleanup errors retain the complete primary cover error and retained cleanup cause. Preparation/close use the existing exact artifact leases and retryable retained cleanup. No fallback to chart/range/point exists.

## State, Ownership, and Lifecycle

The existing PrecisionExecution Mutex protects the same handle on all supported native targets. Evaluation, process waiting, callbacks and cleanup remain outside the lock. There is no conditional synchronization or Sendable change. No view or unsafe pointer crosses await. The existing staged worker owner is retained until exact-child cleanup completes.

## Verification and Change Impact

Public Swift tests exercise complete irrational equality faces, all leaves and endpoint bits, exact binding/box/time, borrowed-address reuse, wrong-operation, bounded refusal, close, cancellation and source lease independence through the actual worker. C wire falsifiers own malformed/partial responses. Source replay/checker qualification remains the native component's authority. Point/range/chart compatibility must run after this scope converges.
