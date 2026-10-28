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
| SRS-EDIT-033 `[web]` | Every pointer gesture shall convert client coordinates to chart coordinates through one shared geometry source read at the moment of the event — row-gutter width, header height, vertical scroll, plot width and the current layout — so that the press, the move, the hit test and the drawn overlay cannot disagree; a marquee sweep shall select exactly the elements the drawn rectangle covers. | Four hand-rolled copies of the same subtraction drifted apart: the lasso's moving edge landed 80px from the pointer, because the move handler was pinned by an empty dependency array to the first render's 120px gutter against the 200px the chart draws; no gesture read `scrollY`, so a scrolled sweep was drawn and tested hundreds of pixels above the pointer; and the hit test ran against a layout captured before the initial fit, so after a zoom a sweep took a bar it did not cover and missed one it did. | Test: `tests/e2e/marquee-geometry.spec.ts`, `tests/unit/geometry.test.ts` | `layout/geometry.ts` `clientToChart`/`chartToClient`; `useChartInteractions`; `ChartArea` `geomRef` |

## Keyboard

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-EDIT-040 `[web]` | Every keyboard binding shall be declared once in a single keymap table as a full chord stating each of Ctrl/Cmd, Alt and Shift as required or forbidden, and a key press shall run only the binding whose chord it matches exactly; an unmatched chord shall run nothing and suppress nothing. | Bindings matched by an if-chain of bare letters let an earlier branch claim a chord meant for a later one and return before it. The bare-`m` insert shadowed the scale block, so the documented `⇧M → Month` inserted a milestone on the first row at today's date and the month scale was unreachable from the keyboard; `Ctrl+N` inserted a task and suppressed the user's "new"; `altKey` was read nowhere in the app at all. | Test: `tests/unit/keymapBehaviour.test.ts`, `tests/unit/keymap.test.ts` | `app/keymap.ts`; `useKeyboardShortcuts`; `App` |
| SRS-EDIT-041 `[web]` | Every surface that teaches a binding — the shortcuts sheet, the command palette, menu labels and tooltips — shall render its key and its label from the keymap table, with the primary modifier drawn by one platform-correct helper. | Three hand-maintained copies of the same facts diverged with nothing to catch it: the sheet taught a chord that could not fire and omitted the three bare letters that mutate the document, the palette named `T` for a command bound to `N`, and ⌘ appeared on four surfaces with "Ctrl" on a tooltip an inch away. | Test: `tests/unit/keymap.test.ts` (`shortcutFor` throws on an unknown id) | `ShortcutsOverlay`; `CommandPalette`; `ExportMenu`; `Header`; `ChartArea` |
| SRS-EDIT-042 `[web]` | While a dialog, a teaching sheet or the command palette is open, no binding that acts on the chart or the document shall run. | Open state lived in three separate component `useState` flags, so the one check that asks whether a modal owns the keyboard could not see them: pressing `M` while reading the shortcuts sheet inserted a milestone behind the scrim, and `Del` deleted the selection underneath it. | Test: `tests/unit/keymapBehaviour.test.ts` | `stores/viewStore` `anyModalOpen`; `useKeyboardShortcuts` |

## Selection & history

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-EDIT-020 | The system shall support selecting one element and extending the selection to multiple. | Bulk operations need multi-select. | Demo | interaction §core operations |
| SRS-EDIT-021 | Edits shall be undoable/redoable through the host's history. | Safe experimentation. | Demo | interaction §undo |

## Open items

- Several requirements verify by Demo today; promote to Test as the web E2E /
  native harness gains hooks for them.
