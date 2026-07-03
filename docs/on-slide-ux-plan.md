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

> Status: PLANNED. Supersedes the V1 interaction model above (V1 shipped 2026-06-30,
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
