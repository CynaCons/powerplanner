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

## Selection & history

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-EDIT-020 | The system shall support selecting one element and extending the selection to multiple. | Bulk operations need multi-select. | Demo | interaction §core operations |
| SRS-EDIT-021 | Edits shall be undoable/redoable through the host's history. | Safe experimentation. | Demo | interaction §undo |

## Open items

- Several requirements verify by Demo today; promote to Test as the web E2E /
  native harness gains hooks for them.
