# PowerPlanner On-Slide Direct Editing UI/UX Plan

This document serves as the master implementation plan and guidance manual for the **Coordinator Agent** and its specialized **Sub-agents** to transition PowerPlanner from a task-pane-driven workflow into a **fully on-slide contextual Gantt editor**, mirroring the interaction model of **think-cell**.

---

## 1. Architectural Vision: No Task Panes, Only Slide Direct Interaction

To achieve a true think-cell UX, the user must never leave the slide area. The entire editing, configuration, and creation loops happen directly on the active PowerPoint slide canvas:

```
+-------------------------------------------------------------------------+
| PowerPoint Slide Area                                                    |
|                                                                         |
|   [ Q3 Launch Plan ]  <-- Double-click to edit title inline             |
|   +------------------+------------------------------------------------+ |
|   | Design           |                [======Task Body======]         | |
|   |   (Hover highlight)|                ^ Hover: show % slider        | |
|   |                  |                ^ Right-click: custom menus    | |
|   +------------------+------------------------------------------------+ |
|   | (Hover "+" line) | <-- Click to insert row                          |
|   +------------------+------------------------------------------------+ |
|                                                                         |
+-------------------------------------------------------------------------+
```

### Core Architectural Pillars
1. **Slide View Subclassing**: Intercept Win32 window messages (`WM_MOUSEMOVE`, `WM_LBUTTONDOWN`, `WM_RBUTTONDOWN`, `WM_LBUTTONDBLCLK`) on PowerPoint’s document window to track mouse states and trigger contextual toolbars without breaking PowerPoint's native selection.
2. **Interactive Overlay Window**: Transform the layered overlay (`Overlay.cpp`) from a click-through display into an interactive event-handler. It must intercept clicks on its own handle zones and floating widgets, while routing background clicks back to PowerPoint.
3. **Fluent Ribbon Context Menu Integration**: Inject custom right-click actions directly into PowerPoint's native context menus (`ContextMenuShape`, `ContextMenuGroup`) using XML.
4. **State-preserving Reflow Engine**: Fast in-memory model reflow that updates the coordinate matrix in milliseconds and maintains active user focus on recreated shapes.

---

## 2. Gaps and Phase-by-Phase Roadmap

### Phase 1: Win32 Subclassing & Canvas Interception (Target: Sub-agent A)
* **Goal**: Enable the add-in to know where the mouse is relative to the Gantt chart elements and highlight rows/slots under the cursor.
* **Technical Strategy**:
  1. Find the PowerPoint active slide window handle (typically a window of class `screenClass` or `paneClassDC` / `ppViewNormal`).
  2. Implement an in-process Win32 Window Subclass (`SetWindowSubclass`) to intercept mouse movements and clicks.
  3. Map slide coordinate spaces using `PointsToScreenPixelsX` and `ScreenPixelsToPoints` (zoom and scroll-aware).
  4. Draw subtle background hovers (e.g. alternating row band highlights) on our layered overlay window based on cursor coordinates.

### Phase 2: Native Right-Click Menus & Floating Badges (Target: Sub-agent B)
* **Goal**: Replace the standard shape right-click menu with a contextual Gantt tool menu and display floating button toolbars.
* **Technical Strategy**:
  1. Update `IRibbonExtensibility::GetCustomUI` to include `<contextMenus>` extensions:
     ```xml
     <contextMenus>
       <contextMenu idMso="ContextMenuShape">
         <menu id="ppContextMenu" label="PowerPlanner Actions" imageMso="ChartTypeColumnInsertGallery">
           <button id="ppCtxAddRow" label="Add Row Below" onAction="OnCtxAddRow"/>
           <button id="ppCtxDeleteTask" label="Delete Task" onAction="OnCtxDeleteTask"/>
           <button id="ppCtxChangeScale" label="Change Time Scale..." onAction="OnCtxChangeScale"/>
         </menu>
       </contextMenu>
     </contextMenus>
     ```
  2. Modify `Overlay.cpp`'s layered window: remove `WS_EX_TRANSPARENT` globally, and in `WM_NCHITTEST` return `HTTRANSPARENT` for empty space and `HTCLIENT` for buttons or overlay controls (floating slider for `% complete`, drag handles).

### Phase 3: Direct Drag-and-Drop Row-to-Row & Drawing (Target: Sub-agent C)
* **Goal**: Drag a task bar vertically to change its row, and click-drag in an empty slot to draw a new task bar.
* **Technical Strategy**:
  1. Detect clicking on a task handle. Initiate custom dragging loop inside the subclassed mouse handler.
  2. Track vertical mouse delta. When crossing a row threshold, update the task's `rowId` in `PP_DOC`.
  3. When hover is held over an empty area, render a preview "phantom bar" aligned with the nearest day grid. Dragging to create sets the `start` and `end` dates immediately.

### Phase 4: On-Slide Inline Text Editing (Target: Sub-agent D)
* **Goal**: Double-clicking a row label, task label, or chart title should launch a borderless, native edit box directly over the slide shape.
* **Technical Strategy**:
  1. Catch `WM_LBUTTONDBLCLK` or PowerPoint selection double-click.
  2. Create a child Win32 `EDIT` control on the overlay window positioned exactly over the target shape's bounding box.
  3. Handle `WM_KILLFOCUS` and `VK_RETURN` to serialize the text, update `PP_DOC`, delete the edit box, and reflow the chart.

---

## 3. Coordinator Agent Verification Checklist

The **Coordinator Agent** must oversee all sub-agents and enforce strict validation loops. Do not accept completion of any phase until these tests succeed:

1. **Non-Gantt Interference Test**: PowerPoint's native shapes (ordinary circles, standard textboxes) must retain their normal click-and-drag behaviors. Subclassing must only hook events when a `PP_KIND` tag is matched or hover is within the active chart root area.
2. **Memory & Thread Safety Test**: Subclassing must be cleanly detached on disconnected add-in events (`OnDisconnection` or process exit). No lingering subclass procedures (`RemoveWindowSubclass`).
3. **High-DPI & Multi-Monitor Check**: Handles, highlighting, and toolbars must map pixel-for-pixel at 100%, 125%, 150%, and 200% Windows desktop scaling factors across secondary monitors.
4. **Undo Stack Preservation**: Modifying a task on-slide and reflowing must be represented as a single cohesive action on PowerPoint’s native undo-redo transaction layer, avoiding fragmentation.

---

## 4. V2 — Pure On-Slide Editor (Interaction Capture)

> Status: SHIPPED 2026-07-04 (15/15 units green, see `docs/on-slide-coordinator-log.md`). Supersedes the V1 interaction model above (V1 shipped 2026-06-30,
> see `docs/on-slide-coordinator-log.md`). V1 facts that constrain this plan:
> there is NO subclassable per-slide window (`FALLBACK_POLLING_ONLY`); the
> chart-wide overlay surface exists and persists (`overlay-chart-surface`); once a
> mouse-down lands on OUR overlay we receive real `WM_MOUSEMOVE` — drags are
> event-driven and smooth, polling is only needed to track chart position/zoom.

### 4.1 Vision

The PowerPoint shapes become a **render target only**. The user never selects or
manipulates a raw square/text box; the add-in's overlay owns every mouse event over
the chart area and exposes think-cell-grade semantic interactions:

- Click a bar → *our* selection chrome (not PowerPoint's), semantic object = task.
- Drag bar body → move task (day snapping, live ghost + date tooltip).
- Drag bar edge → resize (change start/end independently).
- Drag bar vertically → reassign row.
- Drag on empty row space → phantom bar → create task.
- Double-click bar/label/title → floating inline editor (label, start/end dates, %, color).
- Right-click → our own popup menu (TrackPopupMenu on the overlay — PowerPoint's
  shape context menu never fires because PowerPoint never sees the click).
- PowerPoint-native selection of chart internals is actively suppressed.

**Decision record**: keep native shapes as the render target (think-cell parity;
decks degrade gracefully for viewers without the add-in). Rejected alternative:
render the chart as a single picture — bulletproof selection-blocking but kills
the native-shape value prop. Revisit only if suppression (4.2-U2) proves too leaky.

### 4.2 Work units (dependency-ordered)

| id | unit | depends on | gate |
|---|---|---|---|
| U1 | `capture-surface` — whole CHART_ROOT area returns HTCLIENT; internal semantic hit-testing (bars, edges ±4px, milestones, labels, row bands, empty cells) computed headlessly from PP_DOC + layout. Extend PP_PROJ with y-projection (row tops/heights) so hit-testing never needs COM. Escape hatch: Alt+click → HTTRANSPARENT pass-through (select/move whole group); small "move chart" grip in chrome. | — | hit-test unit tests in ops harness (pure, COM-free); manual: click on chart never selects a child shape |
| U2 | `selection-suppression` — Tick watches PowerPoint selection; if a PP_KIND child (not the group) becomes selected via any path we can't intercept (Tab-cycling, Selection Pane, marquee started outside the chart), unselect within one tick and mirror to our selection model. | — | harness: programmatically select a child shape → cleared ≤200ms; normal shapes outside chart unaffected |
| U3 | `alpha-overlay` — migrate paint path from magenta color-key to `UpdateLayeredWindow` with per-pixel premultiplied alpha (GDI+ back buffer). Unlocks translucent hover bands, ghost bars, soft selection chrome. | — | overlay harness PNG shows real translucency; no magenta fringing; existing chrome renders identically or better |
| U4 | `own-selection-model` — internal selection state (task/milestone/row/none) driven by our hit-testing; selection chrome (frame, handles, badge) rendered on our surface; Esc clears. Keyboard (Delete, arrows) deferred to U9 pending focus-strategy discovery. | U1, U3 | manual + harness screenshot: clicking a bar shows our chrome, PowerPoint selection stays empty |
| U5 | `drag-move-resize` — mouse-down on bar/edge starts modal drag (SetCapture on overlay): ghost bar + live date tooltip, snap to whole days via PP_PROJ inverse; drop → GanttOps mutation → rebuild. Suppress Tick/auto-reflow during gesture (g_mutating). | U1, U3, U4 | SendInput-driven harness: drag bar +N px → doc dates shift exactly N/ptPerDay days; REFLOW PASS still green |
| U6 | `drag-row-and-create` — vertical drag reassigns row (`MoveTaskToRow` op exists); drag on empty slot draws phantom bar and creates a task on drop. | U5 | SendInput harness: vertical drag changes rowId; empty-space drag creates task with correct row+dates |
| U7 | `rebuild-in-place` — replace delete-group+re-emit with a diff pass: move/resize/retitle existing shapes by PP_ID, add/remove only deltas. Wrap each gesture with `Application.StartNewUndoEntry` so one gesture ≈ one undo step. Kills flicker, selection churn, and undo fragmentation. | U5 | harness: one drag gesture → single undo entry restores prior state; no shape-count churn on no-op |
| U8 | `floating-editor` — double-click task/milestone → floating card editor (label, start date, end date, %, color) using the focusable top-level window pattern from V1 inline-edit; Enter commits, Esc cancels. Covers task-bar labels (V1 gap). | U1, U4 | harness or manual: edit start date via editor → doc + shapes update; Esc leaves untouched |
| U9 | `keyboard-and-cursors` — per-zone cursors (move/ew-resize/ns-resize/crosshair); keyboard Delete/arrow-nudge for our selection (discovery: WH_KEYBOARD hook vs focusable overlay mode — decide, then implement). | U4 | manual checklist; no interference with PowerPoint typing outside chart |
| U10 | `dpi-and-monitors` — make the ADD-IN DPI-aware (V1 gap: only the test harness calls SetProcessDPIAware); verify overlay↔chart alignment at 100/125/150/200% and across monitors. | U3 | manual matrix at 4 scale factors; document results in this file |

Regression gates for every unit: `native\build.bat` [build] OK ·
`build-conformance.bat` fixtures pass · `build-ops.bat` OPS HARNESS OK ·
`build-reflow.bat` + `ppreflow.exe` REFLOW PASS.

#### 4.2.1 U10 manual verification matrix (DPI × monitor)

U10's automated gate (ops harness `HtScalePx`/edge-band unit tests, printing
`DPI HELPER OK`) only proves the scaling math is correct in isolation. The
add-in runs IN-PROCESS in POWERPNT.EXE, a per-monitor-DPI-aware process, so
`PointsToScreenPixelsX/Y` and `GetCursorPos` coordinates already arrive in
PowerPoint's DPI context — the overlay's chart-alignment was never the risk.
What DID need manual eyes: our own chrome's pixel constants (badge, toolbar
buttons, grip, hover '+' button, selection handles, frame inflation, tooltip)
staying usable (not hairline/unreadable at 150-200%, not comically oversized
at 100%), and hit zones (edge-resize bands) staying reachable at every scale.
This is user visual feedback, not a hard CI gate — run it opportunistically
across scale-factor/monitor changes, not on every commit.

Checklist — for each of the 4 Windows scale factors (100%, 125%, 150%, 200%),
on both the primary monitor and a secondary monitor with a DIFFERENT scale
factor if one is available (e.g. primary 100% / secondary 150%), verify:

| check | what to look for |
|---|---|
| **Overlay↔chart alignment** | Selection frame, badge, and toolbar sit exactly over the CHART_ROOT shape with no visible offset/drift; drag the PowerPoint window between monitors of different scale and confirm the overlay follows without a lag/misalignment frame. |
| **Chrome usability** | Badge text, toolbar button labels (Add/Del/-/+), and the tooltip text are legible (not hairline-thin, not clipped) at each scale; nothing looks pixel-doubled/blurry in a way that suggests a missed scale. |
| **Hit zones** | Task edge-resize cursor zone (±4px @ 96dpi, scaled per `HtScalePx`) is reachable and gives a resize (not a body-drag) right at the bar edge; the hover '+' insert-row button and the 'move chart' grip are both clickable at their visually-drawn location (no dead-zone offset from the visible chip). |
| **Drag threshold** | A deliberate small jiggle at mouse-down does not start a drag (click-vs-drag threshold scales with DPI so it stays ~the same physical distance across scale factors). |

Results (fill in per run — date, scale factors covered, monitor config, pass/fail + notes):

| date | scale factor(s) | monitor config | result | notes |
|---|---|---|---|---|
| _(pending)_ | 100/125/150/200% | _(pending)_ | _(pending)_ | _(pending — run manually per the checklist above and record here)_ |

### 4.3 Risks / known constraints

1. **Suppression is best-effort**: Selection Pane / Tab-cycling can still select
   internals for up to one tick (150ms). Acceptable; `ReflowFromSlide` remains the
   reconciliation safety net for edits we didn't mediate.
2. **Right-click ownership moves to the overlay**: the V1 native `ContextMenuShape`
   items stop firing over the chart (clicks no longer reach PowerPoint). Reuse the
   same 12 handlers behind a TrackPopupMenu; keep the ribbon items as backup.
3. **Keyboard focus on a NOACTIVATE overlay** is unsolved (V1 finding) — U9 starts
   with a discovery spike, not implementation.
4. **Z-order**: overlay vs PowerPoint dialogs/flyouts — verify menus/dialogs still
   appear above the overlay (they are activated windows; expected fine).
5. **Undo API**: `Application.StartNewUndoEntry` exists in the PowerPoint OM but its
   grouping semantics with rapid shape edits need a discovery probe inside U7.

## 5. Run Execution Protocol (For Sub-agents)

Specialized agents must be spawned with clear instructions based on this document:
```powershell
# To compile the native codebase at any point:
.\native\build.bat

# To run the automated layout tests:
.\native\build-conformance.bat

# To run the end-to-end reflow verification:
.\native\build-reflow.bat
```
The Coordinator must verify build success at every step before advancing.

## 6. V3 — Row-Centric Editing + Bottom App Bar

> Status: PLANNED (user feedback session 2026-07-04). Builds strictly on the V2
> interaction layer (overlay capture, own selection, drag engine, GanttOps →
> RebuildChart, one-gesture-one-undo, shared harness).

### 6.1 Vision (user feedback distilled)

1. **Fit-to-slide by default** — a newly inserted chart fills the slide's content
   area, leaving the top ~15% free for a native PowerPoint title placeholder.
2. **Everything is a row** — no separate "category" concept in the UX. Every row is
   equal: any row can hold tasks/milestones AND child rows (indent/outdent). A
   category is just a row others were indented under. (The model already works this
   way — `PpRow.groupId` — this is a UX unification, not a data migration.)
3. **Left rail as a first-class label surface** — per-task label placement:
   on-bar, in the left rail, or both. Rail width becomes a real layout input.
4. **Generic movable markers** — every vertical marker (Today, deadlines, custom)
   is draggable and label-editable; more can be added. "Today" is just a default label.
5. **Text elements** — new `PpText` model element; free-floating or anchored to a
   task/milestone (`anchorId`), so anchored text follows its anchor on every rebuild.
6. **Grid + scale controls** — doc-level timescale-grid visibility/style options
   alongside the existing D/W/M scale ops.
7. **Bottom app bar** (PowerNote `BottomToolbar` pattern) — a docked contextual
   strip at the bottom of the slide area; its content swaps with the internal
   selection (task / milestone / row / marker / text / none). PRIMARY contextual
   surface. Right-click menus stay as shortcuts, driven by the SAME command map.
8. **Dependency UX** — create/remove dependencies from the app bar (link mode:
   click source's Link button, then click target task). Connectors already render.

**Decision record (user, 2026-07-04):** app bar primary + right-click kept (one
shared command map) · uniform rows with optional hierarchy (keep `groupId`) ·
generic movable markers · fit-to-slide reserves a top title zone.

### 6.2 Work units (dependency-ordered)

| id | unit | depends on | gate |
|---|---|---|---|
| V3-1 | `fit-to-slide` — InsertGantt sizes/positions CHART_ROOT to the slide content area below a reserved title zone (top ~15%). Approach: build as today, then set group frame, then ReflowFromSlide re-projects (pure layout + conformance untouched). | — | reflow harness asserts inserted frame ≈ content area → `FIT OK` |
| V3-2 | `marker-model-ops` — pure ops: `AddMarker`, `SetMarkerDate`, `SetMarkerLabel`, delete via existing `DeleteById`; marker type `custom` allowed. | — | ops harness `MARKER OPS OK` |
| V3-3 | `marker-drag` — hit-test zone for vertical markers (±band, DPI-scaled), ew-resize cursor, modal drag with ghost line + date tooltip, commit `SetMarkerDate`. | V3-2 | overlay harness stage `MARKERDRAG PASS` |
| V3-4 | `text-model` — `PpText` {id,label,anchorId,rowId,date,dx,dy}; JSON round-trip; layout (anchored → relative to anchor + offset; free → row+date); emitter (`PP_KIND=TEXT`); ops Add/SetLabel/Move/Delete. | — | `TEXT OPS OK` + conformance fixtures pass |
| V3-5 | `text-interaction` — hit zone Text, select/drag (anchored drag adjusts dx/dy; free drag moves row/date), double-click reuses card editor, Delete key works. | V3-4 | overlay harness stage `TEXT PASS` |
| V3-6 | `label-placement` — per-task `labelPlacement` (bar\|rail\|both, default bar); rail renders task labels for rail/both; rail width a layout input; `SetLabelPlacement` op. | — | `LABEL OPS OK` + conformance |
| V3-7 | `row-uniform-ux` — ops `AddRowAbove`, `IndentRow`, `OutdentRow` (via groupId, one level); uniform row treatment in hit zones/menus. | — | `ROW OPS OK` |
| V3-8 | `grid-scale-options` — all FIVE scales become real (year\|quarter\|month\|week\|day drive a two-tier axis: primary band + separator ticks, e.g. week → months + Monday separators, quarter → years + Q1..Q4); separator density override `gridDensity` (auto\|year\|quarter\|month\|week\|day\|none) + `gridStyle` (solid\|dotted); `SetGridDensity`/`SetGridStyle` ops; separator cap ~150 with coarser fallback. Conformance fixtures are a layout-only cross-impl contract with the web TS engine — never regenerated. | — | `GRID OPS OK` + full suite |
| V3-9 | `appbar-shell` — second docked layered NOACTIVATE strip at the bottom of the slide area; PURE app-bar model `BuildAppBar(selection, doc-state) → sections/buttons` (COM-free, ops-testable); GDI+ paint (shared Material palette), hover, click → command ids. Shows when a chart is on the active slide; obeys the same foreground scoping as the chart overlay (V3-13). | V3-2, V3-4, V3-6, V3-7, V3-8, V3-13, V3-14 | ops `APPBAR MODEL OK`; overlay harness stage `APPBAR PASS` |
| V3-10 | `appbar-actions` — ONE shared command registry (refactor menu `MapMenuCommand` into it); wire every app-bar button per context: task (nudge/dates/percent/color/placement/add-text/delete), milestone, row (add/indent/outdent/delete), marker (date/label/delete), background (scale, grid, add row/marker/text). | V3-9 | overlay harness stage `APPBARACT PASS` (button click → doc mutation) |
| V3-11 | `dependency-ux` — app bar Link button → link mode (crosshair + banner), click target task → `AddDependency` (finish-to-start default), Esc cancels; Unlink removes deps touching selection. | V3-10 | `DEP OPS OK`; overlay harness stage `DEP PASS` |
| V3-12 | `context-menu-v3` — right-click menus rebuilt FROM the shared command registry: add milestone (new `AddMilestone` op)/text/marker, grid + scale submenus (all five scales), link mode entry. | V3-10 | new marker `MENU MAP V3 OK`; all harness stages green |
| V3-13 | `fix-overlay-scoping` — **user bug 2026-07-04**: overlay chrome floats above OTHER apps (seen over CurseForge) because the TOPMOST overlay is only hidden when no chart exists, never on foreground/iconic checks. Fix: Tick hides all our windows (overlay, editor, card) unless PowerPoint is foreground (or the foreground window is ours / a POWERPNT-process dialog) and not minimized. Harness must explicitly foreground PowerPoint before overlay stages. | — | overlay harness stage `SCOPE PASS` |
| V3-14 | `material-theme` — real Material Design pass: shared Material-3 tonal palette in a new pure `GanttTheme.h` consumed by BOTH the shape emitter (bars, rail, header, gridlines, markers, deps) and the overlay chrome (selection, toolbar, ghost, tooltip, card editor); modern look matching the web app's feel with a better palette. Slide-export PNG artifact for visual inspection. | V3-8, V3-13 | new marker `THEME PNG OK` + full suite + fresh `native/build/material-theme-preview.png` |

Regression gates for every unit (same suite as V2): `native\build.bat` [build] OK ·
`build-ops.bat` all markers · `build-conformance.bat` fixtures pass ·
`build-overlay.bat` + `ppoverlay.exe` all stages exit 0 ·
`build-reflow.bat` + `ppreflow.exe` REFLOW PASS. PowerPoint killed before/after COM gates.

### 6.3 Risks / known constraints

1. **Two overlay windows** (chart overlay + app bar) must never fight for capture;
   the app bar is outside the chart rect so NCHITTEST domains are disjoint — verify.
2. **App bar on a NOACTIVATE window**: clicks must not activate/steal focus
   (V2 pattern holds), and TrackPopupMenu-style flicker must not regress.
3. **Anchored text vs rebuild-in-place diff**: TEXT shapes need stable PP_IDs so
   `UpdateGantt` moves rather than recreates them.
4. **Fit-to-slide vs conformance fixtures**: sizing happens AFTER pure layout
   (frame + reflow), so fixtures stay byte-stable; do not resize inside layout.
5. **Grid options widen PP_DOC schema**: keep JSON round-trip backward-compatible
   (absent fields → current defaults) so existing decks load unchanged.
