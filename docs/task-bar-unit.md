# Task bar as unit (v2.9.0)

## Problem
Task bars are emitted as multiple PowerPoint primitives (`TASK`, `TASK_PROGRESS`,
`TASK_LABEL`, …). Users must never select “the text” vs “the rectangle” as
separate objects. The bar is one semantic unit whose primary drag changes dates.

## Contract (SRS)
- **SR-TASK-UNIT-01** — All task primitives map to `ownSel TASK` + suppress native child.
- **SR-TASK-UNIT-02** — Click/`TASK_LABEL` hit selects parent task (including right-of-bar labels).
- **SR-TASK-UNIT-03** — Drag from label footprint moves task dates (body drag).

## Implementation
- `IsTaskKind()` in `GanttHitTest.h` lists TASK / TASK_PROGRESS / TASK_LABEL /
  TASK_PCT / RAIL_TASKLBL / RAIL_DOT.
- Snapshot: `TASK_LABEL` → `HtItemKind::TaskLabel` → hit-tests as `TaskBody`.
  Edges/progress stay on the `Task` body rect only.
- Native selection of any IsTaskKind child mirrors to TASK via
  `Overlay_OnNativeSelectionChanged`.
- Live PowerPoint group entry reports the outer root through `ShapeRange` and
  the clicked primitive through `ChildShapeRange`. Both the event sink and Tick
  now route through `Overlay_OnNativeSelectionChangedWithChild`, which makes
  the child the effective pick before mirroring and suppression.

## Gates
```bash
native\build-ops.bat
python native/tools/harness_driver.py scenario trace_task_bar_unit
```
Invariants: `task_label_selects_task_unit`, `task_body_selects_same_unit`,
`task_label_click_selects_task`, `task_progress_selects_task_unit`,
`task_label_drag_moves_dates`, and `task_group_child_selects_task_unit`.

## Not in scope
- Rail label click still selects the **row** (RowBand); only native pick of
  `RAIL_TASKLBL` mirrors to TASK. Graph-side bar unity is the product goal.

## Live recording evidence (2026-07-18)

Session `20260718-162522-519-14476` disproved the harness-only correction. The
user moved `TASK_LABEL/t1` twice while `TASK/t1` and `TASK_PROGRESS/t1` remained
fixed. PowerPoint emitted only `CHART_ROOT` native selections with
`hasChildShapeRange=false`, so child-first resolution never ran. The next fix
must prevent native group entry/child manipulation without depending on
`ChildShapeRange` being observable.

Recorder follow-up closes the measurement blind spot, not the product bug:
while recording, cached live handles refresh `TASK`, `TASK_PROGRESS`, and
`TASK_LABEL` rectangles. `trace_session_recorder` proved an isolated 4 pt
progress-fill move appears in the entity stream. Independent selection and
movement in live PowerPoint still require the v2.9.1 protection repair above.
