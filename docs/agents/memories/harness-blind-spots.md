# Memory — test infrastructure that cannot see the defect reports green

One dated fact per heading. Corrected in place. Moved here 2026-10-23 from the
tail of `PLAN.md`, where it had grown into prose inside the backlog section.

This is the most expensive lesson in the project's history and it generalises
past the native harness: **green from a layer that cannot observe the failure
mode is not evidence, and it was repeatedly read as evidence.**

## The pattern (2026-07-19)

Five defects plus three invalid gates, all from the same cause:

1. **The Alt latch.** The overlay returned `HTTRANSPARENT` from `WM_NCHITTEST`
   because the Alt escape hatch read `::GetKeyState(VK_MENU)`, which is only
   current as of the last message the thread dequeued. `WM_NCHITTEST` arrives by
   `SendMessage`, so Alt+Tab latched it and the overlay went permanently deaf to
   all mouse input while still painting. The harness takes the
   `g_cursorOverrideEnabled` branch, so it evaluates a *different expression* on
   the exact faulty line.
2. **Unowned-topmost z-order.** No foreground-app semantics in-harness.
3. **Alpha-0 click-through.** Layered `ULW_ALPHA` windows hit-test per-pixel
   alpha; no real compositor in-harness, so it is never exercised.
4. **Header-band overshoot.** The band was derived from the chart *shape* rect
   rather than the *painted* band, overshooting by the right-hand label gutter —
   measured 175 px adrift on a live recording. No PowerPoint shapes in-harness,
   so there is no painted band to disagree with.
5. **`PP_ROWY` staleness was fail-open.** The change detector had no baseline
   after a rebuild, so the first fast-path edit skipped the rewrite and left row
   bands at the previous lane boundaries. Unreproducible in a harness that never
   does rebuild-then-first-edit. The crisp test: the FIRST edit after opening a
   deck misaligns rows and an identical SECOND edit corrects them.

All five were found from live recordings or owner reports, not from the harness.

## Three gates that do not measure what they claim

- **`paint_cadence_min_hz` (SR-SMO-09) is not a valid measurement.** It swings
  1.59 / 7.33 / 7.48 / 10.41 Hz across four runs of *identical* code, p95 281 ->
  3141 ms, while `paintCount` stays deterministic at 35. It measures machine
  scheduling jitter. It can neither detect a regression nor validate a fix. Fix
  it before any recorder-overhead optimisation: measure under controlled load,
  or gate on a deterministic quantity (work-per-paint, paints-per-gesture)
  instead of wall-clock Hz. (2026-07-18)
- **`window_preview_header_pixel_diff` is INVERTED — it passes only on a dirty
  machine.** Across 6 runs the determining variable is `appBarWindowCount`, not
  the code: every run with a stale orphan app-bar window from a previous harness
  process (count=2) PASSES, and every clean run (count=1) FAILS, *including runs
  of unmodified code*. It measures app-bar window cleanup. Two consequences: a
  red here is not a regression signal, and the question it was meant to answer —
  does the axis header preview repaint mid-drag? — is still unanswered and may
  itself be a real defect. Do not "fix" it by re-baselining against a
  contaminated run. (2026-07-19)
- **`no_large_sel_drop_to_empty` is a false positive on any profile with leading
  no-selection steps.** The name says *drop* to empty, but
  `harness_driver.py:686` flags **any** step after index 0 whose `ownSelKind` is
  blank, never checking that selection was non-empty first. `subrow-dense`
  legitimately starts with nothing selected, so it reports FAIL on a sequence
  where selection only ever gains. Harmless today because scenarios gate only
  their declared invariants, but it is noise in every trace report and would
  mask a real drop. Fix: track whether a non-empty kind was seen before flagging
  a later blank. (2026-08-07)
- **A bare `trace` run reports PASS with zero invariants evaluated.**
  `--check-invariants` is opt-in. Always pass it.

## The web suite has the same shape of blind spot (2026-08-07)

Both agent-bridge defects — an unhandled reconnect rejection, and a stale token
file after a Windows kill — live in the *server goes away mid-session* state,
which the Playwright suite never enters: it spawns a bridge, uses it, and
disposes it at test end. Every automated layer was green while a session that
outlived its server was broken.

**And the mitigation test was itself invalid on first write.** The
kill-and-restart e2e passed *with the fix removed*, because the replacement
server came back before any reconnect attempt reached the failing path. It only
became a gate after inserting a deliberate 6 s outage so several backoff
attempts run against a dead server — verified RED without the fix, GREEN with
it.

**The rule this buys:** a regression test written for a defect is not a gate
until it has been *observed to fail*. Red-check every new gate by reverting the
fix. Never accept green on its own as evidence that a gate works.

## A DPI divergence is compiled into the native build (2026-07-19)

Every harness under `native/render/` calls `SetProcessDPIAware()`. The shipping
add-in declares no DPI awareness anywhere — no manifest, no
`SetProcessDpiAware*`, no `SetThreadDpiAwarenessContext` — while `Overlay.cpp`
documents a per-monitor-aware assumption that nothing enforces and PowerPoint's
mode is user-switchable. The harness and the product therefore run in different
DPI modes by construction. Open as defect F under the parked `v2.10.5`.
