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

## 4. Run Execution Protocol (For Sub-agents)

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
