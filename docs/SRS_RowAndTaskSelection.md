# SRS - Row and Task Selection & Highlighting (ASPICE-style)

## 1. Feature Overview / Purpose
Provide first-class, reliable selection and highlighting for rows (via left labels and bands) and tasks (via bars in the Gantt area). Selection drives:
- Visual feedback (highlights, bands, frames)
- Contextual app bar and editing actions (including progress)
- Overall component vs item context

User must be able to discover, select, and act on elements without side effects like labels or content disappearing.

## 2. User Goals & Interactions
- Hover left-side row labels (or rail) → row band highlight appears (current hover works).
- Click row label or band → select that row as first-class object (ownSelKind=ROW, stable highlight, app bar updates to row context, labels remain visible).
- Click task bar body or progress area → select the task (ownSelKind=TASK, visual feedback on bar, app bar shows task actions including progress controls).
- Select overall component (CHART_ROOT via grip or direct) → clear item selection, show overall context, no content loss.
- Transition between selections (row → overall, task → row) must not cause disappearance of titles, bars, progress, or graph elements.
- Progress editing must be possible when a task is selected (no visual overlap hiding the control or text).

## 3. Software Requirements (Functional)
- **SR-ROW-01**: Hover on ROW_LABEL shape or row band area must update hover state and paint highlight band without side effects.
- **SR-ROW-02**: Click on ROW_LABEL or RowBand zone must result in `SetOwnSelection("ROW", rowId)`, stable `ownSelKind="ROW"`, rowLabelCount in chrome state remains >0 for the row, no Rebuild that drops labels.
- **SR-ROW-03**: Selecting a row must not cause any task title, row label, or graph element to disappear (verified by rowLabelCount, shape presence, screenshot content stability).
- **SR-TASK-01**: Click on TaskBody (or TASK_PROGRESS) zone must set `ownSelKind="TASK"`, provide visual task highlight, and populate app bar with task-relevant actions (incl. progress +/- or edit).
- **SR-SEL-01**: Selecting the overall CHART_ROOT (natively or via grip) while an item is selected must clear item selection cleanly and must not trigger rebuild or hide of any content shapes.
- **SR-SEL-02**: All selection changes must preserve or correctly update the emitted native shapes (no loss of labels or bars). Use trace points (pre, immed post, +ticks) + DumpChromeStateForTest to verify.
- **SR-PROG-01**: When TASK selected, progress indicator must be visible and editable (no overlap with label text that prevents reading or interacting). Progress must be changeable via app bar or direct gesture and round-trip correctly.
- **SR-VIS-01**: No selection operation shall produce a state where rowLabelCount drops unexpectedly or screenshot size indicates major content loss compared to pre-op.

## 4. Verification Approach (E2E with Native Seams)
- Use harness (ppappbarshot --trace or dedicated) + harness_driver.
- Scenarios: row-label-hover-click, row-then-overall, task-body-click, progress-edit.
- At trace points capture: DumpChromeStateForTest (ownSel, rowLabelCount, rowBands, chartRect, appBarGroups), stable region screenshots.
- Invariants:
  - rowLabelCount stable or predictable across selection changes.
  - ownSelKind transitions cleanly (ROW → empty for overall, TASK selectable).
  - No large unexpected drops in visual artifact size.
  - Progress actions available in correct context.
- Run via `python native/tools/harness_driver.py scenario ...` or trace.
- Failures (disappearance) must be caught by the mechanism and drive fixes.

## 5. Non-Functional / Constraints
- Must work with the existing layout + reconcile pipeline (no parallel renderer).
- Selection model (ownSel) must remain the source of truth for chrome.
- 150ms tick acceptable for updates, but no permanent loss.
- Compatible with existing app bar, hover, drag, rebuild mitigations (ScreenUpdating, LockWindowUpdate where used).

## 6. Open / Related
- Progress overlap fix may require small layout tweak in GanttScene / builder for task label vs progress bar.
- Task selection must also enable progress in app bar model for TASK.
- Full coverage in onslide-v4-plan acceptance criteria.