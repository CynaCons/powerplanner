# SRS - On-Slide Creation & Editing Flows (ASPICE-style)

## 1. Feature Overview / Purpose
Creating content (tasks, milestones, rows, notes) is the first thing a new
user does; today every creation route exists but several are hidden knowledge
or fail silently. This SRS makes the creation chain discoverable, reliable on
ALL chart states (including empty), and consistent with
docs/onslide-experience-spec.md + the mockup.

Known defects this SRS addresses (inventory 2026-07-11):
- Drag-to-create and right-click "Add task/milestone/note here" fail
  SILENTLY on a chart with no tasks (px/day derived from existing task
  shapes; `ComputeDragPxPerDay` returns 0).
- Hover "+" chip adds a ROW where spec B2.5 promises a TASK.
- Drag-to-create has no visible affordance (cross cursor only).
- Double-click editors and keyboard shortcuts are unadvertised.

## 2. User Goals & Interactions
- From an empty chart, the user can create their first task within seconds
  using any visible route (app bar INSERT, right-click, drag) — none of them
  may silently no-op.
- Hovering a row communicates the primary quick action (add a task here).
- Hovering an empty timeline cell communicates "drag to create a task".
- Right-click anywhere gives a complete, context-correct menu.
- Every mutation gives immediate visual confirmation (new element selected,
  chrome follows within one tick).

## 3. Software Requirements (Functional)

### Reliability on all chart states
- **SR-CRE-01**: Day↔pixel projection for creation gestures shall be derived
  from the chart's own projection (`PP_PROJ` {minDay, pad, ptPerDay, originX}
  + axis geometry), NOT from existing task shapes. All creation routes shall
  work on a chart containing zero tasks.
- **SR-CRE-02**: No creation action may silently no-op. If a creation route
  cannot execute, it must be disabled (40% alpha / grayed menu item) or give
  visible feedback (hint pill), never nothing.
- **SR-CRE-03**: After any successful creation, the new element becomes the
  own-selection (ownSelKind/Id set synchronously post-rebuild) and its
  context chrome (app bar group, selection frame) is visible on the next
  paint; row labels and all prior content remain visible (no flash — see
  SRS_OverlayLifecycle SR-LIFE-13).

### Discoverability
- **SR-CRE-04** (spec B2.5): The per-row hover "+" quick-add shall add a
  TASK in that row (visible-range center, default span), per spec. Row
  insertion keeps its own affordances (app bar INSERT ▸ Row, row context
  Above/Below, right-click).
- **SR-CRE-05**: Hovering an empty timeline cell for ≥ ~600 ms shall show a
  transient hint (tooltip pill near cursor, same visual family as the link
  hint pill): "Drag to create a task — right-click for more". Shown at most
  once per selection/hover session; never during drags/menus/editors.
- **SR-CRE-06**: Double-click on an empty timeline cell shall create a task
  at that row/date with the default span (equivalent to "Add task here").
- **SR-CRE-07**: Milestone creation shall be available from: app bar INSERT ▸
  Milestone, right-click empty cell ▸ "Add milestone here" (works with zero
  tasks per SR-CRE-01). A milestone created from a cell lands on that
  row/date.
- **SR-CRE-08**: Every context menu shall be complete for its target zone
  (mirrors the app bar model — shared registry stays the single source).

### Editing affordance completeness
- **SR-EDT-01**: The task context shall expose Rename directly (button or
  menu item mapping to the card/inline label editor) — parity with row
  Rename; double-click stays as the shortcut.
- **SR-EDT-02**: The global SCALE/Labels/Grid group shall be reachable while
  an item is selected (appended compact at the app bar's right end in all
  contexts, or one-click reachable). Changing scale must not drop the
  current selection.
- **SR-EDT-03**: The ±1d nudge buttons shall be visually directional
  (distinct −/+ glyphs), not two buttons both reading "1d".

## 4. Verification Approach (E2E with Native Seams)
- New scenarios/traces (harness_driver): `create-task-empty-chart` (delete
  all tasks first, then drag-create + menu-create + double-click-create; each
  must yield taskCount +1 in PP_DOC), `create-milestone-cell`,
  `hover-quick-add-task` (chip adds TASK not ROW; rowCount stable,
  taskCount +1), `scale-while-task-selected` (scale group present, selection
  preserved), `task-rename-route`.
- Assertions via DumpChromeStateForTest + PP_DOC JSON: taskCount/rowCount
  deltas, ownSel set to new element (SR-CRE-03), rowLabelCount stable,
  appBarGroups contain expected commands per context.
- Pixel checks: hint pill visible in hover-hold capture (SR-CRE-05);
  no-flash invariant on every creation trace.
- All runs: `python native/tools/harness_driver.py trace/scenario ... --check-invariants`.

## 5. Non-Functional / Constraints
- Shared registry (GanttAppBar.h model → menus) remains the single source of
  truth for commands; new commands extend `HtMenuCmd` in GanttHitTest.h.
- Ground rules of docs/onslide-v4-plan.md §1 (tokens, DPI, header-only bias,
  one undo entry per gesture, input-neutral harness).
- Colors/sizes from GanttTheme.h only.

## 6. Open / Related
- Brackets remain out of scope here (no on-slide bracket support yet —
  candidate for a later iteration; model support exists).
- Collapse chevron toggle (spec R2) tracked separately in the plan.
- Related: SRS_RowAndTaskSelection.md, SRS_ProgressEditing.md,
  SRS_DependencyEditing.md.
