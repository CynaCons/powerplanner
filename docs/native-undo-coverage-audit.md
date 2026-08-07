# Native undo coverage audit (Phase 13 v2.8.3)

One undo entry per user gesture is required by SR-SMO-08 and several WIN/DEP rules.

| Operation class | E2E coverage | Notes |
|-----------------|--------------|-------|
| Window set/clear | automated | `trace_window_undo` — one undo restores window + PP_PROJ; next edit does not resurrect |
| Task nudge / color (app bar) | partial | Latency traces commit ops; undo not asserted every run — Demo OK |
| Task drag date | partial | `trace_drag_date_pill` / walkthrough; undo Demo |
| Progress drag | partial | `trace_progress_drag`; undo Demo |
| Row add / multi delete | partial | traces exist; multi-delete claims one undo entry in code (U4) — Demo recommended |
| Link create / dep delete | partial | `trace_link_drag_port`; undo Demo |
| Inline rename / card edit | partial | rename + card-commit traces; undo Demo |
| Insert Gantt / blank chart | demo | Office undo stack after insert |
| Repair layout / import | demo | |

**Gaps (honest):** no single harness matrix that asserts `Undo` after every gesture.
**Cheap extension later:** add `trace_task_nudge_undo` mirroring window-undo pattern.

**Status 2026-07-17:** audit documented; window path automated; remaining Demo/partial.
