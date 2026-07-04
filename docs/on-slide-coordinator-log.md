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
- self-review (pre-dispatch, rubber-duck) — 11 findings folded into the DB:
  (1) capture-surface gains build-overlay.bat + build-reflow.bat in allowed_paths +
  gate (hard-coded source lists would strand harness links — V1 88718b5 class);
  (2) keyboard-and-cursors prints 'KEYS PASS (cursors only…)' in the NONE branch so
  the pass signal is satisfiable in both branches; (3) COM hygiene: taskkill wrapped
  around every PowerPoint gate; first overlay-test toucher adds a 120s watchdog +
  always-cleanup (today it leaks POWERPNT on _com_error); disco-undo-entry corrected
  (window-probe is NOT a kill/timeout exemplar, only its COM-teardown order);
  (4) added dep dpi→capture-surface (GanttHitTest doesn't exist before it);
  (5) harness stage rule: fixed order, print FAIL + exit nonzero immediately;
  coordinator requires exit code 0 AND marker; (6) drag-row runs CREATE last, both
  markers verified; (7/8) vacuous gates fixed: alpha-overlay now gated on a real
  ALPHA PASS harness stage (LWA_COLORKEY absent/ULW in use), dpi gated on new
  DPI HELPER OK marker with the helper pinned into COM-free GanttHitTest;
  (9) PROTOCOL: coordinator holds a global PowerPoint mutex — never dispatch two
  units that drive POWERPNT concurrently; gates + regressions run serially by the
  coordinator. (10/11) description drift fixed (Esc deferral, AddTask already takes
  row+dates, RebuildChart not Connect.cpp's MutateChart, per-tick COM cost note).
- cycle 1 — dispatched in parallel (disjoint paths, PowerPoint mutex respected):
  alpha-overlay (sole POWERPNT user) + disco-keyboard-focus (no PowerPoint).
  disco-undo-entry deferred to next slot (would contend for POWERPNT).
- disco-keyboard-focus → KEYS PROBE OK, coordinator re-ran gate from deleted
  artifact, fresh stamp → 543dc86. KEY FINDING — VERDICT: HOTKEY. RegisterHotKey
  (Delete/Left/Right) delivers WM_HOTKEY to the unfocused NOACTIVATE overlay; keys
  are stolen system-wide WHILE registered and recover cleanly on unregister, so the
  add-in must register only while (internal selection active) AND (PowerPoint
  foreground), unregister on selection-clear/focus-loss, and check per-key
  registration failure (another app may own the hotkey). Rejected: LL hook (global
  footprint, AV risk), focus-switch (visible activation flicker on the host,
  WM_ACTIVATE round-trip proven). Caveat: probed with SendInput (LLKHF_INJECTED);
  RegisterHotKey routing should be identical for hardware input.
- alpha-overlay → validated from clean rebuild (ALPHA PASS + CAPTURE PASS exit 0;
  full regression 1/1 fixtures, OPS HARNESS OK, REFLOW PASS) → 40c43e9. Paint path
  now UpdateLayeredWindow + 32bpp PARGB DIB + GDI+ (init in OverlayStart/Stop, never
  DllMain); single repaint entry (RequestOverlayRepaint→RenderOverlay→PaintOverlay)
  for future drag ghosts; color-key + KEY constant deleted. TWO NEW FINDINGS:
  (a) Tick() needed a g_inTick re-entrancy guard — a blocked outgoing COM call's
  modal wait dispatches WM_TIMER, nested ticks were issuing COM mid-call;
  (b) HARNESS RULE for all future stages: overlay-test's PumpFor drained
  PeekMessage until queue-empty, but once a tick takes >150ms a due WM_TIMER is
  ALWAYS pending → infinite pump. Bound dispatch loops by TIME, not queue-empty.
  Also: harness now has 120s watchdog + unbuffered stdout (ExitProcess doesn't
  flush CRT buffers) + Quit/release on all exit paths. Registered add-in loads
  in-proc during harness runs → two overlay instances coexist; works, keep in mind.
- cycle 2 — dispatched in parallel: capture-surface (Overlay lane; forbidden from
  running POWERPNT — its gate is compile+ops only) + disco-undo-entry (owns the
  POWERPNT mutex this cycle).
- disco-undo-entry → UNDO PROBE OK, coordinator re-ran from deleted artifacts,
  fresh stamp → 29e209d. KEY FINDING — VERDICT: GROUPING_WORKS (PowerPoint 16.0):
  ONE StartNewUndoEntry call before a batch of automation edits collapses ALL of
  them (move+move+retitle 3/3; even delete+recreate) into ONE undo entry; no
  trailing/sealing call needed (trailing call harmless). Undo executed via
  late-bound CommandBars.ExecuteMso("Undo"). rebuild-in-place can therefore make
  one gesture == exactly one undo step, including full re-emits.
- capture-surface → validated from full clean rebuild (all 5 build gates + OPS
  HARNESS OK incl. 21 hit-test assertions + 1/1 fixtures + ALPHA/CAPTURE PASS +
  REFLOW PASS) → 41eca8e. GanttHitTest pure module (COM-free, no Windows headers);
  snapshot populated in the existing Tick child walk with a rect+count cache
  (per-tick COM cost = 2 calls on cache hit); NCHITTEST = HTCLIENT anywhere in
  chart rect, HTTRANSPARENT everywhere while Alt held; 'move chart' grip selects
  CHART_ROOT natively. THREE FLAGGED GAPS (expected, for next units):
  (a) selection chrome still keyed to PowerPoint selection, but chart clicks no
  longer reach PowerPoint → chrome only appears via grip/Alt until
  own-selection-model consumes g_lastHit; (b) snapshot cache keys on chart rect +
  child count — a child moved without changing either serves stale rects one
  cycle; (c) hit zone RowBand with EMPTY rowId = chart background (strip beside
  title), selection unit must treat it as background, not a row.
- self-review #2 (cycle2-review) dispatched over the alpha-overlay + capture-surface
  diffs and the selection-suppression / own-selection-model specs before further
  Overlay-lane dispatch (4 units landed since last checkpoint; both next units sit
  on the rewritten Overlay.cpp).
- self-review #2 result — selection-suppression SAFE as specced (grip/Alt whole-group
  path provably doesn't fight child-unselect; kind==CHART_ROOT check is one line in
  Tick's existing selection read). Folded: (a) own-selection-model AMENDED to own the
  SetCapture/ReleaseCapture/WM_CAPTURECHANGED lifecycle + 4px click-vs-drag threshold
  now (avoids drag-unit retrofit); (b) NEW unit fix-capture-hardening — catch-all in
  Tick (only _com_error caught today = crash risk), GDI+ GetLastStatus before ULW push,
  WM_MOUSEWHEEL forward (Ctrl+wheel dead over chart), InvalidateHitSnapshot on
  RebuildChart/ReflowFromSlide (stale-cache sequences), grip/badge overlap; (c) NEW
  unit overlay-context-menu — right-click over chart is dead by design since capture;
  overlay owns TrackPopupMenu with semantic zone-dependent items, pure
  zone→menu→op mapping tested in ops (MENU MAP OK), after own-selection-model +
  hardening. Clean: GDI+ lifetime/RAII, ULW resize handling, g_inTick cannot latch
  (RAII unwind). Noted, not blocking: Alt-tap can pop PowerPoint KeyTips (UX wart).
  TOKEN MODE: per user, sub-agents now run on Sonnet with condensed prompts.
- cycle 3 — dispatched selection-suppression (sonnet).
- selection-suppression → validated from clean rebuild (full suite: build, overlay
  stages, OPS, 1/1 fixtures, REFLOW PASS) → c2d49bb. GATE AMENDED (degraded, honest):
  the child-suppression half is UNSIMULATABLE — GroupItems->Select always resolves
  back to CHART_ROOT (PowerPoint OM limitation, confirmed empirically + research);
  Alt+click SendInput passthrough also failed to land a child selection (suspected
  GetKeyState race with SendInput key-state propagation). Harness verifies the
  CHART_ROOT exemption half for real and prints 'SUPPRESS PASS (child-select
  unsimulatable)'. NEGATIVE KNOWLEDGE: no COM/SendInput route creates a native
  child-of-group selection; Selection Pane is the only realistic user path; the
  suppression code covers it as cheap insurance and is exercised in-proc only.
  UIA (IUIAutomation) flagged unexplored if real reproduction is ever needed.
- cycle 4 — dispatched fix-capture-hardening (sonnet, Overlay lane serial).
- fix-capture-hardening → validated clean rebuild, full suite green → 4243863.
  Tick catch-all (std::exception + ...), GDI+ GetLastStatus gates before ULW push,
  WM_MOUSEWHEEL/HWHEEL forwarded to cached PowerPoint hwnd (self-target guard),
  InvalidateHitSnapshot on RebuildChart + reflow-changed, grip moved top-right
  (badge collision). Overlay.cpp only.
- cycle 5 — dispatched own-selection-model (sonnet): internal selection on
  mouse-UP click (4px threshold), SetCapture lifecycle + WM_CAPTURECHANGED cancel
  (review amendment), chrome+toolbar rebound to internal state, suppressed-pick
  mirror, Overlay_GetSelectedIdForTest hook, OWNSEL harness stage.
- own-selection-model → validated clean rebuild, full suite + OWNSEL PASS → 488e886.
  Clicking a bar now selects THE TASK in our model; chrome/toolbar internally driven
  (SyncSelectionChromeFromOwnSelection each tick from the snapshot; cleared if id
  vanishes); Esc-cancel of a gesture polled via GetAsyncKeyState in Tick (NOACTIVATE
  window gets no WM_KEYDOWN). All remaining units touch Overlay.cpp → lane fully
  serial from here.
- self-review #3 dispatched (3 units since last checkpoint) over the selection-lane
  diffs + the drag-move-resize / drag-row-and-create / rebuild-in-place specs; key
  questions: g_mutating vs a separate gesture flag, selection loss after rebuild
  (drag would feel broken), lParam-vs-GetCursorPos in drag handling (PostMessage
  testability), drag↔rebuild-in-place ordering contradiction. Holding drag dispatch
  until folded.
- self-review #3 result — NOT safe as specced; folded: (A1) new g_gestureActive flag
  (g_mutating makes Tick a no-op → ghost would never paint) + REMOVE the stale
  mouseUp/ReflowFromSlide block (~1545: pre-V2 leftover, now runs COM reflow on idle
  ticks after any click-select); (A2) synchronous SetOwnSelection after RebuildChart
  (COM selectId re-select gets suppressed+mirrored a tick later → visible deselect
  after every drag); (A3) PP_PROJ parser only in GanttBuilder.cpp:384 — expose or
  duplicate; (A4) WM_MOUSEMOVE lParam=client coords, g_dragActive latch not endpoint
  math, WM_CAPTURECHANGED idempotent full-reset, Esc ≤150ms by design. Clean:
  capture lifecycle, mirror race-free, wheel forwarding (minor GetParent nit),
  no capture leaks. Same SetOwnSelection rule pre-noted on rebuild-in-place.
- cycle 6 — dispatched drag-move-resize (sonnet) with amendments.
- drag-move-resize → validated clean rebuild, full suite + DRAG PASS → 9fbb001.
  Modal drag engine: ghost + date tooltip in PaintOverlay, day snapping from
  snapshot px-per-day (COM-free moves), NudgeTask/SetTaskDates(new op) commits,
  ParseProj extracted to GanttBuilder.h. Agent found+fixed 2 real bugs:
  (a) ReleaseCapture delivers WM_CAPTURECHANGED SYNCHRONOUSLY → snapshot gesture
  state before releasing (else drags never commit); (b) SyncSelectionChrome right
  after RebuildChart hits the invalidated snapshot and clears selection → set
  own-selection only, let next Tick resync (toolbar-path convention). Stale Tick
  auto-reflow block removed per review #3. Harness covers TaskBody drag only;
  edge/milestone paths share commit logic but are uncovered (noted).
- cycle 7 — dispatched drag-row-and-create (sonnet): vertical row retarget in
  body drags + phantom-bar create on EmptyCell; stages DRAGROW then CREATE (CREATE
  last; exit-on-first-fail makes CREATE PASS prove both; coordinator checks both).
- drag-row-and-create → validated clean rebuild; all 7 stages PASS (both DRAGROW and
  CREATE markers verified) + full suite → 65e87da. Vertical row retarget in body
  drags (RowBandAtScreenY per move, MoveTaskToRow+NudgeTask in one rebuild);
  phantom-bar create on EmptyCell (<0.5 day = click). Agent found+fixed the SAME
  WM_CAPTURECHANGED-synchronous-reset bug class in its new path (Create branch read
  live g_dragPxPerDay after ReleaseCapture zeroed it → every create misclassified
  as click) — third occurrence of this pattern; noted as a standing trap for any
  future WM_LBUTTONUP logic: SNAPSHOT ALL gesture state before ReleaseCapture.
- cycle 8 — dispatched rebuild-in-place (sonnet): StartNewUndoEntry per gesture
  (GROUPING_WORKS), diff-based UpdateGantt (move/resize by PP_ID, add/remove deltas
  only, no regroup on pure moves), InsertGantt kept as fallback, INPLACE harness
  stage asserts same chart-root COM identity + stable child count.
- rebuild-in-place → validated clean rebuild; all 8 stages PASS (incl. INPLACE:
  same chart-root COM identity, stable child count) + full suite → 1f7fef3.
  UpdateGantt reconciles by (PP_KIND,PP_ID,ordinal): pure move/resize = property
  writes only (no regroup); structural = ungroup/delete/add/regroup/retag;
  InsertGantt fallback on failure. StartNewUndoEntry per gesture at RebuildChart +
  MutateChart choke points (GROUPING_WORKS). Agent fixed 4 real bugs: untagged prims
  got stable ids (row/month/milestone); Ungroup() invalidates pre-ungroup Shape
  pointers (re-derive from post-ungroup range); TextFrame AutoSize fought explicit
  heights (ppAutoSizeNone); harness capture race (real WM_MOUSEMOVE interleaves
  with posted ones → SetCursorPos in lockstep). Residual: structural reconcile of
  SUMMARY/BRACKET/DEP kinds exercised only incidentally; registered add-in runs a
  second overlay instance during harness runs (environmental, pre-existing).
- self-review #4 dispatched (3 units since checkpoint; UpdateGantt rewrote the
  mutation path all remaining units sit on) over the three drag/rebuild diffs +
  the 4 remaining specs (floating-editor, overlay-context-menu,
  keyboard-and-cursors, dpi-and-monitors) incl. harness-contamination questions
  and dispatch-order recommendation. Holding dispatch until folded.
- self-review #4 result — landed drag/rebuild code verified sound (snapshot-before-
  ReleaseCapture trap closed; Esc-mid-commit unreachable via g_mutating guard; COM
  lifetimes clean; drag math drift-free). ONE High: partial failure after Ungroup()
  orphans loose ex-group shapes + duplicate chart on InsertGantt fallback → NEW unit
  fix-reconcile-robustness (also: exclude empty-PP_KIND children from diff; blanket
  try/catch net around OverlayWndProc). All four remaining units cleared with
  amendments folded: floating-editor + keyboard + ctx-menu harness stages must
  re-resolve LIVE shape rects/ids (earlier stages mutate the document — INPLACE
  pattern); keyboard harness re-selects explicitly; keys-probe.txt pre-flight OK
  (VERDICT HOTKEY). Recommended dispatch order: fix-reconcile → dpi → ctx-menu →
  keyboard → floating-editor (most harness-fragile last). 15 units total.
- cycle 9 — dispatched fix-reconcile-robustness (sonnet).
- fix-reconcile-robustness → validated clean rebuild; all 8 stages + full suite →
  ed5c5c7. Post-ungroup section now try/caught with cleanupLoose() (deletes
  ex-group shapes + this attempt's rendered prims before E_FAIL → fallback starts
  clean); untagged (empty PP_KIND) children excluded from diff, always survive into
  regroup; OverlayWndProc wrapped in blanket try/catch → DefWindowProc.
- cycle 10 — dispatched dpi-and-monitors (sonnet): pure HtScalePx helper +
  scaled edge band in GanttHitTest, all chrome constants DPI-scaled per tick,
  PMv2 awareness in harness exes, DPI HELPER OK marker, manual matrix appended
  to plan §4.2 U10.
- dpi-and-monitors → validated clean rebuild; OPS+DPI HELPER OK, all 8 stages,
  full suite → bbb2330. Pure HtScalePx (round-half-away) + HtSnapshot.edgeBandPx;
  ~25 chrome metrics DPI-scaled (badge/toolbar/grip/handles/tooltip/fonts/strokes/
  drag threshold); DPI probed per ShowOverlayForChartRect, change → invalidate
  snapshot + repaint; PMv2 awareness (dynamic resolve) in all 3 harness exes;
  manual 100-200% matrix appended to plan §4.2.1 (non-gating).
- cycle 11 — dispatched overlay-context-menu (sonnet): pure zone→menu→op model in
  GanttHitTest (MENU MAP OK gate), TrackPopupMenu on NOACTIVATE via
  SetForegroundWindow+WM_NULL idiom, commands through the standard undo+ops+
  UpdateGantt commit path.
- overlay-context-menu → validated clean rebuild; OPS+DPI+MENU MAP OK, all 8
  stages, full suite → 99e6044. Pure zone→menu→op model (MapMenuCommand validates
  against BuildMenuForZone's own table — can't drift); WM_RBUTTONDOWN selects,
  UP shows TrackPopupMenuEx (TPM_RETURNCMD, SetForegroundWindow+WM_NULL idiom);
  commands via standard undo+ops+UpdateGantt path; PP_OVERLAY_NO_MENU env guard
  for manual smoke. Flicker of the NOACTIVATE popup idiom NOT yet observed
  (modal menu unautomatable) — ask user during visual pass.
- cycle 12 — dispatched keyboard-and-cursors (sonnet): WM_SETCURSOR zone cursors
  (pure CURSOR MAP OK), HOTKEY-verdict keyboard (scoped RegisterHotKey on
  selection+foreground transitions, Delete/arrows/Shift-arrows), Esc clears
  selection via tick poll; KEYS harness stage posts WM_HOTKEY (handler-correctness;
  OS delivery already probe-proven).
- keyboard-and-cursors → validated clean rebuild; OPS+DPI+MENU+CURSOR MAP OK, all
  9 stages incl. KEYS PASS, full suite → ec6fa7c. Pure GanttCursorForZone; scoped
  RegisterHotKey per HOTKEY verdict (transition-edge register/unregister, per-key
  failure tolerated, unregister in HideOverlay AND OverlayStop before
  DestroyWindow); Delete/±1/±7 via standard commit path; Esc clears selection
  (tick poll). KEYS stage posts WM_HOTKEY (handler correctness; OS delivery
  probe-proven). Note: keys stolen system-wide only while a task/milestone is
  selected AND PowerPoint foreground — mitigation is the tight gate.
- cycle 13 — dispatched floating-editor (sonnet, FINAL unit): card editor on the
  focusable-top-level pattern (label/start/end/percent/8 swatches, one commit =
  one undo), SetTaskColor + absolute percent ops, EDITOR harness stage with live
  target resolution (t4 deleted by KEYS).
- floating-editor → validated; ALL 10 stages PASS + full suite from clean rebuild →
  29afc9b. Card editor (label/start/end/percent/8 swatches) on the focusable
  top-level pattern; strict ISO date validation w/ red tint + beep; card
  focus-loss cancels (inline commits — deliberate divergence); mutual exclusion
  inline↔card; edit session suppresses hotkeys+gestures; SetTaskColor +
  SetTaskPercentValue ops. Noted: milestone recolor op not requested → task-only.

## V2 completion summary (15/15 green)

Full gate suite from a clean rebuild: [build] OK · OPS HARNESS OK + DPI HELPER OK +
MENU MAP OK + CURSOR MAP OK · 1/1 fixtures · ppoverlay 10/10 stages (ALPHA, CAPTURE,
SUPPRESS(child-select unsimulatable), OWNSEL, DRAG, DRAGROW, CREATE, INPLACE, KEYS,
EDITOR) · REFLOW PASS.

Shipped (each gated, [todo: id]): 40c43e9 alpha-overlay · 41eca8e capture-surface ·
c2d49bb selection-suppression · 4243863 fix-capture-hardening · 488e886
own-selection-model · 9fbb001 drag-move-resize · 65e87da drag-row-and-create ·
29e209d disco-undo-entry · 543dc86 disco-keyboard-focus · 1f7fef3 rebuild-in-place ·
ed5c5c7 fix-reconcile-robustness · bbb2330 dpi-and-monitors · 99e6044
overlay-context-menu · ec6fa7c keyboard-and-cursors · 29afc9b floating-editor.

Self-reviews: pre-dispatch (11 findings), #2 post-capture (2 new units + amendments),
#3 pre-drag (2 mandatory amendments, stale-reflow removal), #4 post-rebuild (1 High
fix unit + live-rect harness rule + dispatch order). Sub-agents ran on Sonnet from
cycle 3 onward (user token directive).

DELIVERED — V2 pure on-slide editor: chart shapes are render-target only. Overlay
captures all chart input (Alt/grip = whole-group escape); internal semantic
selection with own chrome; drag move/resize with ghost + date tooltip + day
snapping; vertical row reassign; drag-to-create phantom bar; right-click semantic
menu; Delete/arrow hotkeys (scoped); zone cursors; floating card editor
(label/dates/percent/color); diff-based in-place rebuild, one gesture = one undo
entry; per-pixel-alpha chrome; DPI-scaled at 100-200%; crash-hardened (WndProc net,
GDI+ status gates, reconcile failure cleanup).

USER NOTES for the visual pass: restart PowerPoint to load the latest DLL. Watch
for: (a) TrackPopupMenu activation flicker on right-click (unautomatable — report
back); (b) hotkey theft is scoped to selection+foreground but system-wide while
active; (c) manual DPI matrix in plan §4.2.1 awaits your run. Known limits:
SUPPRESS child-select path unverifiable by automation (Selection Pane is the only
real user path); milestone recolor not implemented (op exists pattern, small add);
edge/milestone drag paths share commit logic but only body-drag is harness-covered.

---

## V3 — Row-Centric Editing + Bottom App Bar (started 2026-07-04)

User feedback session distilled into `docs/on-slide-ux-plan.md` §6. Decisions
(user, 2026-07-04): app bar primary + right-click kept (ONE shared command map) ·
uniform rows with optional one-level hierarchy (keep `groupId`) · generic movable
markers (Today = default label) · fit-to-slide reserves a top ~15% title zone.

Backlog seeded (12 units, same SQLite DB): fit-to-slide · marker-model-ops →
marker-drag · text-model → text-interaction · label-placement · row-uniform-ux ·
grid-scale-options · appbar-shell → appbar-actions → { dependency-ux,
context-menu-v3 }. Gates per unit in the DB; full V2 suite is the regression
floor for every unit. Same protocol as V2: Sonnet sub-agents, coordinator
validates gates from clean rebuilds, PowerPoint mutex, commit before done,
self-review pre-dispatch and every ~3 units.

Standing V2 lessons that apply verbatim: hardcoded bat source lists (new .cpp →
every bat); snapshot gesture state before ReleaseCapture; time-bounded harness
pumps; SetOwnSelection-only after RebuildChart; SetCursorPos lockstep in harness.

### V3 pre-dispatch review + user feedback fold (2026-07-04)

Pre-dispatch Sonnet review of the 12-unit backlog: 6 actionable findings, all
folded into the DB: (1) DeleteById does NOT handle markers (claim corrected to
definitive); (2) context-menu-v3 gate was VACUOUS (MENU MAP OK already prints) →
new distinct marker MENU MAP V3 OK; (3) AddMilestone op does not exist → made a
definitive sub-task; (5/6) no harness hook exposes widget rects or selection KIND
(Overlay_GetSelectedIdForTest returns id only) → appbar-shell adds
OverlayAppBarButtonRectForTest, text-interaction adds Overlay_GetSelectedKindForTest;
(9) fit-to-slide FIT assertion must force rc=1 on failure (reflow rc pattern);
(10) SetGridStyleLine→SetGridStyle naming reconciled. Coordinator note (finding 8):
label-placement / grid-scale-options / text-model all edit GanttBuilder emission —
serialize them (serial dispatch is the default anyway).

Second user feedback wave folded as units 13-14 + a rewrite:
- fix-overlay-scoping (V3-13, URGENT user bug): overlay chrome visible over other
  apps (CurseForge) — TOPMOST overlay never checks foreground/iconic. Dispatch FIRST.
  Includes the harness trap: after the fix the harness itself must foreground
  PowerPoint or every posted-mouse stage dies.
- material-theme (V3-14): Material 3 tonal palette in new pure GanttTheme.h, shared
  by shape emitter AND overlay chrome; slide-export PNG artifact for coordinator
  visual inspection.
- grid-scale-options REWRITTEN: verified doc.scale does not affect rendering today
  (header always months; SetScale already accepts all five values; ribbon handlers
  exist). Now: two-tier axis per scale, gridDensity override (auto/.../none),
  gridStyle solid|dotted, separator cap ~150, deliberate fixture regen.

appbar-shell amended: obeys foreground scoping, painted from GanttTheme.h, Grid
cycle + five-scale control; deps now include V3-13 + V3-14. Backlog: 14 pending.

### Review pass 2 (new/rewritten units) folded (2026-07-04)

11 findings on fix-overlay-scoping / material-theme / grid-scale-options, all
folded: material-theme gate was VACUOUS (REFLOW PASS pre-exists) -> new marker
THEME PNG OK with unconditional Slide.Export + on-disk verification + rc=1 on
failure; MaterialLight()/Theme live in Scene.h NOT GanttBuilder.cpp (Scene.h
added to paths, CTX corrected in all 3 units); fixture-regen instruction DELETED
- spec/fixtures/basic-chart.expected.json is a layout-only cross-implementation
contract with the web TS engine (conformance.cpp never serializes separators;
fixture removed from all allowed_paths, "fix the code, never the fixture");
grid-scale-options gate extended to FULL suite (axis rewrite churns harness-
exercised shape population); editor semantics corrected (inline COMMITS on
kill-focus, card CANCELS on WA_INACTIVE, both auto-fire - scoping only
hide-if-still-visible + NULL checks); pid trap documented (compare fg pid vs
g_pptHwnd owner pid, NOT GetCurrentProcessId - Esc idiom is production-only);
SCOPE stage uses exported OverlayHwnd(), editor/card hidden-OR-nonexistent;
foreground-steal helper prints FOREGROUND STEAL FAILED + nonzero on refusal;
deps added: material-theme <- {grid-scale-options, fix-overlay-scoping}.
Verified-good claims recorded: harness already foregrounds PowerPoint at start
and in SUPPRESS; Slide.Export already used argv-gated in reflow-test.cpp.

DISPATCH ORDER: fix-overlay-scoping (user bug) -> fit-to-slide ->
marker-model-ops -> marker-drag -> text-model -> text-interaction ->
label-placement -> row-uniform-ux -> grid-scale-options -> material-theme ->
appbar-shell -> appbar-actions -> dependency-ux -> context-menu-v3.
