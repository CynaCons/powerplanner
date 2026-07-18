# SRS - Native Row and Task Selection & Highlighting

Native-specific requirements for selecting and highlighting rows through labels,
bands, and rails, and tasks through bars and progress areas in the PowerPoint
on-slide Gantt editor. Selection drives visual feedback, contextual app bar and
editing actions, progress editing, and transitions between item and whole-chart
contexts. This file migrates the legacy prose row/task selection requirements
for v2.6.8.

Traces up to: `../srs/SRS-editing.md`, `../interaction.md`,
`docs/onslide-experience-spec.md`,
`spec/srs-native/SRS-SelectionChromeVisuals.md`,
`spec/srs-native/SRS-OverlayLifecycle.md`,
`spec/srs-native/SRS-InteractionConventions.md`, and
`native/tools/harness_driver.py`.

Reference impl: `native/PowerPlannerAddin/` (Overlay.cpp, GanttHitTest.*,
GanttAppBar.h, GanttBuilder.cpp, GanttScene*).

## Row Selection

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-ROW-01 | Hovering a ROW_LABEL shape, row band, or row rail shall update row hover state and paint the row highlight band without mutating selection or emitted chart content. | Row selection must be discoverable while hover remains a preview-only interaction. | Test/Demo with `row-label-hover-click` trace via `python native/tools/harness_driver.py trace ... --check-invariants`; inspect DumpChromeStateForTest hover state, rowBands, rowLabelCount, and screenshots. | Legacy SR-ROW-01; GanttHitTest row label/band zones; Overlay hover paint |
| SR-ROW-02 | Clicking a ROW_LABEL shape or RowBand zone shall establish a stable row own-selection (`ownSelKind="ROW"`, `ownSelId=rowId`) across the immediate and post-tick trace points. | Rows must be first-class selectable objects, not only passive labels. | Test with `row-label-hover-click`; assert `SetOwnSelection("ROW", rowId)` effect, `ownSelKind`, and app bar row context at pre/immed/+1/+3 trace points. | Legacy SR-ROW-02; Overlay ApplyClickSelection/SetOwnSelection; DumpChromeStateForTest |
| SR-ROW-03 | Selecting a row shall preserve visible task titles, row labels, and graph elements. | Selection must not produce content disappearance or label loss. | Test/Review with row selection traces; assert rowLabelCount remains stable or predictable, shape presence remains valid, and screenshots show content stability. | Legacy SR-ROW-03 plus label-drop clause from SR-ROW-02; GanttBuilder/Rebuild; rowLabelCount seam |

## Task Selection

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-TASK-01 | Clicking a TaskBody or TASK_PROGRESS zone shall establish a stable task own-selection (`ownSelKind="TASK"`, `ownSelId=taskId`). | Task bars and progress fills are the primary editable Gantt objects. | Test with `task-body-click`; assert ownSel transition and stability at pre/immed/+1/+3 trace points. | Legacy SR-TASK-01; GanttHitTest TaskBody/TASK_PROGRESS; Overlay ApplyClickSelection |
| SR-TASK-UNIT-01 | A task bar shall be a single non-decomposable user object. Emitting multiple PowerPoint primitives (TASK fill, TASK_PROGRESS, TASK_LABEL, TASK_PCT, RAIL_DOT, RAIL_TASKLBL) shall not create separate user-selectable objects; any native or overlay pick of those primitives shall resolve to the same `ownSelKind="TASK"` / `ownSelId=taskId`, and PowerPoint child selection shall be suppressed (SR-SHP-01/02). When PowerPoint reports outer `ShapeRange=CHART_ROOT` plus a populated `ChildShapeRange`, the child shall be treated as the effective pick. | Users must never select "the text" vs "the rectangle" as independent shapes; the bar is one unit. | Harness `trace_task_bar_unit` invariants `task_label_selects_task_unit`, `task_group_child_selects_task_unit`, `task_body_selects_same_unit`, `task_progress_selects_task_unit`; pure `IsTaskKind` + hit-test ops cases. | v2.9.0/v2.9.1; IsTaskKind; Overlay_OnNativeSelectionChangedWithChild; GanttHitTest TaskLabel |
| SR-TASK-UNIT-02 | Clicking or hit-testing a TASK_LABEL (on-bar or right-of-bar placement) shall select the parent task and shall not clear selection to EmptyCell/background. | Labels that sit outside a narrow bar still belong to that task. | `task_label_click_selects_task`; ops hit-test TaskLabel → TaskBody. | v2.9.0; HtItemKind::TaskLabel; Overlay snapshot build |
| SR-TASK-UNIT-03 | Dragging from the task label footprint shall move/resize the task's dates using the same body/edge drag model as dragging the bar fill (primary verb: change dates). | The unit's primary continuous gesture must work wherever the user grabs the bar visual. | `task_label_drag_moves_dates` asserts pre/post ownSelTaskStart/End change while ownSel stays TASK. | v2.9.0; StartDragGesture TaskBody; SR-IXC-02 |
| SR-RTS-01 | A selected task shall render visible task selection feedback on the task bar without obscuring task content. | Users need clear selected-state feedback that does not fight chart readability. | Test/Review with `task-body-click` screenshots and selection chrome visual matrix. | Legacy SR-TASK-01; SRS-SelectionChromeVisuals; Overlay task highlight paint |
| SR-RTS-02 | The task-selected app bar shall expose task-relevant actions, including a progress editing route. | Selection must unlock the commands users expect for the chosen task. | Test with `task-body-click` and `progress-edit`; assert `appBarGroups` contains the task context and progress action/edit route. | Legacy SR-TASK-01; legacy open note "Task selection must also enable progress"; GanttAppBar model |

## Progress Editing

The legacy progress-editing requirements are folded into this section.
Percent stepper controls are superseded by `SR-IXC-01` and `SR-IXC-06`:
progress is a continuous scalar edited primarily by dragging the progress
handle with a live percentage readout (UF-12).

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-PROG-01 | Hit-testing a task body or task progress area shall select the parent task and provide task-context progress editing affordances. | Users must be able to enter progress editing from the visual progress area as well as the bar body. | Test with `task-select-progress` or `progress-edit`; assert ownSelKind=TASK and progress action/affordance availability. | Legacy SR-PROG-01; SR-TASK-01; GanttHitTest TASK/TASK_PROGRESS |
| SR-PROG-02 | The progress indicator shall remain visible, distinguishable, and not completely overlapped by task label text or selection chrome. | Progress is a central task attribute and must not be hidden by labels or overlays. | Test/Review with `progress-edit`; inspect screenshots for label/control overlap and pixel readability. | Legacy SR-PROG-02; SRS-SelectionChromeVisuals SR-VIZ-01; GanttScene/GanttBuilder |
| SR-PROG-03 | Changing task progress through a supported path shall update the model, re-emit or reconcile shapes as needed, and preserve task selection and progress visuals. | Progress edits must persist through rebuilds without losing context or visual feedback. | Test with `progress-edit` or `progress-drag`; assert document percent changes, survives rebuild/round-trip, and ownSel remains stable. | Legacy SR-PROG-03; SRS-InteractionConventions SR-IXC-01/SR-IXC-06; Overlay progress commit |
| SR-PROG-04 | Native e2e coverage shall verify that after task selection progress is actionable and related chart elements do not disappear. | State-only selection checks do not prove progress editability or visual stability. | E2E trace `task-select-progress` or `progress-edit`; assert stable rowLabelCount, selected task context, progress affordance/action, and screenshot visibility. | Legacy SR-PROG-04; harness trace invariants |

## Selection Transitions

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-SEL-01 | Selecting the overall CHART_ROOT natively, by grip, or by direct component selection shall enter the overall-component context by clearing item own-selection. | Whole-chart operations need a clean context switch from row/task selection. | Test with `row-then-overall` and task-to-overall traces; assert ownSelKind becomes empty/overall and app bar shows overall context. | Legacy SR-SEL-01; CHART_ROOT selection; Overlay grip/direct selection handling |
| SR-SEL-02 | Selection changes shall preserve or correctly update emitted native shapes without losing labels, task bars, progress indicators, or graph elements. | Transitions between row, task, and overall contexts must not cause disappearing content. | Test with pre/immed/+1/+3 trace points and DumpChromeStateForTest; assert shape presence, rowLabelCount, task bar presence, and screenshot stability. | Legacy SR-SEL-02; user goals row->overall and task->row; GanttBuilder/Rebuild; Overlay Tick |
| SR-VIS-01 | No selection operation shall produce a state where rowLabelCount drops unexpectedly or screenshot dimensions/content-region size indicate major content loss compared with the pre-operation capture. | Broad visual invariants catch regressions that state-only checks miss. | Test with trace invariants and stable region screenshots; fail runs on unexpected rowLabelCount or visual artifact-size drops. | Legacy SR-VIS-01; harness_driver invariants; DumpChromeStateForTest |

## Cross-Cutting Constraints

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-RTS-04 | The add-in own-selection model (`ownSel`) shall remain the source of truth for selection chrome, app bar context, and editing commands. | Native PowerPoint shape selection is too coarse for internal row/task semantics. | Review code paths that build chrome/app bar from ownSel; test row/task/overall transitions with DumpChromeStateForTest. | Legacy non-functional constraint; Overlay selection model; GanttAppBar model |
| SR-RTS-05 | Row and task selection behavior shall use the existing layout and reconcile pipeline without introducing a parallel renderer. | Selection must stay consistent with the chart data model and emitted PowerPoint shapes. | Review implementation changes for reuse of existing layout/reconcile paths; run selection traces after mutations. | Legacy non-functional constraint; layout/reconcile; GanttBuilder |
| SR-RTS-06 | Selection, hover, and app bar state updates shall settle within the normal tick cadence (150 ms acceptable). | Feedback should feel immediate while staying compatible with the existing polling model. | Test trace pre/immed/+1/+3 captures; verify stable expected state by the next tick and no permanent content loss. | Legacy non-functional constraint; Overlay Tick; harness trace timing |
| SR-RTS-07 | Selection behavior shall remain compatible with existing app bar, hover, drag, and rebuild mitigations including ScreenUpdating and LockWindowUpdate where those mitigations are used. | Selection must coexist with the native add-in's current stability mechanisms. | Review code paths around app bar, hover, drag, and rebuild mitigations; run row/task/progress traces with invariants. | Legacy non-functional constraint; Overlay.cpp; rebuild mitigation paths |

## Open / Related

- Progress overlap fixes may require small layout adjustments in GanttScene or
  GanttBuilder for task labels versus progress fills.
- Full coverage is expected through the native v2.6.x harness scenarios:
  `row-label-hover-click`, `row-then-overall`, `task-body-click`, and
  `progress-edit`.
