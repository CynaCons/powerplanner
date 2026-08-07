# The overlay — what it is, why it exists, and why it keeps breaking

Status: written 2026-07-19, after four overlay defects shipped in two days.
Written to be picked up cold; assumes no context.

## 1. Why there is an overlay at all

PowerPlanner's deliverable is a **PowerPoint deck**, not an app window. The Gantt
chart is authored as real native PowerPoint shapes (`CHART_ROOT`, a group of
tagged children — see `GanttScene.h`). That is a hard requirement, not an
implementation choice:

- the deck must open and print correctly on a machine with no add-in installed
- it must export to PDF/PNG, animate, and survive Save/Reopen round-trips
- a colleague without PowerPlanner must be able to nudge a bar with the mouse

So the chart **must** be shapes.

But PowerPoint's own interaction model for shapes is wrong for a Gantt. Dragging
a rectangle moves a rectangle; in a Gantt it should change a *date*. Resizing
should change *duration*, snapped to a day grid. Clicking a bar should select a
task *unit* (bar + progress fill + label), not one child rectangle. And a hover
highlight or a drag ghost must never end up saved into the deck.

PowerPoint offers no extensibility point for this. An add-in gets ribbon UI, the
COM object model, and selection-change events. It gets **no mouse hook on the
slide canvas, no custom hit-testing, and no way to draw ephemeral pixels**.

Hence the overlay: a transparent, click-through-where-empty window floated
exactly over the chart, which

1. **intercepts input before PowerPoint sees it**, so a drag on a bar becomes a
   date edit instead of a shape move; and
2. **draws chrome that must not become part of the document** — hover outlines,
   drag ghosts, date pills, the app bar, expand chevrons.

The one-line version: **the document must be shapes; the interaction must not
be.** The overlay is a synthetic interaction layer for a host that offers none.

That framing also explains the cost. We are maintaining a second coordinate
system that must track, to the pixel, a window we do not own and get no
notifications from. Nearly every defect below is a consequence of that.

## 2. Physically: four windows

All four are `WS_POPUP` + `WS_EX_LAYERED`, owned by PowerPoint's root window
(`EnsureChromeOwner`, `Overlay.cpp`). Ownership — rather than `WS_EX_TOPMOST` —
is what makes them sit above PowerPoint and follow its minimize/restore without
floating over unrelated apps. (They *were* topmost-and-unowned until 2026-07-18;
that is why the chart appeared over Chrome.)

| Window | Role | Positioned from |
|---|---|---|
| `g_hwnd` | main overlay: capture layer, selection frame, hover, ghosts, badge | chart rect ± `INFL`, plus a `BADGE_H` strip on top |
| `g_appBarHwnd` | bottom app bar (Insert / Scale / Settings) | centred under the chart, clamped to the slide |
| `g_editorHwnd` | inline rename editor | the edit region's screen rect |
| `g_cardHwnd` | card editor popup | anchor rect, clamped to the monitor work area |

## 3. The paint model, and the trap inside it

The overlay never uses `WM_PAINT` for content. Every frame is composed into a
32-bpp premultiplied-alpha bitmap and pushed whole:

```cpp
// Overlay.cpp — RenderOverlay()
Gdiplus::Bitmap surface(w, h, w * 4, PixelFormat32bppPARGB, (BYTE*)g_bufBits);
g.Clear(Gdiplus::Color(0, 0, 0, 0));
PaintOverlay(g, w, h);
BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
::UpdateLayeredWindow(g_hwnd, NULL, &dst, &size, g_bufDc, &src, 0, &bf, ULW_ALPHA);
```

**The trap:** on a `ULW_ALPHA` layered window, hit-testing is per-pixel against
the alpha channel. A pixel with alpha 0 is not merely invisible — it is
*click-through*, and `WM_NCHITTEST` is never sent for it. Transparency and input
are the same channel.

So `g.Clear(Color(0,0,0,0))` made the entire plot area click-through. Only the
row-label rail — which had actual paint on it — could receive the mouse. That is
the whole explanation for "the app only works on the left side": every mouse
event over the chart body went straight through to the PowerPoint shape
underneath. The fix is a deliberate alpha-1 capture layer filled over the chart
rect before anything else is drawn — visually invisible, but opaque to input.

Anything that must be clickable therefore has to be **inside the capture
layer**. A control painted outside the chart rect renders fine and silently eats
nothing.

## 4. The coordinate chain

| Stage | Space | Conversion |
|---|---|---|
| shape geometry | slide **points** (float) | `chart->GetLeft/GetTop/GetWidth/GetHeight()` |
| → screen | **screen pixels** | `win->PointsToScreenPixelsX/Y()` — the only primitive |
| → window-relative | **overlay client px** | `− g_windowOriginX/Y` |
| → composed | back-buffer px (1:1 with client) | `PaintOverlay` |
| → screen | screen px | `UpdateLayeredWindow(dst = GetWindowRect().topleft)` |

`g_windowOriginX/Y` is written in exactly one place (`ShowOverlayForChartRect`)
and subtracted in ~70. Two things follow:

- Globals are **not** in a uniform space. `g_chartScreenRect`, `g_rowBands[]`,
  `g_hoverBandRect`, `g_headerBandScreenRect` are screen; `g_frameRect`,
  `g_gripRect`, `g_buttonRects[]` are client. The name does not always say so.
- There are **two** screen↔client idioms: the stored origin (used by hit
  testing) and `ScreenToClient` on the real window (used by `WM_NCHITTEST` and
  cursor logic). They agree only while the stored origin matches the real window
  rect — which is not guaranteed mid-tick.

## 5. The tick — polling, not events

```cpp
g_timer = ::SetTimer(NULL, 0, 150, TimerProc);   // Overlay.cpp
```

Everything is driven by a 150 ms poll: read the chart rect from COM, rebuild row
bands, reconcile selection, reposition the windows, repaint if anything changed.

**There is no event-driven repositioning at all.** No `WM_WINDOWPOSCHANGED`
handler, no scroll hook, no `WM_DISPLAYCHANGE`, no `WM_DPICHANGED`. The wheel is
swallowed and re-posted asynchronously (`::PostMessageW(target, msg, wp, lp)`),
so PowerPoint scrolls on its own schedule while the overlay waits for its next
tick.

Consequence: during any scroll, zoom, window resize, Ribbon expand/collapse or
pane dock, the entire overlay — capture layer, frame, row wash, app bar — trails
the chart by up to 150 ms and then snaps. This is the most likely thing a user
means by "the overlay position is not correct" while interacting.

## 6. Why this keeps breaking — the structural reasons

**a. Position has two sources of truth.** `ShowOverlayForChartRect` stores
`g_windowOriginX/Y`; `RenderOverlay` reads the *real* `GetWindowRect`; the paint
code uses the *stored* origin. Between the tick reading the chart rect and the
tick moving the window there are ~130 lines including COM calls that pump
messages and can trigger a repaint. `SetWindowPos`'s return value is never
checked.

**b. Geometry is cached on a key that includes the possibly-stale value.** Row
bands are cached on `(childCount, chartScreenRect, PP_ROWY, PP_PROJ)`. If
`PP_ROWY` goes stale, the cache key is stale too, so nothing invalidates and the
staleness is self-reinforcing. This produced a real fail-open: the first
fast-path edit after every rebuild skipped the `PP_ROWY` rewrite because the
change-detector had no baseline, so row hit zones sat at the previous lane
boundaries. Fixed 2026-07-19 by treating "no baseline" as *rewrite* rather than
*assume unchanged*.

**c. DPI is a process-wide global shared by windows that can be on different
monitors.** `g_dpi` is probed from the overlay window in
`ShowOverlayForChartRect` and then again from the app-bar window in
`ShowAppBar` — which runs *later in the same tick*, after the overlay has
already been sized from the previous value. The app bar is docked below the
chart and can legitimately land on a different monitor.

**d. The add-in never declares a DPI awareness mode.** There is no manifest
anywhere in the repo and no `SetProcessDpiAware*` / `SetThreadDpiAwarenessContext`
call in `native/PowerPlannerAddin/`. The code documents the assumption —

> "The overlay runs IN-PROCESS in POWERPNT.EXE, a per-monitor-DPI-aware process,
> so COM coordinates … already come back in PowerPoint's DPI context"

— but nothing enforces it, and PowerPoint's mode is user-switchable (Office's
"Optimize for compatibility" vs "best appearance"). Note the asymmetry: **every
test harness under `native/render/` *does* call `SetProcessDPIAware()`; the
shipping add-in does not.** A DPI divergence between harness and product is
compiled into the build.

**e. Screen geometry is derived from the chart shape, not from what is painted.**
The chart *shape* is wider than the timeline — it reserves a right gutter for
labels and notes that overflow the axis. Deriving the axis header band from the
chart rect therefore overshoots, and a control anchored to `band.right` lands in
dead space. This shipped: the expand chevron rendered ~175 px outside the chart
content. Fixed 2026-07-19 by sourcing the band's horizontal extent from the
painted `HEADER_BAND` child.

## 7. What the test harness can and cannot see

`ppappbarshot` drives the overlay **in its own process**, COM-free, against a
synthetic scene. That makes it fast and deterministic, and structurally blind to
most of the above. It has missed, by construction:

| Defect | Why the harness could not see it |
|---|---|
| Alt-latch input loss | evaluates the *other* branch of the faulty expression via a test seam |
| Topmost/unowned chrome | no foreground-application semantics |
| Alpha-0 click-through | no real layered-window compositor |
| Header band overshoot | no PowerPoint shapes, so no painted band to disagree with |

Two gates were additionally found to certify the wrong thing: the `trace`
subcommand treats `--check-invariants` as opt-in (so a bare `trace` run reports
PASS with zero invariants evaluated), and `window_preview_header_pixel_diff`
passes **only when a stale orphan app-bar window is present** — green on a dirty
machine, red on a clean one.

**The conclusion this points to:** for input routing, window styles, DPI and
compositing, a **recorded live session is the primary gate** and the COM-free
harness is a fast secondary. A recording carries both the chrome state and the
COM entity dump, so it can compare what the overlay *believes* against what
PowerPoint *actually drew*. That is how the header-band overshoot was found and
proven (`session_report.py`, rule `window_ports_inside_axis_header`), and it is
the only class of gate that has caught a live-only defect so far.
