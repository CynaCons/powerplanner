# SRS — Editing

Requirements for creating and editing a chart. These define *what* each edit
does, independent of surface; web implements them via direct manipulation, the
PowerPoint add-in reaches them in stages (see `SRS-powerpoint.md`). Feature tag:
`EDIT`.

Traces up to: `../interaction.md`, `../data-model.md`.

## Edit semantics

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-EDIT-001 | All edits shall operate on the document and then re-run layout; no edit shall mutate device coordinates directly. | One source of truth; every surface stays consistent. | Review | interaction §core operations |
| SRS-EDIT-002 | Date edits shall snap to whole days (the abstract x unit). | Schedules are day-granular; sub-day positions are meaningless. | Review | interaction; layout §coordinates |
| SRS-EDIT-004 | The snap grid shall be one day on the day scale and one week on every coarser scale; it shall never be derived from the view scale. | How far a user is zoomed out is a property of the window, not of the calendar. Quantising to the view scale made a month view snap to 30 days, so on the shipped document every drag under a fortnight committed nothing and the bar sprang back. | Test (`snapDelta`) | utils/snap `snapDelta` |
| SRS-EDIT-005 | A drag preview shall show the snapped result, and the release shall commit exactly what the preview showed. | A bar that follows the pointer to the day and then jumps to a different date on release means the user never let go of the thing they got. | Test (e2e: no jump on release) | useChartInteractions `previewDelta` |
| SRS-EDIT-003 | Resize shall keep a task's `end` on or after its `start` (a resize cannot produce `end < start`). | Upholds the duration invariant during editing. | Demo (web resize) — promote to Test with an editing harness | data-model; SRS-ELEM-002 |

## Operations

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-EDIT-010 | Move shall shift a task's `start` and `end` together by a whole number of days. | Moving preserves duration. | Test | interaction §operations |
| SRS-EDIT-011 | Resize shall change `start` or `end` independently. | Re-scoping changes one boundary, not both. | Test | interaction §operations |
| SRS-EDIT-012 | The system shall create tasks and milestones at a target row and date. | Authoring new work. | Demo | interaction §operations |
| SRS-EDIT-013 | Connect shall create a Dependency between two tasks; the link `type` shall be selectable among the four types. | Express predecessor/successor relations. | Test | data-model (Dependency); SRS-ELEM-041 |
| SRS-EDIT-014 | The system shall create a bracket spanning the selected rows and date range. | Annotate a phase across rows. | Demo | SRS-ELEM-030 |
| SRS-EDIT-015 | The system shall edit element fields: label, color, percent complete, notes, dates. | Full control of element content. | Demo | data-model |
| SRS-EDIT-016 | Delete shall remove the selected element(s) and any dependencies referencing a deleted task. | No dangling references after delete. | Test (delete task removes its deps) | data-model invariants |
| SRS-EDIT-017 | The system shall start a new document, either empty or seeded with the sample programme, and the user shall choose which. | Without it the only way to a chart of one's own is to delete the demo content row by row, and a planner who wants to see a worked example cannot get it back. | Test (e2e) | documentStore `newDocument`; ExportMenu |
| SRS-EDIT-018 | Starting a new document, or opening a file, shall require confirmation while the current document has unsaved changes, naming what would be lost. | Discarding a half-built roadmap to an unguarded menu click is unrecoverable; autosave restores the last session, not the document being replaced. | Test (e2e) | documentStore `isDirty`; ExportMenu |

## Pointer affordance

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-EDIT-030 `[web]` | Each interactive region shall carry the cursor that names the gesture it performs: `move` on a draggable body, `ew-resize` on a date edge, `col-resize` on the panel divider, `text` on an editable name, `grab`/`grabbing` only where the surface itself is panned. | The cursor is the only thing that tells a user what a region will do before they commit to a drag; a hand on a bar that moves a date promises panning and delivers a schedule change. | Test (e2e asserts the computed cursor) | TaskBars, Milestones, ChartArea |
| SRS-EDIT-031 `[web]` | A cursor shall change to its active form while the gesture is in progress, and return when it ends. | Without it there is no feedback that a drag was picked up, only that something moved. | Test (e2e) | useChartInteractions |
| SRS-EDIT-032 `[web]` | No interactive region shall carry the default arrow cursor where a gesture exists, and no non-interactive region shall carry a gesture cursor. | A cursor that lies in either direction costs a user a wasted drag and the undo that follows. | Test (e2e sweeps the surface) | renderer/* |

## Selection & history

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-EDIT-020 | The system shall support selecting one element and extending the selection to multiple. | Bulk operations need multi-select. | Demo | interaction §core operations |
| SRS-EDIT-021 | Edits shall be undoable/redoable through the host's history. | Safe experimentation. | Demo | interaction §undo |

## Open items

- Several requirements verify by Demo today; promote to Test as the web E2E /
  native harness gains hooks for them.
