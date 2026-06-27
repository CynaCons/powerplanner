# SRS — Chart Elements

Requirements for the visual vocabulary of a chart: tasks, milestones, summary
rows, brackets, dependencies, and markers. Feature tag: `ELEM`.

Traces up to: `../data-model.md`, `../layout.md`, `../visual-vocabulary.md`.

## Tasks

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-ELEM-001 | The system shall render each task as a horizontal bar positioned at its `start` date and spanning to its `end` date **inclusive** (a task with `start == end` occupies one whole day). | A Gantt bar's length is its duration; inclusive end matches user intent. | Test: `spec/fixtures/basic-chart` (t_d1 widthDays=8) | layout §4; `src/renderer/TaskBars.tsx`; `native/.../GanttLayout` |
| SRS-ELEM-002 | A task shall satisfy the invariant `end >= start` (duration ≥ 1 day inclusive); a task violating it is malformed. | Negative duration is meaningless. | Review (invariant) — **not yet enforced at load; see open items** | data-model §Task; SRS-EDIT-003 |
| SRS-ELEM-003 | When two tasks in the same row overlap in time, the system shall place them on separate stacking sub-rows so neither is occluded. | Overlapping work must remain readable. | Test: `basic-chart` (t_d1 subRow 0, t_d2 subRow 1) | layout §2; SRS-LAY-020 |
| SRS-ELEM-004 | The system shall fill a task bar with its `color` when set, else an implementation default accent. | Color encodes category; sensible default when unset. | Review | visual-vocabulary; data-model |
| SRS-ELEM-005 | The system shall indicate `percentComplete` (0–100) as a darker inset filling that fraction of the bar from its start. | Communicates progress at a glance. | Demo / Review | visual-vocabulary |
| SRS-ELEM-006 | The system shall place a task's label per `labelPlacement` (on-bar, left, right, above, below, hidden), defaulting to on-bar with fallback to right then left when it does not fit on the bar; `hidden` suppresses the label. | Labels must stay readable on narrow bars; all enum values must be honored. | Demo | data-model (LabelPlacement); visual-vocabulary |
| SRS-ELEM-007 | The system shall render every task with a minimum bar width so a zero- or near-zero-span (or malformed) task stays visible and never crashes the renderer. | Robustness: a degenerate span must not vanish or break layout. | Analysis (engine `width = max(2, …)`) | layout §4; `src/layout/engine.ts` |

## Milestones

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-ELEM-010 | The system shall render each milestone as a diamond centered on its `date` within its row. | Milestones are zero-duration events. | Test: `basic-chart` (m_ship xDay=33, rowIndex 3) | layout §5; visual-vocabulary |
| SRS-ELEM-011 | The system shall size a milestone diamond proportional to the bar thickness so it reads as a peer element. | Visual consistency across implementations. | Review | visual-vocabulary |

## Summary (group) rows

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-ELEM-020 | For any **visible** row that is a parent of other rows and has descendant tasks, the system shall derive a summary bar spanning the min `start` to max `end` of those tasks. | A summary rolls up its children's span. | Test: `basic-chart` (r_phase xDay=4, widthDays=24) | layout §6 |
| SRS-ELEM-021 | The system shall hide a collapsed group's child rows and still show the parent's summary bar. | Collapse declutters without losing the rollup. | Test (collapsed fixture, F2) | layout §1, §6 |

## Brackets

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-ELEM-030 | The system shall render a bracket as a span from its `start` to `end` covering vertically from the topmost to the bottommost of its `rowIds`. | Brackets annotate a phase across rows. | Test: `basic-chart` (br1 topRow 1, bottomRow 2) | layout §7 |

## Dependencies

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-ELEM-040 | The system shall connect dependent tasks with an orthogonal (3-segment elbow) connector, with an arrowhead at the successor. | Standard Gantt dependency notation. | Test: `basic-chart` (dep1 fromXDay=12, toXDay=14) | layout §8; visual-vocabulary |
| SRS-ELEM-041 | The connector endpoints shall be selected by dependency `type`: finish-to-start, start-to-start, finish-to-finish, start-to-finish. | The four link types attach to different bar ends. | Test (all-types fixture, F2) | layout §8 |

## Markers

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-ELEM-050 | The system shall render a marker as a full-height vertical line at its `date` with its label, styled by `type` (today / deadline / note). | Markers call out dates spanning all rows. | Demo / Review | data-model; visual-vocabulary |

## Open items

- **SRS-ELEM-002 is not yet enforced:** neither `schema.ts` nor the JSON Schema
  rejects `end < start`, and the engine only clamps width (SRS-ELEM-007). Enforce
  the invariant when the JSON Schema becomes the validation source (Phase F2).
- All-dependency-types and collapsed-group fixtures are added in **F2**
  (conformance breadth); SRS-ELEM-021/041 cite them as future tests.
- Percent-complete and label-placement currently verify by Demo/Review; promote
  to Test when a render-level fixture exists.
