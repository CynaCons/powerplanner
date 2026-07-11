# SRS - Native Overlay Lifecycle, Visibility & Rendering Stability

Native-specific requirements for the PowerPoint COM add-in overlay windows:
`PowerPlannerOverlay`, `PowerPlannerAppBar`, editors, menus, and future
floating windows. These windows are top-level desktop windows, so visibility,
positioning, teardown, and repaint behavior must make them behave like part of
the active PowerPoint slide rather than independent desktop chrome.

This file migrates the legacy prose overlay lifecycle requirements for v2.6.8.

Traces up to: `../srs/SRS-powerpoint.md`, `../interaction.md`,
`docs/onslide-experience-spec.md`, `docs/onslide-v4-plan.md`,
`spec/srs-native/SRS-SelectionChromeVisuals.md`, and
`native/tools/harness_driver.py`.

Reference impl: `native/PowerPlannerAddin/` (Overlay.cpp, GanttHitTest.*,
GanttAppBar.h, app-bar/editor window classes, harness seams).

## Visibility Gating

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-LIFE-01 | The overlay runtime shall evaluate exactly one visibility decision function per tick as the single authority for the shown/hidden state of all overlay windows; no code path may show a window outside that decision. | A single authority prevents stale top-level windows from appearing over other applications or monitors. | Review Overlay show paths; harness state dumps assert all overlay/editor/appbar windows share the same gate result. | Legacy SR-LIFE-01; Overlay Tick; IsHostActiveForOverlayChrome |
| SR-LIFE-02 | The visibility decision shall require all host-state criteria: the tracked document root HWND is foreground or owns the foreground overlay/editor/popup, the presentation window is visible and not minimized, the tracked chart is present on the currently displayed slide, and normal edit view is active rather than slideshow, print preview, or protected/read-only view. | PID-only focus checks are too broad and caused chrome to paint outside the intended editing surface. | Harness scenarios `overlay-deactivate`, `overlay-minimize`, `overlay-monitor-move`, and slideshow/view guards; assert false gate hides every window. | Legacy SR-LIFE-02; host-active audit; Overlay foreground/window checks |
| SR-LIFE-03 | When the visibility decision turns false, all overlay windows shall be hidden within one tick (<=200 ms), including editors and menus owned by the overlay. | Switching apps, slides, monitors, or modes must remove all PowerPlanner pixels quickly. | Trace pre/immed/+1 captures for deactivate/minimize/slide-change; DumpWindowStateForTest visible=false for every PowerPlanner window. | Legacy SR-LIFE-03; overlay lifecycle scenarios |
| SR-LIFE-04 | Test overrides such as `Overlay_SetHostActiveOverrideForTest` shall be compiled or hard-gated so they cannot activate in a normal add-in session unless the harness arms test mode through a dedicated in-process entry point. | Test seams must not leak into user sessions and force chrome visible on live desktops. | Code review of override guards; harness-only activation test; normal add-in smoke test shows overrides inert. | Legacy SR-LIFE-04; test-mode seam |

## Positioning And Multi-Monitor

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-LIFE-05 | Window placement shall be recomputed from live PowerPoint window and slide geometry before every show and on every tick while shown; a show using a stale rect older than the current tick measurement is prohibited. | Overlay windows must track the chart after focus, slide, move, and monitor transitions. | DumpWindowStateForTest reports rect measurement tick; traces assert no show uses stale geometry. | Legacy SR-LIFE-05; Overlay chart rect computation |
| SR-LIFE-06 | If the computed chart rect does not intersect the monitor containing the PowerPoint document window, the overlay runtime shall hide chrome rather than show it at the computed coordinates. | Fail-closed behavior prevents random windows on secondary monitors or off-slide regions. | `overlay-monitor-move` and off-monitor rect override scenarios; monitor field in state dump; screenshot review. | Legacy SR-LIFE-06; monitor intersection gate |
| SR-LIFE-07 | Per-monitor DPI rect math and pixel constants shall remain correct on mixed-DPI multi-monitor topologies, including a stacked layout with primary 1920x1080 at (0,0) and secondary 1440x960 at (0,1080). | DPI mistakes cause visible drift and stale chrome on multi-monitor setups. | Manual/harness DPI matrix at 100% and 150% where available; coordinate assertions against HtScalePx-derived values. | Legacy SR-LIFE-07; DPI and monitor checklist |
| SR-LIFE-08 | Harness and test runs that park PowerPoint off screen shall compute coordinates outside the full virtual screen bounding box, not from primary-monitor dimensions alone. | Test isolation must not put live chrome on a user's real desktop. | Harness teardown and off-screen parking assertions using SM_XVIRTUALSCREEN/SM_CYVIRTUALSCREEN union. | Legacy SR-LIFE-08; overlay-test off-screen audit |

## Teardown

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-LIFE-09 | `OverlayStop`, add-in disconnection, document close, and PowerPoint exit shall destroy all overlay windows so zero `PowerPlanner*` top-level windows remain on the desktop. | Hidden but leaked windows can reappear, retain z-order, or pollute later harness runs. | Desktop sweep enumerates top-level windows after stop/close/exit; fails on any `PowerPlanner*` survivor. | Legacy SR-LIFE-09; OverlayStop; harness cleanup |
| SR-LIFE-10 | Harness executions such as ppoverlay, traces, and appbar-shot shall clean up all overlay windows even when a stage fails or PowerPoint is taskkilled, with teardown verifying no `PowerPlanner*` windows remain. | Failed automation must not leave chrome on a user's live desktop. | Harness teardown sweep; failure-path tests; optional guard that refuses to attach to a live user PowerPoint where applicable. | Legacy SR-LIFE-10; harness_driver teardown |

## Rendering Stability

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-LIFE-11 | While observable chrome state is unchanged, tick processing shall perform no repaint, no `SetWindowPos`/z-order call, and no show/hide toggling. | Idle polling must be paint-free to avoid flicker and z-order churn. | Paint/reposition counters from DumpWindowStateForTest; `overlay-idle-stability` requires zero deltas over idle ticks. | Legacy SR-LIFE-11; UpdateLayeredWindow/SetWindowPos counters |
| SR-LIFE-12 | State changes shall repaint at most once per tick per window without toggling window visibility during drags. | Bounded repaint work keeps drag and hover feedback stable. | Drag and hover traces assert paintCount deltas <=1 per tick and visibility remains stable during drag. | Legacy SR-LIFE-12; Overlay repaint batching |
| SR-LIFE-13 | Document mutations shall not produce a visible intermediate frame where previously visible content is absent while own-selection is stable. | Add, rename, and row operations must not flash blank labels or bars between rebuild steps. | Consecutive-frame capture compares content-region diffs against a flash threshold; no mutation trace may exceed the threshold against both neighbors. | Legacy SR-LIFE-13; no-flash invariant; row/task/creation traces |

## Open / Related

- Root causes from the 2026-07-11 audit remain useful trace context: slideshow
  and view-type gating, active-window tracking, unconditional topmost churn,
  live PowerPoint harness attachment, stale-position windows, full rebuild
  flashes, and coarse LockWindowUpdate masking.
- ARC-07 (WindowSelectionChange event sink) may reduce polling work, but these
  requirements remain valid with the tick as watchdog/fallback.
