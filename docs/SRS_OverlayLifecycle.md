# SRS - Overlay Window Lifecycle, Visibility & Rendering Stability (ASPICE-style)

## 1. Feature Overview / Purpose
The on-slide editor chrome (chart overlay `PowerPlannerOverlay`, app bar
`PowerPlannerAppBar`, editors, and any future floating windows) consists of
TOPMOST layered windows painted over PowerPoint. Because they are top-level
desktop windows, any gating or positioning defect is visible over *everything*
on the user's desktop — including other monitors and other applications.

User-reported symptoms this SRS addresses:
- Blue rectangles with a "PowerPlanner" chip appearing **randomly on other
  screens** (outside PowerPoint, on secondary monitors).
- Frequent **flickering** of overlay elements.

The overlay must behave like part of PowerPoint: visible only when the user is
actually looking at the slide that owns the chart, always at the correct
screen position, and painted without visible churn.

## 2. User Goals & Interactions
- The user opens/uses PowerPoint normally; chrome appears only over the active
  slide's chart area in the active PowerPoint window.
- The user switches to another app, minimizes PowerPoint, moves PowerPoint to
  another monitor, switches slides, enters a slideshow, or closes the
  presentation → all chrome follows correctly or disappears **immediately**
  (within one tick), and never lingers at stale coordinates.
- The user works on other screens → no PowerPlanner pixels ever appear there
  unless PowerPoint (with the chart visible) is on that screen.
- The user watches the chart during operations → elements do not flicker,
  flash, or z-fight.

## 3. Software Requirements (Functional)

### Visibility gating
- **SR-LIFE-01**: There shall be exactly ONE visibility decision function
  (single authority) evaluated per tick that determines the shown/hidden state
  of ALL overlay windows together. No code path may show a window outside this
  decision.
- **SR-LIFE-02**: The decision shall require ALL of: (a) the tracked
  document window itself (its root HWND) is the foreground window — or the
  foreground is one of our overlay/editor windows or a popup owned by that
  document window; a foreground window merely belonging to the POWERPNT
  process (pid match) is NOT sufficient; (b) the presentation window visible
  and not minimized; (c) the tracked chart present on the CURRENTLY displayed
  slide in the active window; (d) normal edit view — hide when a slideshow is
  running for the presentation (SlideShowWindows), in print preview, or in
  protected/read-only view.
- **SR-LIFE-03**: When the decision turns false, ALL overlay windows shall be
  hidden within one tick (≤200 ms), including editors and menus owned by the
  overlay.
- **SR-LIFE-04**: Test overrides (`Overlay_SetHostActiveOverrideForTest` and
  similar) shall be compiled or hard-gated such that they can never activate
  in a normal add-in session (e.g. only honored when the harness explicitly
  arms test mode via a dedicated entry point in the same process).

### Positioning & multi-monitor
- **SR-LIFE-05**: Window placement shall be recomputed from the live
  PowerPoint window/slide geometry before every show and on every tick while
  shown; a show with a stale rect (older than the current tick's measurement)
  is prohibited.
- **SR-LIFE-06**: If the computed chart rect does not intersect the monitor
  containing the PowerPoint document window, chrome shall be hidden (fail
  closed), not shown at the computed coordinates.
- **SR-LIFE-07**: Per-monitor DPI: pixel constants and rect math shall be
  correct on mixed-DPI multi-monitor topologies, including stacked layouts
  where the secondary monitor occupies y > primary height (verified for
  topology primary 1920x1080 @ (0,0) + secondary 1440x960 @ (0,1080)).
- **SR-LIFE-08**: Harness/test runs that park PowerPoint "off screen" shall
  compute coordinates strictly outside the FULL virtual screen bounding box
  (SM_XVIRTUALSCREEN/SM_CYVIRTUALSCREEN union), never from primary-monitor
  dimensions.

### Teardown
- **SR-LIFE-09**: `OverlayStop`/add-in disconnection/document close shall
  destroy (not merely hide) all overlay windows; PowerPoint exit shall leave
  zero `PowerPlanner*`-class windows on the desktop (no orphan process-less
  windows, no leaked windows from harness kills).
- **SR-LIFE-10**: Harness executions (ppoverlay, traces, appbar-shot) shall
  clean up all overlay windows even when a stage fails or PowerPoint is
  taskkilled; a sweep step shall verify no `PowerPlanner*` windows remain.

### Rendering stability (no flicker)
- **SR-LIFE-11**: While the observable chrome state (selection, hover, rects,
  document) is unchanged, tick processing shall perform NO repaint
  (`UpdateLayeredWindow`), NO `SetWindowPos`/z-order call, and NO
  show/hide toggling. Idle ticks must be paint-free (verified by a paint
  counter seam).
- **SR-LIFE-12**: State changes shall repaint at most once per tick per
  window; chrome updates during drags shall not toggle window visibility.
- **SR-LIFE-13**: Document mutations (add row, rename, etc.) shall not
  produce a visible intermediate frame where previously visible content
  (labels, bars) is absent (flash). Measured via consecutive-frame captures:
  no trace step may show a frame whose content-region difference against both
  neighbors exceeds the flash threshold while ownSel is stable.

## 4. Verification Approach (E2E with Native Seams)
New/extended seams (all input-neutral, harness-only):
- `Overlay_DumpWindowStateForTest`: per-window {class, visible, rect, monitor,
  lastShowReason, lastPaintTick, paintCount, setWindowPosCount}.
- Paint/reposition counters readable at trace points → invariant
  "idle ticks paint-free" (SR-LIFE-11): run N idle ticks, require
  paintCount delta == 0.
- Host-active scenario stages: simulate deactivate/minimize/monitor-move via
  the existing `Overlay_SetHostActiveOverrideForTest` (harness-armed only) and
  a new rect-override seam; assert all windows hidden within one tick
  (SR-LIFE-03), assert fail-closed on off-monitor rects (SR-LIFE-06).
- Flicker probe: rapid capture (≥5 frames at tick cadence) of the chart
  region during idle and during each traced operation; compute frame-to-frame
  diff; report `flickerScore` in report.json; invariant threshold enforced
  (SR-LIFE-13).
- Desktop sweep check in harness_driver teardown: enumerate top-level windows;
  fail the run if any `PowerPlanner*` window survives (SR-LIFE-09/10).
- Scenarios: `overlay-deactivate`, `overlay-minimize`, `overlay-monitor-move`,
  `overlay-idle-stability`, plus flicker assertions added to existing traces.

## 5. Non-Functional / Constraints
- 150 ms polling remains the base cadence (event sink is a future
  optimization, ARC-07); requirements are expressed per-tick.
- All fixes stay inside the existing Overlay architecture (layered windows +
  Tick); no parallel window system.
- Ground rules of docs/onslide-v4-plan.md §1 apply (exceptions never escape
  Tick/WndProc, DPI via HtScalePx, tokens from GanttTheme.h, harness
  input-neutral, no new .cpp without updating all build .bat files — prefer
  header-only).

## 6. Open / Related
- Root causes CONFIRMED by code audit (2026-07-11), ranked:
  1. No slideshow/view-type guard + pid-based host-active (`fgPid == ppPid`
     branch, Overlay.cpp `IsHostActiveForOverlayChrome`): presenting or any
     POWERPNT-owned foreground window keeps CHART_ROOT chrome (blue frame +
     chip) painting at the edit window's monitor.
  2. `Tick` blindly follows `Application.GetActiveWindow()` with no
     foreground/monitor validation (multi-window / multi-presentation).
  3. Per-tick unconditional `SetWindowPos(HWND_TOPMOST)` on the overlay
     (z-order churn; app bar is already change-gated).
  4. Harness `ppappbarshot.exe` attaches to a LIVE user PowerPoint via COM
     single-instance, forces host-active override, inserts a chart (chip
     state), never moves off-screen → real chrome on the user's desktop
     whenever agent harness runs happen. Needs a "refuse if PowerPoint
     already running" guard (SR-LIFE-10 extension).
  5. ≤150 ms stale-position window on focus/monitor transitions (hide is
     immediate but reposition-while-hidden never happens).
  Note: `overlay-test.cpp` off-screen parking math was audited and is
  correct (uses full virtual-screen right edge + 200).
- Flicker causes CONFIRMED, ranked: (1) full Ungroup/Delete/re-add/Group in
  `UpdateGantt` on every edit, unmasked on most paths (only row ops use
  LockWindowUpdate, with fragile multi-exit unlocks); (2) per-tick TOPMOST
  re-assert; (3) hover-poll full `UpdateLayeredWindow` recomposites;
  (4) redundant double repaint per tick when chartChanged; (5) coarse
  LockWindowUpdate freezes; (6) app-bar full re-render per hover transition.
- ARC-07 (WindowSelectionChange event sink) would reduce idle work further.
- Related SRS: docs/SRS_RowAndTaskSelection.md (selection chrome states),
  upcoming SRS_SelectionChromeVisuals.md (what chrome looks like).
