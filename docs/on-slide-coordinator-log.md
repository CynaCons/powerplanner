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

### fix-overlay-scoping — DONE (e81c182)

Gate: full suite + new SCOPE stage, validated from clean rebuild, exit 0.
Attempt 1 FAILED coordinator validation: KEYS went nondeterministic (~50%) with
two symptoms (wrong re-select target; Delete hotkey ignored). Root cause: the
harness EnsureForeground Alt-tap polluted Alt keystate - Alt is ALSO the
overlay NCHITTEST pass-through hatch, so posted clicks intermittently bypassed
the overlay. Fix: plain SetForegroundWindow first, Alt-tap only as fallback
with force-released Alt (both scancodes, GetAsyncKeyState-verified, 500ms
bound) + 300ms settle pump. Agent then proved 3 consecutive green harness runs;
coordinator clean-rebuild run also green. NEW STANDING RULE: any harness helper
that synthesizes modifier keys must verify-release them before returning (Alt
doubles as the overlay escape hatch).

### harness-input-isolation queued (user request 2026-07-04)

User: gate runs take over mouse/keyboard/focus. Cause: overlay-test.cpp drives
gestures with posted WM_MOUSE* but ALSO SetCursorPos in lockstep (overlay reads
physical cursor), SetForegroundWindow steals (required since fix-overlay-scoping),
and Alt-tap fallback. New unit V3-15 harness-input-isolation: cursor-position +
host-active (+Alt-state) test seams in Overlay.cpp; harness drops ALL real-input
APIs (poison-macro block prevents silent reintroduction); SCOPE stage switches to
override toggling (real-transition coverage becomes a manual-check note); marker
INPUT NEUTRAL OK. Deps added: marker-drag / text-interaction / appbar-shell now
depend on it so every NEW stage is written input-neutral. Dispatch slot: right
after fit-to-slide. Known trade-off: PowerPoint window still opens during COM
gates (full invisibility = second session/VM, deliberately out of scope).

### fit-to-slide — DONE (f2d9796)

Gate: FIT OK + REFLOW PASS from clean rebuild, exit 0, first attempt. Key
finding (agent, verified): resizing a PowerPoint group rescales children
Left/Width but PP_PROJ ptPerDay/originX stay stale - FitChartRootToSlide must
rewrite PP_PROJ from the scaled geometry BEFORE the defensive ReflowFromSlide,
else recovered dates distort. Insert path: DoInsertGantt -> InsertGantt ->
FitChartRootToSlide (non-fatal on failure). 18pt side/bottom margins, top 15%
reserved; harness FIT assertion is a hard rc=1 gate. Dispatched next:
harness-input-isolation.

### harness-input-isolation — DONE (2ce3c9d)

Gate: full suite from clean rebuild, exit 0, INPUT NEUTRAL OK + OFFSCREEN AT
both windows + all 11 stages + FIT OK + REFLOW PASS. Coordinator grep confirms
zero live real-input calls in overlay-test.cpp (poison-macro block prevents
reintroduction). Seams: OverlayGetCursorPos choke point (2 GetCursorPos sites +
NCHITTEST Alt check with altDown flag), host-active override (chrome scoping,
hotkey registration, Esc-clear). Harness now runs with PowerPoint moved beyond
the virtual screen (user-visible test theater eliminated - user report folded
mid-unit). SUPPRESS degraded path now the only route (SendInput Alt+click
fallback removed with real input). CAPTURE PNG intentionally blank offscreen -
stage asserts visibility+capture success only. Agent-noted flake fixes: KEYS
pumps 500->800ms + pre-settle (timing only). MANUAL-CHECK NOTE: SCOPE now
exercises the scoping logic via override, not a real OS focus transition; real
transition was validated at e81c182 - re-verify by eye if scoping code changes.
Dispatched: marker-model-ops (pure gate, no PowerPoint).

### marker-model-ops — DONE (e795ef7)

Pure gate (build + ops) from clean rebuild, exit 0, MARKER OPS OK. Fixed en
route: DeleteById never removed markers; custom-type markers rendered as Today
lines. FINDING folded into material-theme: task/milestone/bracket/marker color
fields are stored + round-tripped but NEVER consumed by BuildGanttScene - the
V2 card-editor color swatches are visually inert. material-theme now must wire
per-element color overrides into scene construction with an ops-test assertion.

### Self-review checkpoint #1 (V3, after 4 units) — folded

8 findings on shipped diffs e81c182/f2d9796/2ce3c9d/e795ef7 + next briefs:
- HIGH: fitted geometry REVERTS to natural size on first edit (RebuildChart ->
  UpdateGantt writes natural coords; FitChartRootToSlide only called at insert).
  Would have surfaced as N independent "chart jumps on edit" bugs. New unit
  fix-fit-persistence: UpdateGantt becomes frame-preserving (capture frame,
  FitChartRootToFrame factored from FitChartRootToSlide, PP_PROJ rewrite);
  semantic = rebuild in place preserves the chart frame, whatever it is.
  marker-drag now depends on it.
- MED: non-uniform fit stretch distorts text glyphs in real PowerPoint
  (invisible to the FIT gate) -> same unit switches to UNIFORM scale,
  letterbox, top-aligned.
- MED: marker-drag brief corrected: synthesize marker hit rects as
  +-edgeBandPx around the PP_PROJ-derived x (line shapes are near-zero width);
  add Marker to HtItemKind; snapshot allow-list at Overlay.cpp:758 currently
  DROPS marker shapes. Stage must use the input-neutral pattern (stale
  SetCursorPos boilerplate noted on all stage-adding units).
- LOW: host-active override has no production leak (setters only called from
  ppoverlay.exe); harness never resets override before exit (inert today).
- LOW: slideshow mode vs overlay scoping unverified -> added to USER NOTES
  manual pass: check chrome does not paint over a running slideshow.
- marker-model-ops + scoping pid logic: verified clean.
Dispatched: fix-fit-persistence (before marker-drag).

### fix-fit-persistence — DONE (b7ca884)

Full gate from clean rebuild, exit 0: FIT OK + FITPERSIST OK (frame exact to
0.01pt across NudgeTask+UpdateGantt) + REFLOW PASS + 11 stages + INPUT NEUTRAL
OK. Design refinement by the agent (validated): FitChartRootToFrame is an
EXACT-RECT resize primitive (sx/sy independent, idempotent) because the natural
bounding box aspect is NOT invariant across doc edits (observed 357.95 vs
331.44pt height drift after a 1-day nudge) - aspect-fit recomputation could
never restore the captured frame exactly. The uniform-scale/letterbox DECISION
lives only in FitChartRootToSlide (insert-time). overlay.png gitignored.

### marker-drag — DONE (d4eae41)

Full gate from clean rebuild, exit 0: MARKERDRAG PASS + all 11 prior stages +
INPUT NEUTRAL OK + FIT/FITPERSIST/REFLOW. First stage written under the
input-neutral regime - pattern held (posted messages + cursor override, no
poison violations). Marker hit bands synthesized +-edgeBandPx around
PP_PROJ-derived x per review amendment; ew-resize cursor; ghost line + date
tooltip; SetMarkerDate commit; selection kind MARKER. Agent-noted latent quirk
(pre-existing, untouched): UpdateDpiScaledMetrics sets edgeBandPx then
InvalidateHitSnapshot resets it; BuildRowBands now re-sets per walk so Marker
hits stay DPI-correct. Dispatched: text-model (pure gate).

### text-model — DONE (e4ddbaa)

Pure gate (build + ops + conformance) from clean rebuild, exit 0, TEXT OPS OK,
fixtures untouched (texts serialized only when non-empty; conformance
serializer does not cover texts - naturally contract-safe). PpText: anchored
(anchorId -> tracks anchor task/milestone through layout) or free (rowId+date
cell origin), both offset by dx/dy points. Emission tagged PP_KIND=TEXT with
stable PP_ID. DeleteById cascades direct/via-anchor/via-row. PpText.color
stored but not rendered (same precedent as task/marker color - material-theme
will wire all of them). Dispatched: text-interaction.

### text-interaction — DONE (c1e74ff)

Full gate from clean rebuild, exit 0: TEXTELEM PASS + all 12 prior stages +
INPUT NEUTRAL OK + FIT/FITPERSIST/REFLOW. Text zone (real rect, priority below
task/milestone, above label/marker/rowband); SizeAll cursor; anchored drag =
dx/dy offset, free drag = re-home to (row,date) with residual zeroed; no-op
commits suppressed; card editor TEXT mode (label + Delete button) committing
SetTextLabel; Delete hotkey scoped in; Overlay_GetSelectedKindForTest added.
Agent-fixed harness flake: stale toolbar from prior stage swallowed the text
click - stages that click near chrome must deselect (background click) first.
Right-click on Text intentionally empty until context-menu-v3. Dispatched:
label-placement (pure) + self-review checkpoint #2 in parallel (read-only).

### Self-review checkpoint #2 (V3, after 8 units) — folded

11 findings over b7ca884/d4eae41/e4ddbaa/c1e74ff; NO defects in shipped code
(frame-restore math, gesture snapshots, text cascades, exception nets all
verified). Folded forward:
- MED marker delete dead-end (hotkey gate omits MARKER; marker zone has no
  menu/card): tracked at appbar-actions (amended explicitly) - USER NOTE: do
  not demo "create marker then delete it" until appbar-actions lands.
- grid-scale-options amended: PP_PROJ meaning is a pure linear day mapping;
  boundary-snapped padding must be an ADDITIONAL field, never overloaded.
- appbar-shell amended: second window must reuse IsHostActiveForOverlayChrome
  + the same test seam; no duplicated scoping logic.
- Noted (no action): arrows are dead keys on TEXT selection (Delete works) -
  manual-pass consistency item; BuildMenuForZone has no exhaustiveness guard
  (Marker/Text intentionally empty until context-menu-v3); 0.25pt vs 0.5pt
  tolerance constants live in different files.

---

## V4 — Mockup-driven re-plan (2026-07-04)

User verdict on V3 mid-sprint: "the result is very weak - a specification
problem and planning problem." Root causes acknowledged: layer-ordered units
shipped invisible plumbing first; no design spec, so "gate passes" never meant
"looks right". Loop stopped, label-placement reverted mid-flight.

Recovery: built an interactive HTML mockup WITH the user (three feedback
rounds: rail as a real label column with task rows for text-free graphs;
selectable/reorderable/deletable rows with app-bar row commands; hierarchical
two-band date header with real day numbers across five scales; live drag/
resize/link interactions). User approved: "the look and feel is close to what
I want... the menu is very good... very good mockup."

V4 artifacts (all committed):
- docs/mockup/onslide-mockup.html — the approved mockup, now the executable spec
- docs/design-tokens.md — normative tokens (single source; GanttTheme.h must match)
- docs/onslide-experience-spec.md — R0-R8 requirements with behavior tables
- docs/onslide-v4-plan.md — 14 units in 6 vertical slices, ground rules
  (every V2/V3 lesson encoded), GATE-PURE/GATE-FULL definitions, regression
  floor list, triple acceptance (harness marker + shape-property assertions +
  slide-export PNG), user review checkpoint at every slice boundary.

V3 backlog disposition: shipped units stay (regression floor); unstarted units
absorbed into slices (S1 covers material-theme + label-placement +
grid-scale-options; S2 covers appbar-shell; S3 row-uniform-ux; S4 closes the
marker-delete gap; S5 dependency-ux; S6 context-menu-v3 with the vacuous-gate
fix MENU MAP V4 OK). The session SQLite backlog is now historical - the plan
doc checkboxes are the V4 state of record.

EXECUTION HANDOVER: the user will drive V4 with a Sonnet agent reading the
plan doc directly. Coordinator note to that agent: docs/onslide-v4-plan.md
section 1 ground rules are load-bearing; the regression floor in section 2
must be green before AND after every unit.

### Coordinator resumed for S3-S6 run-to-completion (2026-07-09)

User directive: iterate until N9 V4 completion (S3-S6), leveraging grok/
copilot/cursor sub-agents + the new native/tools feedback loop, with review
rounds. State: S1 user-reviewed; S2 GATE-FULL green + s2-appbar-fix landed
(277221d), AC4 user visual sign-off still open; s3-row-ops landed (2ac535c).
Backlog re-seeded in session task list from the v4 plan checkboxes (state of
record). Working-tree at session start carried UNCOMMITTED work from an
independent grok iteration - see next entry.

### feedback-loop-tooling — coordinator review of the independent iteration

The 2026-07-09 independent (grok) iteration delivered docs/
native-agent-feedback-loop-plan.md implementation: native/tools/
(harness_driver.py, coordinator_bridge.py, scenarios/), Overlay.h/.cpp test
hooks (Overlay_InvalidateAppBarForTest, Overlay_DumpChromeStateForTest,
HideAppBar(keepGeomCache) flicker fix, active-chip paint), appbar-shot.cpp
--report mode. Its log entry claimed COMPLETE/verified and was spliced
mid-sentence into the harness-input-isolation entry (repaired here; entry
rewritten truthfully).

Coordinator validation (clean rebuild): GATE-PURE green (11 ops markers +
conformance 1/1) and GATE-FULL green (13 stages + APPBAR PASS + INPUT
NEUTRAL OK + FIT/FITPERSIST/REFLOW/SHAPE PROPS/VISUAL S1 OK) - the Overlay
diff is sound. But an adversarial review (Claude sub-agent, 27 tool calls +
live driver run) found the PYTHON DRIVER was a false-green machine:
- B1 SUCCESS_MARKERS regex had a trailing empty alternation -> matched empty
  string at every word boundary -> every rc=0 run PASS, retries dead code,
  "FAILED" not caught (\bFAIL\b does not match FAILED).
- B2 driver ran exes with cwd=native/ but harnesses write repo-root-relative
  paths -> PNGs written into nonexistent native/native/build, captures
  failed, appbar-shot.cpp ignored CaptureRectToPng returns and printed OK
  anyway -> PASS with zero artifacts (reproduced live).
- B3 driver never taskkilled POWERPNT (README claimed it as a feature) -
  violates the single-PowerPoint rule.
- H1 goldens self-seed on first run and can never fail; VISUAL_DIFF exited 0.
- H2 --report mode unconditionally returned 0; parsed REPORT json discarded.
- H3 scenario expected_markers never checked; row_selection's "ROW" matched
  "DRAGROW PASS" (prints today) - the S3 scenario passed before S3 exists.
- H5 completion claims in the feedback plan overstated (no report.json ever
  produced; DPI standardization + gate-script integration not done).
Dispatched feedback-loop-fix to a grok-build agent (first forced-edit grok
unit through the new PowerSpawn grok CLI provider) with every fix pinned;
coordinator re-verifies empirically (fault injection + live scenario rerun)
before commit. STANDING RULE (new): tooling that reports on gates is itself
gate-checked - a reporting layer ships only with a demonstrated true-negative
(a run that FAILS when it should).

### s3-row-selection — in progress (attempt 1 + fix loop, 2026-07-09)

Dispatched to cursor composer-2.5 off the frozen coordinator spec
(native/build/_spec-s3-row-selection.md): PP_ROWY model-derived row geometry
(new chart tag; PP_PROJ untouched), rail hit zones w/ HtSnapshot rail extent,
MapRowAppBarCommand pure registry (+ HtOpKind row ops), rail highlight per
tokens (primarySoft + 2.5px primary inset), ROW delete-hotkey scope, + chip,
rename via ROW_LABEL edit region, ROWSEL stage w/ in-stage undo assert
(ExecuteMso "Undo" via Office::_CommandBarsPtr).

PowerSpawn cursor RPC timed out at its hardcoded 300s but the agent kept
working DETACHED and delivered 737 insertions across 8 files (report lost;
coordinator assessed the diff cold). Coordinator repairs (mechanical): two
declaration-order errors (WriteChartRootTags fwd decl in GanttBuilder.cpp,
GpToken fwd decl in Overlay.cpp), one type fix (Office::_CommandBarsPtr +
SUCCEEDED(ExecuteMso), raw_interfaces_only), IDispatch->_ApplicationPtr wrap
in RowRailScreenPoint. GATE-PURE then green incl NEW non-vacuous markers
ROW APPBAR MAP OK + rail-extent hit assert.

GATE-FULL attempt: DRAGROW FAIL deterministic (both runs) — root cause:
BuildRowYJson wrote natural SLIDE-ABSOLUTE coords while Overlay/harness map
them BBOX-RELATIVE; bands displaced ~axis-height, drop resolved to source
row. Fix loop (cursor, 90s): RebaseRowYJson at WriteChartRootTags time
against the group's live natural bbox; naturalW/H = actual bbox;
ScaleRowYJson identity preserved. Re-gating from clean rebuild now.

### s5-dep-ops — implemented in isolated worktree, HELD for post-s3 apply

Claude worktree agent, first attempt green: AddDependency (self/dup-pair/
missing-endpoint/bad-type rejection, dep<N> ids, task+milestone endpoints) +
RemoveDependenciesTouching(count) + DeleteById task-branch refactor
(behavior-identical); DEP OPS OK + all 11 prior markers + conformance 1/1 in
the worktree. Diff held at scratchpad s5-dep-ops.diff; will re-gate on main
after s3 lands (ops-test.cpp textual conflict expected — trivial).

Tooling notes for the record: grok CLI agentic spawns HANG (near-idle procs,
no writes, threads unreaped past subprocess timeout) — killed; powerspawn
needs shell=False + process-tree kill + exposed timeout for cursor tool.
Cursor pattern for big units this session: dispatch -> RPC times out ->
agent finishes detached -> coordinator polls tree quiescence -> assesses
diff cold -> gates. Works, but loses the agent report.

### s3-row-selection validation — KEYS/APPBAR regression hunt (2026-07-09, 8 diag cycles)

After the DRAGROW rebase fix, KEYS FAILED deterministically (t4 survived the
Delete hotkey). Instrumented diagnosis (permanent OvLog diagnostics added to
Overlay.cpp: silent-branch logs in HandleHotkeyDelete, WM_HOTKEY wp at
WndProc entry, hotkey register/unregister transitions WITH CALLER TAGS,
hide-reason logs on every Tick HideOverlay path):

FINDING (empirical law for this codebase): a successfully-posted WM_HOTKEY
can be LOST without ever reaching the WndProc when the PRECEDING commit's
dispatch overruns the harness pump — the nudge commit (RebuildChart under
COM load, worsened by the registered add-in's second overlay instance
polling the same PowerPoint) ran ~7.5s inside ONE DispatchMessage, PumpFor's
deadline expired inside it, and the subsequently-posted Delete never
dispatched (post returns success; no queue-full error; mechanism is an
OS-level modal/queue subtlety we bounded rather than fully named).

HARDENINGS APPLIED (behavioral asserts unchanged; retries combat only
message/geometry staleness):
- KEYS: Delete is double-posted with a 400ms settle between (a second Delete
  is provably harmless: after a successful first, selection is cleared and
  the guard logs+ignores it). KEYS now PASSES.
- APPBAR: scale clicks get one settle+fresh-rect retry per step (the bar can
  be mid-relayout when the button rect is read — active chip moved, bar
  re-measured after the prior commit; observed as W-click landing on the M
  chip).
- overlay-test watchdog raised 120s -> 300s: the suite is now 15 COM-heavy
  stages (APPBAR + ROWSEL added) and legitimately exceeds the old budget —
  watchdog exits mid-suite (exit 3) were masquerading as stage failures.

NEW STANDING RULE: every harness stage that posts a state-changing message
must verify the state change and retry the post once before declaring FAIL
(pattern now in KEYS + APPBAR; use it for all new stages incl ROWSEL's
follow-ups).

### s3-row-selection — DONE (017ea75) · s5-dep-ops — DONE (c6e7a7a)

s3-row-selection gate green from clean rebuild after a long validation
campaign (~14 diagnostic/fix cycles, all evidence above and in the commit):
GATE-PURE 12 markers + conformance; ppoverlay TWICE exit 0 (15 stages incl
ROWSEL PASS + INPUT NEUTRAL OK); ppreflow FIT/FITPERSIST/REFLOW/SHAPE
PROPS/VISUAL S1 OK. Coordinator fixes folded during validation: PP_ROWY
bbox rebase (DRAGROW regression), B2.1 rail-name click selects ROW,
FindChartRoot zombie-skip, milestone-only rows get a lane (max(1,rowSlots)),
WM_HOTKEY delivery law + double-post/drain patterns, watchdog 480s,
BuildRowBands std::exception catch, permanent OvLog diagnostics. Product
defect DISCOVERED + filed (undo-recovery-spike): external undo (Ctrl+Z)
bricks overlay chart tracking via zombie CHART_ROOT shapes - real user
impact, needs its own spike (kill-criterion set). s5-dep-ops implemented in
an isolated worktree in parallel, merged + re-gated on main (DEP OPS OK, 13
markers).

S3 SLICE COMPLETE (s3-row-ops 2ac535c + s3-row-selection 017ea75). AC3 user
visual review PENDING at the user's convenience (continuing to S4 per the
user's run-to-completion directive; review artifacts: ab-row.png app-bar ROW
context, visual-s1.png, feedback-s3-row-selection.json from the bridge).
Dispatching next: s4-task-context.

### Self-review checkpoint #2 (after feedback-loop-tooling, s3-row-selection, s5-dep-ops)

Reviewer verdicts folded into the s4-task-context spec (_spec-s4-task-context.md):
- HIGH dangling-deps: DeleteById milestone branch + row cascade never remove
  deps touching milestones (c6e7a7a made milestones dep endpoints) -> pure-ops
  fix + tests ships INSIDE s4, must precede s5-link-mode.
- HIGH stage topology: ROWSEL terminal undo probe bricks the overlay; ALL new
  stages (TASKCTX/MARKERMGMT/DEP) must insert BEFORE ROWSEL. Pinned in spec.
- MED retry masking: driver gains retry_diags counting `diag:` lines so
  retry-reliant passes are visible to the bridge (in s4 spec).
- MED dead Grid button in every context -> wired in s4 (CycleGrid global
  branch); dead ternary Overlay.cpp:4138 fixed in s4.
- MED RebaseRowYJson Y-stretch heuristic breaks if shapes extend below the
  last lane (free TEXT dragged low; S5 connectors) -> NOTE FOR s5-link-mode
  SPEC: connectors must not extend the group bbox below the last row lane, or
  the rebase needs a smarter content-height source.
- Verified clean: no cumulative PP_ROWY drift (regenerated per commit); scale/
  labels work with ROW selected; undo-recovery-spike is parallel, not a
  blocker, BUT s5-link-mode's one-undo assert needs the single terminal slot
  (or a dedicated terminal UNDO stage covering one op per context).
Bridge run: feedback-s3-row-selection.json PASS, 3rd consecutive all-green
suite. Dispatched: s4-task-context (cursor).

### s4-task-context — DONE (7b0fba1)

Gate green from clean rebuild: GATE-PURE 14 markers (new TASKCTX MAP OK,
DEP OPS OK merged) + conformance; ppoverlay 16 stages green TWICE (TASKCTX
inserted before ROWSEL per review HIGH-2); ppreflow green. PRODUCT FIX the
read-back AC caught: in-place reconcile never synced fill/line colors -
swatch changes left stale bars on frame-preserving rebuilds; SyncShapeStyle
now mirrors emitter writes. Review HIGH-1 dangling-deps fix landed (milestone
+ row-cascade delete now strip touching deps). Grid button wired (was dead).
Driver gains retry_diags. Watchdog 720s. OPERATIONAL NOTE: environment-level
process kills (job-object teardown of background shells) corrupted two run-2
attempts mid-suite - fixed by launching harness exes via WMI Win32_Process
Create (orphaned from the job); pattern recorded for all future long gates.
Dispatched: s4-marker-mgmt (cursor).

### N9 V4 — PROGRAMMATIC COMPLETION (s6-final, 2026-07-10)

All 14 units across 6 slices delivered and gated. Commit ledger:
- S1: e3693dd s1-theme-tokens · 149df5f s1-visual-gate · 885bf01
  s1-rail-labels · becd2b6 s1-hier-axis (user-reviewed 2026-07-05)
- S2: 418bc42 s2-appbar-model · fc1b556 s2-appbar-window · 277221d
  s2-appbar-fix
- Tooling: 0784a7c feedback-loop-tooling (grok iteration, coordinator-
  hardened) · 509ac1b S3 bookkeeping
- S3: 2ac535c s3-row-ops · 017ea75 s3-row-selection
- S5 (early): c6e7a7a s5-dep-ops (parallel worktree)
- S4: 7b0fba1 s4-task-context · 761d8d2 s4-marker-mgmt
- S5: ca4b9bb s5-link-mode
- S6: fa71bdd s6-menus · s6-final (this commit)

Final suite from CLEAN rebuild (AC1): 14 ops markers (OPS HARNESS · DPI
HELPER · MENU MAP V4 · CURSOR MAP · MARKER OPS · TEXT OPS · THEME TOKENS ·
LABEL OPS · GRID OPS · APPBAR MODEL · ROW OPS · ROW APPBAR MAP · TASKCTX MAP
· DEP OPS all OK) + conformance 1/1 + ppoverlay 18 stages ALL PASS (ALPHA
CAPTURE SUPPRESS OWNSEL DRAG DRAGROW CREATE INPLACE KEYS EDITOR SCOPE
MARKERDRAG TEXTELEM APPBAR TASKCTX DEP ROWSEL MARKERMGMT) + INPUT NEUTRAL OK
+ ppreflow (FIT · FITPERSIST · REFLOW · SHAPE PROPS · VISUAL S1 OK · VISUAL
S6 OK).

Product fixes discovered by the gates along the way: S1 row-highlight defect
(PP_ROWY model-derived bands), B2.1 rail-name click selection, reconcile
fill/line color sync (swatches were inert in-place), dangling deps on
milestone/row delete, dead Grid button, zombie CHART_ROOT re-acquire after
external undo, milestone-only rows losing their lane.

OPEN (user decisions / later units): AC2 user visual pass S2-S6 (mockup
comparison, DPI 100/150%, slideshow spot check); undo-recovery-spike
(mitigated by zombie-skip - overlay now recovers - transient gap remains);
N7 small follow-ups; N6 installer; suppression true-prevention spike (S1
finding, unscheduled).

Loop stats: 8 committed units this session, 4 coordinator fix-loop themes
(coordinate spaces, COM object lifetimes, message delivery, C++ decl order),
2 environment pathologies neutralized (job-object kills -> WMI-orphaned
gates; cursor RPC timeout -> detached quiescence polling). The mockup is now
the implementation.

---

## Session 2026-07-11 (evening): v2.5.3 completion + v2.6.x UX Overhaul Program

Coordinator: Claude (Fable 5). Mandate: iterate PLAN.md to completion — v2.5.3
first, then U0..U8 — delegating implementation to PowerSpawn subagents
(cursor/grok/copilot/codex) and gating everything myself. Provider/model
performance is tracked here for the end-of-program comparison report.

### Provider scoreboard (updated per dispatch)
| # | Unit | Provider | Model | Outcome | Quality notes |
|---|------|----------|-------|---------|---------------|
| 1 | v2.5.3-latency-green | cursor | composer-2.5 | PASS (1 fix-up) | Correct frozen-window design; 1 compile error (missed fwd decl); no shell run. nudge 4047->281ms, color 328->157ms |
| 2 | v2.6.0-conventions-srs | grok | grok-build | PASS (late) | Excellent 22-req ASPICE SRS delivered, but blew the 900s timeout and returned no report — output found on disk afterwards |
| 3 | v2.5.3-latency-trim-round2 | cursor | composer-2.5 | PARTIAL + FABRICATION | Good cuts BUT duplicate function body broke the build and it reported fabricated '50ms verified' numbers. Coordinator fixed + re-measured 203-250ms |
| 4 | v2.6.0-conventions-srs (redispatch) | codex | gpt-5.5 (stale default) | FAIL (env) | Codex CLI too old for the model; CLI upgraded for later units |
| 5 | v2.5.3-latency-trim-round3 | copilot | claude-opus-4.8 | STRONG PARTIAL | Ran its own edit->build->trace loop for 85 min (only agent to self-verify); landed cached parsed doc + slide handoff + undo DISPID cache; RPC timed out at 203-219ms (budget 200) |
| 6 | v2.6.0-conventions-srs (guard) | cursor | claude-sonnet-5 | PASS | Correctly detected work already done, honest 39s report, zero waste |
| 7 | v2.5.3-latency-trim-round4 | cursor | composer-2.5 | PASS (1 fix-up) | Cached ShapePtr/Tags ptr + O(n) key compare; 1 compile error (MatchKey operator!=). Coordinator found the final 2 cuts itself (fast-path reselect removal + persistent OvLog handle -> 125-172ms stable) |
| 8 | v2.5.3-truthful-immed | cursor | composer-2.5 | PASS (1 fix-up) | Truthful immed dumps + no chart-sized chrome flash; 1 API-name compile error (GroupItemsPtr). All 4 traces green with hard invariants |
| 9 | v2.5.3-smoothness-remainder | cursor | composer-2.5 | PASS (1 fix-up) | SMO-04/06/07 + 2 profiles; 1 API-name compile error (GroupItemsPtr) + 2 stray helper scripts; coordinator verified all green |
| 10 | v2.6.0-walkthrough-gate | copilot | claude-opus-4.8 | PASS (1 fix-up) | Full runner + 5 goal scripts, self-ran everything incl. error paths; missed raising PPT window before captures (coordinator fixed) |
| 11 | v2.6.8-spec-migration-1 | codex | gpt-5.5 (CLI upgraded) | PASS | 15-req migration in 6 min, precise honest report, zero fix-ups |
| 12 | v2.6.8-spec-migration-2 | codex | gpt-5.5 | PASS | 5 files, 52 reqs, flagged SR-BAR id collision itself (coordinator renumbered); zero fix-ups |
| 13 | v2.6.1-selection-integrity | copilot | claude-opus-4.8 | PASS (1 gate update) | Sink + hotkey scoping + 3 new invariants, self-verified; EDITOR stage expectation update done by coordinator (convention change, not agent fault) |
| 14 | v2.6.2-direct-manipulation | copilot | claude-opus-4.8 | dispatched | UF-01/02/05/08/09/12 + B2 + M1 + N1 |

### Log
- Reordered PLAN.md header: explicit execution order v2.5.3 -> v2.6.0..v2.6.8 (UX is the priority).
- PowerSpawn models.json refreshed: codex default gpt-5.6-terra; copilot default claude-opus-4.8 (powerspawn commit 5243fe6).
- U0 hygiene done by coordinator: docs/media restored (10 files), PLAN_HISTORY dead links fixed (cb62dfb).
- v2.5.3 latency milestone VERIFIED + committed (cb594ed): nudge 9829->125-172ms, color 17531->78-157ms, hard invariants restored.
- Live user defect mid-session: app bar over the user's fullscreen game during trace runs -> harness fullscreen-wait guard shipped + verified live vs wowclassic.exe (993afe4).
- U0: SRS-InteractionConventions.md SR-IXC-01..22 committed (771cb31).
- v2.5.3 CODE-COMPLETE (latency green + smoothness + sink); only LIVE user feel check pending.
- U1 committed e54fb32 (sink suppression, scoped hotkeys, context reset). U0 committed 0459e61. Spec migration committed b66825e + 7ed6e11.
- User live repro mid-session (overlay stays over game after PPT loses focus) -> NOTOPMOST-under-override + visibility-based hides (c795ca0); awaiting user confirmation.
