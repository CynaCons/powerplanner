# SRS — Critical Path

Requirements for Critical Path Method (CPM) computation and highlighting.
Feature tag: `CPM`.

Traces up to: `../data-model.md` (Dependency). Reference impl:
`src/layout/criticalPath.ts`.

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-CPM-001 | The system shall compute the critical path via a forward pass (earliest start/finish) and backward pass (latest start/finish) over the task/dependency graph. | CPM is the standard schedule-criticality method. | Test: `tests/unit/criticalPath` | criticalPath.ts |
| SRS-CPM-002 | The computation shall honor all four dependency types (finish-to-start, start-to-start, finish-to-finish, start-to-finish) when deriving constraints. | Each link type imposes a different timing constraint. | Test | criticalPath.ts; SRS-ELEM-041 |
| SRS-CPM-003 | A task shall be marked critical when its total float (slack) is ≤ 0 (zero or, in over-constrained graphs, negative). | Zero/negative-float tasks determine the project end date. | Test | criticalPath.ts |
| SRS-CPM-004 | The system shall scope the computation per connected component (independent task clusters are analysed separately). | Unrelated sub-plans have independent critical paths. | Test | criticalPath.ts |
| SRS-CPM-005 | The system shall detect dependency cycles and fail safe (no infinite loop; the chart still renders). | A cyclic graph must not hang or crash. | Test (cycle fixture) | criticalPath.ts |
| SRS-CPM-006 | The computation shall run in O(V+E) over tasks (V) and dependencies (E). | Must stay responsive on large plans. | Analysis | criticalPath.ts |
| SRS-CPM-007 | When enabled, the system shall visually highlight critical tasks distinctly (e.g. a red emphasis) without altering layout. | Highlighting is a view concern, not a layout change. | Demo / Review | visual-vocabulary; useViewStore |
| SRS-CPM-008 | The computation shall expose the project duration (longest path length, in days) and per-task float. | These are product-visible outputs (statistics, drift analysis), not just internal state. | Test | criticalPath.ts (`projectDurationDays`, `taskFloat`) |

## Open items

- Cycle and multi-component fixtures referenced by SRS-CPM-004/005 are part of F2.
