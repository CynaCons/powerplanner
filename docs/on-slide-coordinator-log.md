# On-Slide UX — Coordinator Log

Durable, repo-tracked narrative for the `onslide-coordinator` skill. Re-read this
on every rehydrate. State of record: session SQL `todos` (status) + git (`[todo: <id>]`
markers). Intent of record: `docs/on-slide-ux-plan.md`.

## Backlog (seeded at bootstrap)

| id | depends on | gate |
|---|---|---|
| disco-ops-harness | — | `native\build-ops.bat` → `OPS HARNESS OK` |
| disco-slide-window | — | `native\build-window-probe.bat` → `window-classes.txt` exists |
| ops-model-mutations | disco-ops-harness | `native\build-ops.bat` → `OPS HARNESS OK` |
| ctx-menu-actions | ops-model-mutations | `native\build.bat` → `[build] OK` |
| overlay-material | — | `native\build.bat` → `[build] OK` (+ overlay png) |
| overlay-toolbar | overlay-material, ops-model-mutations, disco-slide-window | `native\build.bat` → `[build] OK` |
| hover-rowband | disco-slide-window, overlay-material | `native\build.bat` → `[build] OK` |
| inline-edit | disco-slide-window, ops-model-mutations | `native\build.bat` → `[build] OK` |

Every native unit also re-runs `build-conformance.bat` (`1/1 fixtures passed`) and
`build-reflow.bat` + `ppreflow.exe` (`REFLOW PASS`) as regression gates.

## Cycle log

- bootstrap — seeded 8 units + 9 dep edges; schema extended with structured columns;
  integrity clean (0 missing deps, 3 ready).
- self-review (rubber-duck) — folded fixes: GanttOps DLL-link edit moved into
  ops-model-mutations (owns native/build.bat for the link); ctx-menu no longer edits
  build.bat + must verify no dead onAction callbacks; disco-slide-window gate hardened
  (delete stale txt, print WINDOW PROBE OK, require class records or
  FALLBACK_POLLING_ONLY); ops gate also re-runs conformance+reflow. Overlay lane runs
  SERIALLY (shared Overlay.cpp) per skill default. COM-backed gates (reflow/probe) run
  with a timeout + PowerPoint cleanup to avoid hanging the loop; pure-visual overlay
  smoke screenshots are best-effort, not hard gates (user gives visual feedback).
- cycle 1 — dispatched in parallel (disjoint allowed_paths): disco-ops-harness,
  disco-slide-window, overlay-material. All three validated from clean builds + committed:
  - disco-ops-harness → `OPS HARNESS OK` → 007038d
  - overlay-material → `[build] OK` → ecb8130
  - disco-slide-window → `WINDOW PROBE OK` → 279c9c7
  KEY FINDING: window probe = FALLBACK_POLLING_ONLY (no per-slide child window to
  subclass). Architecture decision recorded: all on-slide interaction goes through OUR
  overlay window + the 150ms Tick() poller (GetCursorPos mapped via PP_PROJ), never by
  subclassing PowerPoint. hover-rowband / inline-edit / overlay-toolbar specs updated.
  Skipped reflow regression for these two (neither touched layout/json/builder logic).
- cycle 2 — ops-model-mutations validated (OPS HARNESS OK, 1/1 fixtures passed,
  [build] OK, REFLOW PASS) → bfecf63.
- self-review #2 (cycle1-review, code-review) — Overlay.cpp CLEAN (no per-paint GDI
  leak, transparency + Tick/auto-reflow preserved); build scripts do not fake markers;
  scale/conformance round-trip safe. Found 1 latent bug in the discovery harness
  window-probe.cpp (COM pointers Released after CoUninitialize; Close gate too wide) →
  logged as low-priority fix-window-probe-com-teardown (no dependents).
- cycle 3 — ctx-menu-actions validated ([build] OK; all 12 onAction callbacks resolve
  in GetIDsOfNames; MutateChart->GanttOps->re-emit verified real, not hollow) → dbed209.
  This delivers on-slide right-click editing: Add Task/Row, Delete, Nudge +/-1, % +/-10,
  Change Scale. (ctx-menu edits only Connect.*, outside conformance/reflow link sets.)
- cycle 4 — overlay lane + fix, validated serially:
  - overlay-toolbar → [build] OK + overlay harness links; inspected real (WM_NCHITTEST
    gates buttons, MA_NOACTIVATE keeps selection, g_mutating re-entrancy guard vs Tick,
    no throw out of WndProc) → 7f87aa9
  - fix-window-probe-com-teardown → WINDOW PROBE OK exit 0 → 54376c4
  Status: 7 units done. Remaining: hover-rowband, inline-edit (both serial on Overlay.cpp).
- self-review #3 (cycle4-review) dispatched over the overlay-toolbar diff + the two
  remaining specs (held next dispatch until findings land, since both build on the
  freshly-rewritten interactive Overlay.cpp). Known design risks to confirm: (a) overlay
  window currently covers only the selected shape's bounds, so a whole-chart row-hover
  band may need a larger/second window; (b) a child EDIT on a WS_EX_NOACTIVATE window may
  not get keyboard focus — inline-edit needs a focusable approach.
- self-review #3 result (cycle4-review): overlay-toolbar CLEAN (hit-testing, re-entrancy
  guard, leaks, selection all verified). One Low nit → fix-percent-noop-undo todo (no-op
  percent rebuilds spurious undo at 0/100%, same in ctx-menu). Part B was decisive: both
  remaining units would dead-end as written. Restructured backlog:
  - NEW overlay-chart-surface (keystone): make the overlay cover the whole CHART_ROOT and
    persist regardless of selection (today it is sized to the selected shape + hidden when
    nothing selected — no surface to draw hover on).
  - hover-rowband: subclassing language DELETED (FALLBACK confirmed sole path); draw on the
    chart-wide surface; row geometry from layout constants/ROW_LABEL Ys (PP_PROJ has x only);
    "+" hotspot HTCLIENT, rest click-through. Now depends on overlay-chart-surface.
  - inline-edit: use a SEPARATE focusable top-level editor window (main overlay is
    NOACTIVATE so a child EDIT cannot get keyboard focus); dbl-click via chart-wide surface
    making label regions HTCLIENT. Now depends on overlay-chart-surface.
- cycle 5 — dispatched overlay-chart-surface (serial on Overlay.cpp). Then fix-percent-noop,
  hover-rowband, inline-edit remain.

## Completion summary (all units green)

13/13 todos done. Full gate suite passes from a clean rebuild:
`native\build.bat` [build] OK · `build-conformance.bat` 1/1 fixtures passed ·
`build-ops.bat` OPS HARNESS OK · `build-overlay.bat` compiles ·
`build-reflow.bat`+`ppreflow.exe` REFLOW PASS.

Shipped commits (each gated, [todo: id] in message):
- 007038d disco-ops-harness · ecb8130 overlay-material · 279c9c7 disco-slide-window
- bfecf63 ops-model-mutations · dbed209 ctx-menu-actions · 7f87aa9 overlay-toolbar
- 54376c4 fix-window-probe-com-teardown · 7a42e80 overlay-chart-surface
- 07c89ad hover-rowband · 4ba1cce inline-edit · c4f2238 fix-percent-noop-undo
- 88718b5 regression-final-reflow-link (caught by the final full-suite gate: reflow
  harness missing GanttOps link)
- 31b627d regression-reflow-exception (caught by final-review: std::exception from a
  malformed PP_DOC escaped the 150ms WM_TIMER auto-reflow callback → PowerPoint crash;
  hardened ReflowFromSlide with catch(std::exception)+catch(...))

Self-reviews run: backlog (pre-dispatch), cycle1 diffs, cycle4 (overlay-toolbar + the two
remaining specs — produced the chart-wide-surface restructure), and final subsystem review.

Delivered on-slide UX (native C++ COM, no task pane): right-click editing menu
(add/delete/nudge/%/scale), interactive floating mini-toolbar, row-hover highlight + "+"
insert, double-click inline title/row-label editing, Material chart-wide overlay, pure
document-ops engine with a headless test seam, auto-reflow on drag.

NOTE for the user: the rebuilt DLL is registered at native/build/PowerPlannerAddin.dll;
RESTART PowerPoint to load the latest build. Remaining future work (not blocking): true
per-pixel-alpha translucent hover band (current is opaque-thin due to color-key), editable
task-bar labels, dark-mode theme, MSI/WiX installer (N6).

---

# V2 — Pure On-Slide Editor (interaction capture)

Intent of record: `docs/on-slide-ux-plan.md` §4 (V2). State of record: session SQL
`coordinator-v2.sqlite` (scratchpad) + git `[todo: <id>]` markers; this log is the
recovery narrative if the DB is lost.

## Backlog (seeded at V2 bootstrap, 2026-07-03)

| id | depends on | gate |
|---|---|---|
| alpha-overlay | — | `build.bat` `[build] OK` + `build-overlay.bat` compiles |
| disco-undo-entry | — | `ppundoprobe.exe` → `UNDO PROBE OK` + `undo-probe.txt` |
| disco-keyboard-focus | — | `ppkeysprobe.exe` → `KEYS PROBE OK` + `keys-probe.txt` |
| capture-surface | alpha-overlay | `build-ops.bat` → `OPS HARNESS OK` (pure hit-test tests) |
| selection-suppression | capture-surface | `ppoverlay.exe` → `SUPPRESS PASS` |
| own-selection-model | capture-surface, alpha-overlay, selection-suppression | `ppoverlay.exe` → `OWNSEL PASS` |
| drag-move-resize | own-selection-model | `ppoverlay.exe` → `DRAG PASS` |
| drag-row-and-create | drag-move-resize | `ppoverlay.exe` → `DRAGROW PASS` + `CREATE PASS` |
| rebuild-in-place | drag-move-resize, disco-undo-entry | `ppoverlay.exe` → `INPLACE PASS` + `REFLOW PASS` |
| floating-editor | own-selection-model | `ppoverlay.exe` → `EDITOR PASS` |
| keyboard-and-cursors | own-selection-model, disco-keyboard-focus | `KEYS PASS` (or `KEYS SKIPPED` per probe verdict) |
| dpi-and-monitors | alpha-overlay | `[build] OK` + `OPS HARNESS OK` (dpi helper tests) |

Standing regression on every native unit: `build.bat` `[build] OK` ·
`build-conformance.bat` fixtures pass · `build-ops.bat` `OPS HARNESS OK` ·
`build-reflow.bat`+`ppreflow.exe` `REFLOW PASS`. COM-backed gates (ppoverlay,
ppreflow, probes) require PowerPoint closed before and killed after, with timeouts.
Overlay.cpp lane is SERIAL (V1 lesson). Discovery probes are parallel-safe with the
overlay lane (disjoint paths); the two probes must not run their COM gates
concurrently with each other or with ppoverlay/ppreflow.

## V2 cycle log

- bootstrap-v2 — plan §4 written (10 units) + 2 discovery spikes seeded; schema with
  structured gate columns; integrity clean (0 missing deps, 3 ready:
  alpha-overlay, disco-undo-entry, disco-keyboard-focus). Ordering decision:
  alpha-overlay precedes capture-surface (paint plumbing before interaction code
  builds ghosts on it); suppression precedes own-selection-model (side-channel
  mirror feeds it).
