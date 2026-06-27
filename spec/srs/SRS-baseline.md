# SRS — Baseline

Requirements for capturing a baseline schedule and visualizing drift against the
current schedule. Feature tag: `BASE`.

Traces up to: `../data-model.md` (BaselineSnapshot).

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-BASE-001 | The system shall capture a baseline snapshot of the current task spans (`id`, `start`, `end`) with a capture timestamp. | A baseline is a frozen reference of the plan at a point in time. | Test (capture populates baseline) | data-model (BaselineSnapshot) |
| SRS-BASE-002 | The baseline shall be stored on the document and persist through save/load round-trips. | Drift comparison must survive reopening the file. | Test | SRS-PERS-013; data-model |
| SRS-BASE-003 | When enabled, the system shall visualize each task's drift by rendering its baseline span as a secondary rail beneath the current bar. | Shows slippage/advance at a glance against the plan. | Demo / Review | visual-vocabulary |
| SRS-BASE-004 | Drift visualization shall not alter the live layout of current tasks. | The baseline is an overlay, not a layout input. | Analysis | layout (baseline excluded from positioning) |
| SRS-BASE-005 | The system shall allow clearing the baseline. | Re-baselining after re-planning. | Demo | data-model |

## Open items

- A baseline-drift fixture (document with a baseline) is part of F2; SRS-BASE-002/003
  cite it.
