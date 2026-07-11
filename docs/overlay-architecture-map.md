# Overlay Architecture Map (audit 2026-07-11)

Reference for agents working on `native/PowerPlannerAddin/Overlay.cpp` and the
v2.5.x iterations. Line numbers = working tree on 2026-07-11 (drift expected;
re-verify anchors before editing).

## Windows (all created lazily, destroyed in OverlayStop)
| Window | Class | Ex-styles | Show / Hide |
|---|---|---|---|
| Chart overlay `g_hwnd` | `PowerPlannerOverlay` | LAYERED+TOOLWINDOW+TOPMOST+NOACTIVATE | `ShowOverlayForChartRect` (~3690, SetWindowPos EVERY tick) / `HideOverlay` (~3641) |
| App bar `g_appBarHwnd` | `PowerPlannerAppBar` | same | `ShowAppBar` (~4250, change-gated) / `HideAppBar` |
| Inline editor `g_editorHwnd` | `PowerPlannerInlineEditor` | TOOLWINDOW+TOPMOST | open/hide on demand |
| Card editor `g_cardHwnd` | `PowerPlannerCardEditor` | TOOLWINDOW+TOPMOST | open/close; hidden on host-inactive |

Blue frame, 8 handles, "PowerPlanner" chip, ghosts, hover wash, link pill are
ALL painted inside the chart overlay window by `PaintOverlay` (~3150-3539).
Chip (~3483-3502) shows only when `g_hasSelectionChrome && g_ownSelKind.empty()`
= native CHART_ROOT selection branch in Tick (~5505-5522).

## Visibility gating (Tick, 150 ms)
`IsHostActiveForOverlayChrome` (~5332-5358): test override (harness-process
only — never set inside the DLL) → `!IsIconic(ppRoot)` + `IsWindowVisible` →
foreground is ppRoot / one of ours / **any window of the POWERPNT pid** (the
loose branch). **No slideshow / view-type check exists anywhere.** Chart rect
from `GetActiveWindow()->PointsToScreenPixelsX/Y` (~5455-5463) — no
foreground/monitor validation of that window. Hide paths pair
HideOverlay+HideAppBar (g_app null, no active window, host inactive, no
CHART_ROOT, all catch blocks).

## Confirmed defects (v2.5.0 targets)
1. Ghosts on other monitors: no view-type guard + pid-based host-active +
   blind GetActiveWindow tracking (details: spec/srs-native/SRS-OverlayLifecycle.md).
2. Per-tick unconditional `SetWindowPos(HWND_TOPMOST)` in
   ShowOverlayForChartRect (~3690) — app bar shows the correct pattern (~4249).
3. Redundant double repaint when chartChanged (~5571-5577).
4. Every edit = full `UpdateGantt` Ungroup(GanttBuilder.cpp ~374) → Delete
   (~404-445) → re-add → Group (~484) via `RebuildChart` (~4524), called from
   ~15 sites; LockWindowUpdate only on row ops (~4304, ~4791) with fragile
   multi-exit unlocks. Everything else flashes.
5. `ppappbarshot.exe` attaches to a live user PowerPoint (COM
   single-instance), forces host-active override, shows real chrome on the
   user's desktop.

## Monolith stats & split path (audit)
Overlay.cpp = 5,785 lines, 132 `g_*` globals, 6 WndProcs, ~17 concerns.
Build = hand-written .bat with COPY-PASTED source lists (Overlay.cpp named in
5 bats, GanttBuilder.cpp in 6; vcvars64 path hardcoded in 11). No
CMake/sln. **Header-only extraction requires zero .bat edits** — the low-risk
split path:
- Tier A (pure, safe now): OverlayMetrics.h (DPI/Scale), OverlayGeometry.h
  (AddRoundRect/DrawHandle/color helpers), OverlayFormat.h (Narrow/Widen/
  ParseIsoDateStrict/...).
- Tier B (TU-fragment `.inc.h` included after the globals block):
  OverlayAppBar.inc.h (~830 ln), OverlayCardEditor.inc.h (~600),
  OverlayInlineEditor.inc.h, OverlayDrag.inc.h (~550),
  OverlayHotkeys.inc.h, OverlayContextMenu.inc.h, OverlayTestSeams.inc.h.
- Enabler: bundle globals into ~6 structs in OverlayState.h.
- Build quick win: shared source list via `native/sources.bat` `call`ed by
  all harness bats.

Theme/token divergence (3 copies): docs/design-tokens.md ⇄ GanttTheme.h ⇄
src/styles/global.css — already diverged (web accent #4f46e5 vs native
#4355E0; axis 56px vs 30pt; rail 200px vs 150pt). Web never consumes the doc.

Harness monoliths: overlay-test.cpp 3,280 ln; ops-test.cpp 1,730 ln;
harness_driver.py 817 ln (run/scenario/golden/trace CLI).

Hot files by churn (last 30 commits): Overlay.cpp > overlay-test.cpp >
ops-test.cpp > GanttHitTest.* — the monoliths ARE the hot files.
