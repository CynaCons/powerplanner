# On-Slide Editor V4 ÔÇö Implementation Plan (mockup-driven vertical slices)

> **For the implementing agent.** You are building the native C++ PowerPoint
> add-in under `native/` up to the quality of the approved interactive mockup.
> Read, in this order, before writing any code:
> 1. `docs/onslide-experience-spec.md` ÔÇö WHAT to build (requirements R0ÔÇôR8)
> 2. `docs/design-tokens.md` ÔÇö the exact colors/dimensions/type (normative)
> 3. `docs/mockup/onslide-mockup.html` ÔÇö open in a browser; this is the target
> 4. ┬º1 Ground rules below ÔÇö hard-won lessons; violating any of them has
>    already caused a failed unit in this repo. They are not suggestions.
>
> Work slice by slice (S1ÔåÆS6), strictly in order. A slice is DONE only when
> every one of its acceptance criteria (AC) passes **from a clean rebuild**,
> you have committed it, ticked its checkboxes here, and produced its PNG.
> **STOP at the end of each slice** and wait for the user's visual review
> before starting the next.

> **Agent feedback tooling:** `native/tools/harness_driver.py` + scenarios +
> bridge give structured post-unit feedback on S3+ UX (run from repo root;
> scenarios enforce `expected_markers`; goldens fail when missing unless
> `--update-goldens`). It AUGMENTS the gates ÔÇö a unit's AC markers from a
> clean rebuild remain the only acceptance authority. See
> docs/native-agent-feedback-loop-plan.md.

## 0. Architecture you are building on (all shipped and green)

Chart = grouped native shapes tagged `PP_KIND`/`PP_ID` under a `CHART_ROOT`
group; document JSON in `PP_DOC` tag; x-projection in `PP_PROJ` tag
(`{minDay,pad,ptPerDay,originX}` ÔÇö meaning is frozen, see G8). Pure layers:
`GanttModel.h` (PpDocument: rows/tasks/milestones/brackets/deps/markers/texts),
`GanttJson.cpp` (round-trip), `GanttLayout.cpp` (abstract layout),
`GanttBuilder.cpp` (scene emission + `UpdateGantt` frame-preserving diff
rebuild + `FitChartRootToSlide/Frame`), `GanttOps.cpp` (pure mutations),
`GanttHitTest.cpp` (pure hit zones + menu model + cursor map), `Scene.h`
(prims + `Theme`/`MaterialLight()`). `Overlay.cpp` owns a layered
`WS_EX_NOACTIVATE`+`TOPMOST` per-pixel-alpha window over the chart: 150 ms
`Tick()` poller, `SetCapture` drags (4px threshold), internal selection
(`g_ownSelKind`/`g_ownSelId`), scoped `RegisterHotKey` keyboard, floating
card/inline editors, right-click `TrackPopupMenu`, foreground scoping via
`IsHostActiveForOverlayChrome`. Existing ops: AddRow/AddTask/DeleteById/
NudgeTask/SetTaskDates/SetTaskPercent/SetTaskColor/MoveTaskToRow/SetScale
(all 5 values)/AddMarker/SetMarkerDate/SetMarkerLabel/AddText/SetTextLabel/
MoveText/SetEntityLabel.

## 1. Ground rules (every one has a failed-unit story behind it)

1. **Build scripts have HARDCODED source lists.** A new `.cpp` must be added to
   every `.bat` that links that module: `native\build.bat`, `build-ops.bat`,
   `build-conformance.bat`, `build-overlay.bat`, `build-reflow.bat`. Prefer
   header-only additions. MSVC `/MT`; no PCH with `#import`.
2. **Exceptions never escape WndProc/Tick**: catch `_com_error`,
   `std::exception`, and `...` at every entry point you add.
3. **One gesture/command = one undo entry** (`StartNewUndoEntry` via the
   existing RebuildChart path). After `RebuildChart`, call ONLY
   `SetOwnSelection` synchronously ÔÇö never resync chrome from a pre-rebuild
   snapshot.
4. **Snapshot ALL gesture state into locals BEFORE `ReleaseCapture()`** ÔÇö
   `WM_CAPTURECHANGED` is delivered synchronously and clears gesture globals.
5. **The test harness is input-neutral and offscreen.** `overlay-test.cpp`
   poisons `SetCursorPos/SetForegroundWindow/keybd_event/SendInput` into
   compile errors. New stages: posted `WM_MOUSE*` + `SetOverlayCursorOverride`
   + `Overlay_SetHostActiveOverrideForTest`, modeled on the DRAG/MARKERDRAG
   stages. Bound every message pump by TIME, never queue-empty. PowerPoint is
   moved outside the virtual screen ÔÇö never reposition it back.
6. **`spec/fixtures/*.json` is a cross-implementation contract** with the web
   TS engine. NEVER modify it. If conformance fails you broke geometry: fix
   the code. Never add elements to `MakeSampleDocument()` (fixtures + every
   harness stage depend on its exact shape population).
7. **`PP_PROJ` fields keep their exact meaning** (pure linear day mapping).
   New data = new fields.
8. **`UpdateGantt` is frame-preserving** (captures + restores CHART_ROOT frame
   via `FitChartRootToFrame`). Nothing you add may break `FITPERSIST OK`.
9. **Any new top-level window** (the app bar) must: be `WS_EX_NOACTIVATE |
   WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED`, paint via
   `UpdateLayeredWindow` + GDI+ (init exists in OverlayStart), take its
   visibility from the SAME `IsHostActiveForOverlayChrome` decision in the
   SAME Tick block as the chart overlay, be destroyed in OverlayStop/HideOverlay,
   and be covered by the SCOPE harness stage.
10. **All COM gates kill PowerPoint before/after** (`taskkill /f /im
    POWERPNT.EXE`). Never leave a POWERPNT process behind.
11. **DPI**: any new pixel constant goes through `HtScalePx`.
12. **Tokens only** (G6): every color/size in emitters and chrome comes from
    `GanttTheme.h`. If you type a hex literal outside GanttTheme.h, stop.
13. **Commit protocol**: one commit per numbered unit, message
    `onslide(<unit-id>): <summary> [todo: <unit-id>]`, tick the unit's
    checkbox in THIS file in the same commit. Never commit with a failing
    gate; never skip hooks; leave the tree clean.
14. **Report honestly**: paste the gate tail (last ~20 lines) in your report.
    If a criterion is not met, say so ÔÇö do not reinterpret it.

## 2. The gate suite (run from repo root; exit 0 required)

```
GATE-PURE : cmd /c native\build.bat && cmd /c native\build-ops.bat && cmd /c native\build-conformance.bat
GATE-FULL : taskkill /f /im POWERPNT.EXE >nul 2>&1 &
            cmd /c native\build.bat && cmd /c native\build-ops.bat && cmd /c native\build-conformance.bat &&
            cmd /c native\build-overlay.bat && cmd /c native\build-reflow.bat &&
            native\build\ppoverlay.exe && native\build\ppreflow.exe
            & taskkill /f /im POWERPNT.EXE >nul 2>&1
```

**Regression floor ÔÇö these must print on every GATE-FULL, before and after
your work**: ops `OPS HARNESS OK ┬À DPI HELPER OK ┬À MENU MAP OK ┬À CURSOR MAP OK
┬À MARKER OPS OK ┬À TEXT OPS OK`; conformance `1/1 fixtures passed`; ppoverlay
stages `ALPHA CAPTURE SUPPRESS OWNSEL DRAG DRAGROW CREATE INPLACE KEYS EDITOR
SCOPE MARKERDRAG TEXTELEM` all PASS + `INPUT NEUTRAL OK`; ppreflow `FIT OK ┬À
FITPERSIST OK ┬À REFLOW PASS`. Validate from a clean rebuild:
`Remove-Item native\build\*.obj,native\build\*.exe,native\build\*.dll` first.
Delete any stray `overlay.png` (gitignored) afterwards.

**The visual gate (new, built in S1.1):** `ppreflow.exe` additionally always
exports `native/build/visual-<slice>.png` (1280├ù720, `Slide.Export`; delete
stale file first, verify on disk after, else print `VISUAL FAIL` and rc=1) and
prints `VISUAL <SLICE> OK`. The PNG is compared BY EYE against
`docs/mockup/onslide-mockup.html` ÔÇö attach it to your report; the user is the
final judge at each slice boundary.

## 3. Slices

Numbering: unit id `s<slice>-<name>`; AC = acceptance criterion (all must
hold). ACs marked ÔÜÖ are programmatic (must appear in gate output); Ô£ï are
coordinator/user visual checks on the exported PNG or live PowerPoint.

---

### S1 ÔÇö The Look: theme, rail label column, hierarchical header
*Spec: R2 (rendering), R3, R4 (visual), tokens ┬º1ÔÇô5. This slice makes a
freshly inserted chart resemble the mockup.*

- [x] **s1-theme-tokens** ÔÇö Create `native/PowerPlannerAddin/GanttTheme.h`
  (header-only): every token from `docs/design-tokens.md` ┬º1ÔÇô5 as named
  constants (BGR helpers where needed) + `BlendOnWhite(rgb, alpha)`. Re-point
  `Scene.h`'s `Theme`/`MaterialLight()` fields and ALL hardcoded colors in
  `GanttBuilder.cpp` at GanttTheme constants. Wire the stored-but-ignored
  color fields: task/milestone/bracket/marker/text `color` (empty ÔçÆ token
  default; else parse `#RRGGBB`). Bar rendering switches to track =
  `BlendOnWhite(swatch, 0.40)` + solid progress overlay + radius `bar.radius`.
  - AC1 ÔÜÖ ops-test asserts (new checks, marker `THEME TOKENS OK`): scene prims
    for a doc with a colored task carry that exact fill; empty color ÔçÆ
    `swatch1`; track color equals the blend formula; GATE-PURE exit 0.
  - AC2 ÔÜÖ conformance still `1/1` (geometry untouched).
  - AC3 ÔÜÖ GATE-FULL floor green (INPLACE tolerates the restyle ÔÇö if it
    diffs on shape-count, the emitter changed structure: fix the emitter,
    not the stage).
- [x] **s1-visual-gate** ÔÇö In `reflow-test.cpp`: unconditional
  `Slide.Export` ÔåÆ `native/build/visual-s1.png` (delete stale ÔåÆ export ÔåÆ
  verify on disk ÔåÆ `VISUAL S1 OK`, else `VISUAL FAIL` + rc=1). Pattern for the
  call exists argv-gated at ~line 100. Also add shape-property assertions
  (read back via COM): a task bar's fill RGB == expected blend; milestone fill
  == `ink`; today marker line == `primary` ÔÇö marker `SHAPE PROPS OK`.
  - AC1 ÔÜÖ `VISUAL S1 OK` + `SHAPE PROPS OK` + `FIT OK` + `FITPERSIST OK` +
    `REFLOW PASS`, exit 0, PNG freshly on disk.
- [x] **s1-rail-labels** ÔÇö `labelPlacement` per task (`bar|rail|both`, empty =
  `bar`; JSON round-trip backward compatible) + global doc flag `railLabels`
  (all-rail override). Layout/emitter: rail-placed tasks emit NO on-bar label;
  instead a rail dot (8pt, task swatch, radius 3, tagged `RAIL_DOT`/taskId) +
  rail label (`type.railTask`, tagged `RAIL_TASKLBL`/taskId) at the task's
  lane; on-bar % suppressed when bar < ~110pt. Rail width per tokens ┬º4
  (default byte-identical when no rail labels ÔÇö conformance safety). Ops:
  `SetLabelPlacement(doc, taskId, v)` + `SetRailLabelsGlobal(doc, bool)`.
  - AC1 ÔÜÖ ops-test: round-trip, ops validation, layout assertions (rail label
    rect inside rail at the task's lane; on-bar label absent when rail) ÔÇö
    marker `LABEL OPS OK`; GATE-PURE exit 0.
  - AC2 ÔÜÖ conformance `1/1` (sample doc untouched).
  - AC3 ÔÜÖ GATE-FULL floor green.
- [x] **s1-hier-axis** ÔÇö Two-band hierarchical header per spec R3 table +
  `gridDensity`/`gridStyle` model fields (backward-compatible JSON) + ops
  `SetGridDensity`/`SetGridStyle` + separator emission with stable ISO tag ids
  + ~150-separator cap with density fallback. Band heights/typography per
  tokens. GanttHitTest scale submenu gains Quarter/Year ids.
  - AC1 ÔÜÖ ops-test: per-scale band/label/separator assertions (week ÔçÆ Monday
    separators + months band; day ÔçÆ auto-thinned day numbers; quarter ÔçÆ
    "Q2 2026" cells; density override; `none` ÔçÆ no ticks, bands intact; cap
    fallback) ÔÇö marker `GRID OPS OK`; GATE-PURE exit 0.
  - AC2 ÔÜÖ `PP_PROJ` unchanged in meaning: `FITPERSIST OK`, `REFLOW PASS`,
    `MARKERDRAG PASS` all still green (they consume it).
  - AC3 ÔÜÖ GATE-FULL floor green; `VISUAL S1 OK` regenerated.
  - AC4 Ô£ï visual-s1.png vs mockup: rail column with dots+labels for rail
    tasks, two-band header with week day-numbers, Material bars with blended
    tracks, correct palette. **User reviews before S2.**

### S2 ÔÇö App bar shell + global commands
*Spec: R8 (window, anatomy, globals), tokens ┬º4ÔÇô6. Ground rule 9 mandatory.*

- [x] **s2-appbar-model** ÔÇö Pure module `GanttAppBar.h` (HEADER-ONLY if at all
  possible ÔÇö else update every bat): `AppBarItem {int cmd; std::string label;
  int icon; bool enabled; bool danger;}`, `BuildAppBar(selKind, doc, selId)
  ÔåÆ groups` implementing the R8 table exactly (task rows get the Row group;
  Unlink enabled iff deps touch the task). Command ids extend the existing
  menu-command id space in `GanttHitTest.h` (ONE id space).
  - AC1 ÔÜÖ ops-test asserts the full R8 table (every context: exact labels,
    order, enabled flags) ÔÇö marker `APPBAR MODEL OK`; GATE-PURE exit 0.
- [x] **s2-appbar-window** ÔÇö The window per ground rule 9: docked
  bottom-center (8px up, Ôëñ94% width, `appbar.height` @DPI), painted per
  tokens (container, group hairlines, group labels, buttons, hover via
  WM_MOUSEMOVE, segmented scale control with active chip, swatch row);
  rebuild items on selection change + Tick. Clicks ÔåÆ dispatch the pre-existing
  command ids through the existing menu executor; NEW ids (row ops, labels,
  grid, link, unlink, note, marker mgmt) may temporarily no-op ÔÇö S3ÔÇôS5 wire
  them. Test hook `OverlayAppBarButtonRectForTest(cmd, RECT*)` exported next
  to the existing `_ForTest` hooks.
  - AC1 ÔÜÖ overlay-test stage `APPBAR` (after TEXTELEM): bar window exists +
    visible with chart; click Scale-W button via its rect hook (posted mouse,
    input-neutral) ÔçÆ `doc.scale == "week"` in PP_DOC ÔçÆ `APPBAR PASS`.
  - AC2 ÔÜÖ SCOPE stage extended: app bar hides/reappears with the overlay
    (override 0/1) ÔÇö SCOPE PASS still printed.
  - AC3 ÔÜÖ GATE-FULL floor green (13 stages + APPBAR).
  - AC4 Ô£ï screenshot of live PowerPoint (user or coordinator): bar matches
    mockup anatomy at 100% and 150% DPI; no focus stealing while clicking
    (type in the title placeholder afterwards still works). **User reviews.**

### S3 ÔÇö Rows are objects
*Spec: R2 behavior B2.1ÔÇôB2.6.*

- [x] **s3-row-ops** ÔÇö Pure ops: `AddRowAbove/Below(doc, rowId)` (level of the
  reference row), `MoveRowUp/Down(doc, rowId)` (flat adjacent swap),
  `IndentRow`/`OutdentRow` (+ B2.4 normalization; guards: one level max, row
  with children can't indent), `DeleteRow` cascade (tasks, milestones,
  anchored texts, touched deps). Marker `ROW OPS OK` with assertions incl.
  every guard + normalization + cascade.
  - AC1 ÔÜÖ `ROW OPS OK`; GATE-PURE exit 0.
- [x] **s3-row-selection** ÔÇö Row as a selection kind end-to-end: rail hit
  zones (row-name area vs rail task-label area ÔÇö B2.1/B2.2), rail highlight
  rendering (tokens ┬º6) on the overlay, app-bar Row context + Row group live
  (dispatching s3-row-ops through the shared registry), Delete hotkey scoped
  for ROW, quick-add `+` chip on rail hover (B2.5), Rename via card editor.
  - AC1 ÔÜÖ overlay-test stage `ROWSEL`: click a rail row name ÔçÆ
    `Overlay_GetSelectedKindForTest()==row`; click app-bar "Below" via rect
    hook ÔçÆ rows.size()+1 in PP_DOC; click "Ôåô" ÔçÆ order swapped; Delete key ÔçÆ
    row gone + cascade held (its task absent) ÔçÆ `ROWSEL PASS`.
  - AC2 ÔÜÖ GATE-FULL floor green; one undo entry per op, asserted INSIDE the
    ROWSEL stage itself (INPLACE-style undo count around one representative
    row op) so it is machine-enforced, not a report claim.
  - AC3 Ô£ï live check: select/reorder feels like the mockup. **User reviews.**

### S4 ÔÇö Task & marker context complete
*Spec: R4 (B4.1, B4.2), R5. Closes the marker-delete gap.*

- [x] **s4-task-context** ÔÇö Wire remaining task-context commands through the
  registry: swatches (`SetTaskColor` ÔÇö visible on bar AND rail dot), ÔêÆ1d/+1d,
  `Label: bar/rail` cycle (`SetLabelPlacement`), global Labels toggle
  (`SetRailLabelsGlobal`), Note (anchored `AddText` at default offset), Edit
  (card editor), Delete. Milestone context likewise (nudges, Note, Delete).
  - AC1 ÔÜÖ overlay-test stage `TASKCTX`: select task ÔåÆ click swatch3 rect ÔçÆ
    task.color == `#7A4FA3` in PP_DOC AND bar fill reads back as its blend;
    click `+1d` ÔçÆ dates +1; click Label ÔçÆ placement flipped; ÔçÆ `TASKCTX PASS`.
  - AC2 ÔÜÖ GATE-FULL floor green.
- [x] **s4-marker-mgmt** ÔÇö Markers become full selection citizens: MARKER in
  `HandleHotkeyDelete` + Delete-scope registration, app-bar marker context
  (Rename via card editor, ┬▒1d = `SetMarkerDate`, Delete = `DeleteById`),
  "Marker" insert on background context (custom marker at visible center).
  - AC1 ÔÜÖ overlay-test stage `MARKERMGMT`: insert marker via app bar ÔçÆ
    markers.size()+1; select it, Delete key ÔçÆ gone ÔçÆ `MARKERMGMT PASS`.
  - AC2 ÔÜÖ GATE-FULL floor green. AC3 Ô£ï **User reviews S4.**

### S5 ÔÇö Dependencies + note entry points
*Spec: R6 (entry points), R7.*

- [x] **s5-dep-ops** ÔÇö Pure ops `AddDependency(doc, from, to, type="finish-to-start")`
  (reject self/dup/missing; from/to may be task or milestone ids) +
  `RemoveDependenciesTouching(doc, id)`. Marker `DEP OPS OK`.
  - AC1 ÔÜÖ `DEP OPS OK`; GATE-PURE exit 0.
- [x] **s5-link-mode** ÔÇö B7.1/B7.2 exactly: Link button ÔåÆ link mode
  (crosshair over bars, hint pill per tokens ┬º6 above the app bar), click
  target ÔçÆ AddDependency + exit + one undo; Esc/background/selection-loss
  cancels; Unlink button. Note buttons live (task/milestone context anchored;
  background context free at center).
  - AC1 ÔÜÖ overlay-test stage `DEP`: select task A ÔåÆ Link (rect hook) ÔåÆ click
    task B's bar ÔçÆ dep AÔåÆB `finish-to-start` in PP_DOC + connector shape
    present; Esc path asserted (enter link, Esc, click B ÔçÆ NO new dep) ÔçÆ
    `DEP PASS`.
  - AC2 ÔÜÖ GATE-FULL floor green. AC3 Ô£ï **User reviews S5.**

### S6 ÔÇö Context menus from the registry + final sweep
*Spec: B8.3. The V3 vacuous-gate lesson applies: the old `MENU MAP OK` prints
today without any new work ÔÇö your gate is the NEW marker.*

- [x] **s6-menus** ÔÇö Rebuild `BuildMenuForZone`/`MapMenuCommand` to derive
  from the shared registry: task/row/marker/text/milestone zones mirror their
  app-bar contexts; empty cell adds "Add task here / Add milestone here
  (`AddMilestone` op: CREATE if missing, mirroring AddTask) / Add note here";
  background = Insert + Scale + Grid submenus. Keep the
  SetForegroundWindow+TrackPopupMenu idiom.
  - AC1 ÔÜÖ ops-test: menu model for EVERY zone matches the registry (no dead
    ids, no divergent labels) ÔÇö NEW marker `MENU MAP V4 OK`; GATE-PURE exit 0.
    **Anti-tautology rule**: because builder and test would otherwise share
    the registry, the test must ALSO pin literal expected labels+ids for at
    least the task zone and background zone as hardcoded strings (e.g.
    "Delete row" / `HtCmd_DeleteRow`) so a registry-wide regression cannot
    self-validate.
  - AC2 ÔÜÖ GATE-FULL floor green (menus are covered purely; no new stage).
- [x] **s6-final** ÔÇö Full-suite validation from clean rebuild; export
  `visual-s6.png`; update `PLAN.md` N9 checkboxes; write a completion summary
  in `docs/on-slide-coordinator-log.md` listing every commit hash.
  - AC1 ÔÜÖ GATE-FULL exit 0 with the complete marker set: floor + `THEME
    TOKENS OK ┬À SHAPE PROPS OK ┬À LABEL OPS OK ┬À GRID OPS OK ┬À APPBAR MODEL OK
    ┬À ROW OPS OK ┬À DEP OPS OK ┬À MENU MAP V4 OK` + stages `APPBAR ┬À ROWSEL ┬À
    TASKCTX ┬À MARKERMGMT ┬À DEP` + `VISUAL S1/S6 OK`.
  - AC2 Ô£ï Final user visual pass against the mockup, DPI 100/150%,
    slideshow-mode spot check (chrome must not paint over a running show).

## 4. Risks and their mandated mitigations

| Risk | Mitigation (mandatory) |
|---|---|
| Axis rewrite churns shape population | Stable ISO tag ids; run GATE-FULL not just GATE-PURE for s1-hier-axis |
| Second window fights the first for capture | App bar lies OUTSIDE the chart rect; if rects ever overlap, chart wins (NCHITTEST domains disjoint) |
| Theme touches everything | s1-theme-tokens changes COLORS ONLY ÔÇö any geometry diff in conformance/INPLACE is a defect in your change |
| New stages flake | Follow DRAG/MARKERDRAG pattern verbatim; time-bounded pumps; deselect (background click) before clicking near chrome; run ppoverlay twice green before reporting a slice |
| Registry refactor breaks old menus | `MENU MAP OK` (old) must keep printing until s6 replaces its checks with `MENU MAP V4 OK` |
| Card editor focus semantics | Inline editor commits on kill-focus; card editor CANCELS on WA_INACTIVE; hide-if-still-visible with NULL checks only |
