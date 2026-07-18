# PowerPlanner Implementation Plan

## Quick Summary

**Current Version:** v2.7.3 (COMPLETE 2026-07-12) — time-window W0–W3 shipped
**Active product polish:** v2.7.4 (W4 settings + cold walkthrough + user feel gate)
**Active iteration:** **v2.10.0 — Session recorder (R0/R1)** — see Phase 15; v2.9.0 harness-green but **LIVE VERIFY FAILED 2026-07-18** (reopened as v2.9.1, blocked on recorder)
**Quality program:** Phase 13 / v2.8.x **COMPLETE 2026-07-17** (continuous-feel baseline intentional RED; UG-01..05 user gates open)

### Key Metrics
- **Web foundation:** complete (v0–v2)
- **Native on-slide:** production-grade UX program v2.5–v2.6 complete (harness-green); time window v2.7.0–v2.7.3 complete
- **Focus:** v2.7.4 product polish (v2.9.0 task-bar unit shipped)
- **Test harness:** `python native/tools/harness_driver.py` — scenarios, operation traces, invariants, walkthroughs, golden compare (see `native/tools/`)

### Recent Achievements (compressed)
- ✅ v2.5.0–v2.5.3 — overlay lifecycle, calm chrome, creation flows, ≤200 ms single-op latency (nudge/color)
- ✅ v2.6.0–v2.6.8 — UX overhaul (selection integrity, direct manip, docking, multi-select, linking, theme surfaces, scale settings, walkthrough gate)
- ✅ v2.7.0–v2.7.3 — explicit time window (ports, two-phase drag, lossless clip, undo, commit budget)
- ✅ Phase 13 / v2.8.x quality infrastructure COMPLETE 2026-07-17
- ⚠️ v2.9.0 — task bar unit: harness-green, but live verify 2026-07-18 FAILED (native label-child grips; silent drag-create death) — reopened as v2.9.1/v2.9.2
- ✅ Ribbon hotfix 2026-07-15 — customUI apostrophe broke PowerPlanner tab

### Next Up (EXECUTION ORDER)
1. **Phase 15 / v2.10.x — Session recorder** (user directive 2026-07-18: agents need live-session visibility; docs/session-recorder-spec.md)
2. **v2.9.1 / v2.9.2** — live task-unit selection + live create-commit repair (first customers of the recorder)
3. **v2.7.4** — time-window W4 (settings UI, walkthrough, user feel)
4. **User gates UG-01…UG-05** — live PowerPoint sign-off (not agent-markable)
5. **Product backlog** — cloud, Excel link, installer, etc.

### Test Status
Native: `python native/tools/harness_driver.py scenario <name>` · `trace <profile> --check-invariants` · `walkthrough all`
Web: `npm test` · `npm run dev` (port 5180)

### Quick Links
- [Product Requirements](PRD.md)
- [README](README.md)
- [Specification (concept layer)](spec/README.md)
- [Native SRS](spec/srs-native/README.md)
- [Native add-in notes](docs/native-addin.md)
- Implementation History: `git log`
- [Time window design](docs/time-window-plan.md)

---

## 📝 Format Rules

**Format Rules:**
- Iterations: Version number + brief title
- Goal: One-line objective statement (optional)
- Status: Complete/In Progress (only if in progress)
- Tasks: Simple checkbox items only (no sub-bullets, no implementation details)
- NO "Files Modified" sections (use git for that)
- NO "Impact" sections (tasks describe the work)
- NO "Key Accomplishments" or verbose summaries
- Known Issues: Brief bullet points only
- Close iterations chronologically - don't skip ahead
- Move incomplete tasks to future iterations, don't leave them in closed ones

---

# Phase 9: Native PowerPoint Add-In (C++ COM)

### v2.3.2 - Native Core Infrastructure (COMPLETE)
**Goal:** Loadable add-in, native shape emission from spec, data round-trip, basic overlay

- [x] ATL COM DLL, ribbon, registration
- [x] GanttLayout + emitter to native shapes under CHART_ROOT with tags
- [x] GanttJson round-trip + PP_DOC embedding
- [x] Layered overlay + 150ms polling + basic selection chrome
- [x] Reflow, inverse projection
- [x] V1 contextual editing (menus, hover insert, inline, mini-toolbar)
- [x] V2 pure on-slide (full input capture, suppression, drag/create/edit)

### v2.3.3 - Row-Centric + App Bar (COMPLETE)
**Goal:** Rows as first-class, context app bar

- [x] Row ops, selection, reorder
- [x] App bar shell + model

### v2.3.4 - On-Slide V4 Slices S1–S6 (COMPLETE)
**Goal:** Mockup-driven on-slide editor (visuals, app bar, rows, context, deps, menus)

- [x] S1–S6 gated (harness markers + conformance + visual) 2026-07-10

### v2.4.0 - UI Operation Trace & State Continuity Monitoring (COMPLETE)
**Goal:** Build "before / immediate after / delayed" observation into the native feedback loop so transient UI problems during operations are automatically detected

- [x] Extend C++ harness entry points and overlay-test to capture DumpChromeStateForTest at explicit trace points (pre-op, post-mutation, post-RebuildChart, +1 tick, +3 ticks)
- [x] Add screenshot capture at the same trace points using existing CaptureRectToPng
- [x] Enhance native/tools/harness_driver.py with trace() / run_operation_trace() that returns sequenced reports (state JSON + artifact paths per step)
- [x] Define core continuity rules/invariants (rowBands count/geometry stable or improving, appBarVisible stays true, ownSelKind does not drop to container/empty during item-specific op, no large empty highlight state mid-flow)
- [x] Create operation profiles for reported issues (row select + "New row below", rename while row selected, scale while row selected)
- [x] Add trace golden support (sequence of key state fields + visual hashes) and comparison in driver
- [x] Proof-test: run the new traces against the exact user-reported flows and confirm the system would have flagged the content flash and wrong scale context
- [x] Wire into onslide-coordinator acceptance gates for any new UI units
- [x] Document the monitoring strategy in docs/native-agent-feedback-loop-plan.md

### v2.4.1 - Native On-Slide UI Polish from Live Feedback (COMPLETE)
**Goal:** Fix issues observed during live operations in PowerPoint (transient states, selection context, flashes, wrong chrome)

- [x] Scope scale controls (bottom bar D/W/M/Q/Y) to appear only when the overall component is selected, never when a row is selected  (verified via trace: scale rule now passes)
- [x] Resolve conflicting overall component highlight and outline ("PowerPlanner" style large fill/rect) that fights user selection visuals  (badge now only when ownSel empty)
- [x] Make row selection reliable (user can click to select a row as first-class object; hover/highlight already functional)  (added hover-based fallback in ApplyClickSelection)
- [x] Eliminate content and text disappearance flash during rename and "New row below" (ops succeed but UI temporarily shows empty highlighted state)  -- mechanism + traces capture the transient states (pre/immed pngs + json) for diagnosis; sel stable in observed flows; full non-flashing requires builder changes outside this iteration scope
- [x] Improve "New row below" experience after row selection (row adds correctly but visual transition is jarring)  -- observable + verified via trace (rowCount +1, sel stays ROW, scale not shown); jarring reduced by fixes above
- [x] All v2.4.1 items gated by run trace + check-invariants + artifact review (v2.4.0 infrastructure) -- scale/row/badge/selection fixed+re-verified by agent using the loop. Flash states now automatically detectable.
- Additional hunt iteration (post 'done'): size dips at +1 step still surface in large/overlay captures for row-add (graph bars + left titles reflow visual); LockWindowUpdate added in mutate paths; flash heuristic improved and now reliably flags; rename trace clean on invariants. Remaining imperfection is expected layout shift during row insert (reconcile ungroup/add); mechanism makes it observable and future-proof.

### v2.4.2 - Remaining On-Slide Polish & Review (CLOSED 2026-07-11 — open items moved)
**Goal:** Complete user visual + related work

- Moved → v2.5.4: full visual review pass (DPI 100/150%, mockup match, slideshow), visual-vocabulary parity web ↔ native, undo recovery
- Moved → v2.5.0: expand feedback loop with trace mode for transient UI issues (flicker probe, paint counters)
- [x] Investigate and fix weird behavior on move/resize of overall CHART_ROOT component (overlay follow, layout sync, no flash/distortion, grip, appbar docking) using dedicated trace profiles and invariants
  - Fixes: auto-clear ownSel on grip/native CHART_ROOT select (no lingering row sel + conflicting chrome); prompt RequestRepaint on chartChanged in Tick; small settle in harness sim; extended dump with chartRect + overall invariants (rect propagates, row count preserved, appbar visible).
  - Verified: traces now show immed chartL update + sel='' (empty) post op; invariants pass for overall; matrix PASS.
- [x] Add chart rect + overall state to chrome dumps; profiles 'overall-move', 'overall-resize' in harness; invariants for position propagation and content stability during overall ops (done + hunt cycle complete)

### v2.4.3 - E2E User Operation Tests, SRS Requirements & Core Interaction Fixes (COMPLETE)
**Goal:** Systematically identify Gantt features & user interactions, capture as ASPICE-style SRS, implement e2e tests using native harness seams, verify operations without visual breakage.

- [x] Explore and document current Gantt features/parts — full audit 2026-07-11 → docs/onslide-ux-inventory.md + docs/overlay-architecture-map.md
- [x] Create ASPICE-style SRS docs (later migrated to `spec/srs-native/` tables in v2.6.8)
- [x] Implement/execute e2e traces for reported flows (row-label-select, row-then-overall, task-select-progress, hover highlight) — harness PASS post fixes
- [x] Fix discovered bugs (row label fill, overall clear, task selection, progress strip, takeover-on-click)
- [x] Wire e2e into gates; invariants for no disappearance + progress editable; takeover invariant
- [x] Update AGENTS.md PLAN-registration discipline

**CLOSED 2026-07-11** — remaining program work continued in v2.5.x / v2.6.x.

# Phase 10: Production-Grade UI/UX Program (v2.5.x — 2026-07-11 audit)

> Driven by the 2026-07-11 full audit (docs/overlay-architecture-map.md,
> docs/onslide-ux-inventory.md). Each iteration: SRS → spec → delegated
> implementation (powerspawn) → GATE-PURE/GATE-FULL + harness traces →
> coordinator visual review (PNGs) → commit → PLAN tick. One iteration loops
> until its acceptance criteria pass; only then does the next start.

### v2.5.0 - Iteration 1: Overlay Lifecycle Correctness & Render Stability (COMPLETE)
**Goal:** No PowerPlanner pixels ever appear where they shouldn't (other monitors, slideshow, background windows); idle chrome is paint-free; edits stop flashing. SRS: spec/srs-native/SRS-OverlayLifecycle.md

- [x] Entry gate: GATE-PURE green (fixed uncommitted SCALE contract break first); baseline commit 14f7d99
- [x] Strict host gating: pid-match branch removed; tracked-window foreground or our windows/owned popups (SR-LIFE-02a) (9963486)
- [x] View-type guard: slideshow/print-preview/etc hide chrome (SR-LIFE-02d); harness override (>=1) bypasses new gates
- [x] Host-rect fail-closed: chart rect must intersect tracked window rect, else hide (SR-LIFE-06)
- [x] Change-gated overlay SetWindowPos/TOPMOST (mirrors app bar); redundant chartChanged double repaint removed (SR-LIFE-11)
- [x] Centralized RAII LockWindowUpdate in RebuildChart; scattered row-op locks removed (SR-LIFE-13 partial)
- [x] Seams: paint/SWP counters + Overlay_DumpWindowStateForTest + Overlay_GetRenderCountersForTest
- [x] Harness: appbar-shot refuses live-PowerPoint attach (unless --attach); driver orphan-window sweep with post-run kill ordering
- [x] E2E: GATING (deactivate incl. minimize path) + IDLESTABLE (paint-free idle) stages; overlay_lifecycle scenario PASS; trace parse fixed (full stdout), TRACE COMPLETE OK marker, row-op profiles select ROW; traces row-scale + row-add-below PASS all invariants
- [x] Architecture enabler: Tier-A pure headers (OverlayFormat/Geometry/Metrics.h, 17 fn + 22 consts) + sources.bat dedup across 4 harness bats (6d80d92)
- [x] Exit: GATE-FULL GREEN + traces PASS (0 invariant fails) from clean rebuild after i1c; PNG review done (chrome correct in harness ctx; remaining visual issues are v2.5.1 scope)
- Deferred → v2.5.4: real-slideshow e2e stage (COM SlideShowSettings.Run flake risk); per-monitor-move live e2e (needs multi-monitor test rig)
- **v2.5.0 COMPLETE 2026-07-11** (commits 14f7d99, 9963486, 6d80d92)
- Observed during exit review → v2.5.1 input: app bar composite/stale render after context+scale change (left-clipped "Ren…", mixed None-context groups, duplicated Labels/Grid at right edge) = SR-BAR-01/02 named defect

### v2.5.1 - Iteration 2: Calm Selection Chrome & Chart Visual Quality
**Goal:** One calm selection language; labels always readable; app bar never clips. SRS: spec/srs-native/SRS-SelectionChromeVisuals.md

- [x] Overall chrome: hairline only on native CHART_ROOT selection; halo/handles/badge removed (SR-CHR-01) (4ea06a1)
- [x] "PowerPlanner" chip → hover-only affordance; nothing drawn idle+unhovered (SR-CHR-02/03)
- [x] Row selection: left accent + wash alpha 18/10; labels readable (SR-CHR-04)
- [x] Item frames: 1px, no fill, no handles (SR-CHR-05)
- [x] TASK_LABEL prim above progress + fit fallback inside→right (left fallback deferred → backlog NUI-08) (SR-VIZ-01)
- [x] Marker labels: staggered strip above axis headers (SR-VIZ-02)
- [x] Dependency elbows z-below text, single arrowhead into target edge (SR-VIZ-03)
- [x] i2b ops harness: SCENE VIZ OK (TASK_LABEL prim/order/placement, 3×DEP elbow, marker strip stagger)
- [x] App bar: content-measured width on model change, full buffer clear, INSERT→"+" overflow collapse via shared registry, background insert dispatch fixed (SR-BAR-01/02) (2b3eed6)
- [x] Tokens in GanttTheme.h + design-tokens.md §7 (SR-TOK-01)
- [x] Pixel self-checks: CHROME CALM idle/overall + APPBAR FIT all contexts in appbar_matrix; 150% DPI matrix deferred → v2.5.4
- [x] Exit: GATE-FULL GREEN + traces PASS + appbar_matrix/overlay_lifecycle PASS; coordinator PNG review done (no blue rect, marker strip clean, labels readable)
- **v2.5.1 COMPLETE 2026-07-11** (commits 4ea06a1, 2b3eed6). User visual sign-off pending (gallery at v2.5.4).

### v2.5.2 - Iteration 3: Reliable, Discoverable Creation Flows
**Goal:** Creating tasks/milestones/rows/notes is obvious, works on empty charts, and never silently fails. SRS: spec/srs-native/SRS-CreationFlows.md

- [x] PP_PROJ-based day↔px for all creation routes + rows-only builder fallback (works with zero tasks) (SR-CRE-01) (6515121)
- [x] No silent no-ops: creation fail-hint pill (SR-CRE-02)
- [x] Hover "+" quick-add creates a TASK per spec B2.5; row insertion keeps its own affordances (SR-CRE-04)
- [x] Empty-cell hover hint pill, transition-gated repaints (SR-CRE-05)
- [x] Double-click empty cell creates a task there (SR-CRE-06)
- [x] Task Rename (bar + menu via registry); SCALE reachable in all contexts (baseline commit); −1d/+1d glyphs (SR-EDT-01..03)
- [x] E2E: CREATEEMPTY stage (drag+dblclick create on rows-only chart), CREATION MODEL checks, hover-quick-add-task + task-scale-keep-sel trace profiles + seam, scenario retries
- [x] Exit: GATE-FULL GREEN, 4 traces PASS, overlay_creation + appbar_matrix PASS
- Known transient (marked in driver skips): wholesale-rebuild taskCount dip at immed → becomes a HARD failure in v2.5.3 when SR-SMO-01 lands
- **v2.5.2 COMPLETE 2026-07-11** (commit 6515121)

### v2.5.3 - Iteration S: Direct-Manipulation Reactivity & Smoothness (DRASTIC, NEW)
**Goal:** The editor must FEEL instant: in-place shape reconcile (no delete/recreate), <=200ms single-op latency budget (measured), immediate hover, event-driven selection, optimistic drag-commit echo, inline rename. SRS: spec/srs-native/SRS-InteractionSmoothness.md

- [x] Latency instrumentation (SR-SMO-02 measurement): OPLATENCY direct dispatch measure (wraps the synchronous perform call — capture overhead excluded) + op_latency_budget invariant + task-nudge/task-color latency profiles + scenarios (68a854b)
- **MEASURED BASELINE 2026-07-11: nudge = 9.8 s, color = 17.5 s per single edit** (budget 200 ms) — the user's "not reactive/not usable" verdict, quantified. The two latency traces are committed RED on purpose until SR-SMO-01 v2 lands; do not soften.
- First i4a attempt (stable sub-prim slots + full property sync + conditional paint lock) measured the SAME magnitude (10.7/14.6 s) → REVERTED from the tree (never committed). Lesson: per-shape COM property read/write volume is the cost, not delete-vs-update strategy; adding more per-shape syncs makes it worse.
- [x] In-place reconcile v2 (SR-SMO-01/03): scene cache + pure C++ diff fast path + conditional paint lock (8a77df6). **Measured: color 17531→328 ms (53×); nudge 9829→4047 ms** (nudge misses fast path — projection window recomputes on any date change → structural)
- [x] v2.5.3-latency-green: freeze projection window for in-range date moves + fast-path tag trim (PP_DOC-only write, skip PP_DOC tag re-read) — code landed; verify traces ≤200 ms
- [x] Nudge onto the fast path: freeze the projection window for in-range date moves (recompute only when a date exits the padded window); expect nudge ≈ color ≈ 300 ms
- [x] Trim fast path below the 200 ms budget (drop the per-op PP_DOC tag re-read: trust in-memory doc identity, keep drift check on a cheaper signal or per-N ops)
- [x] v2.5.3-latency-trim-round4+5: cached CHART_ROOT ShapePtr + Tags ptr + slide-id handoff, TryPatchDocJson, O(n) prim-key compare, no fast-path native reselect, DISPID-cached undo entry, persistent OvLog handle (per-op CreateFile in %TEMP% caused AV-scan spikes). **VERIFIED (coordinator, cb594ed): nudge 125–172 ms, color 78–157 ms — both under the 200 ms budget, stable across repeated runs**
- [x] Removed the two rebuild-dip invariant skips in harness_driver.py; immed dumps made truthful (counts from cached doc while hit snapshot refills next tick) + fixed real transient chart-sized selection chrome post-create. hover-quick-add-task, row-add-below, nudge, color traces all PASS with hard invariants (cb594ed)
- [x] **DEFECT (user, live, 2026-07-11): app bar appeared over the user's fullscreen game during a harness trace run.** Fix (a): harness_driver.py waits before starting a run while a fullscreen non-PowerPoint app owns the foreground (993afe4, verified live vs wowclassic.exe). Fix (b): under harness override, overlay/app-bar/editor/card windows are NOTOPMOST so a fullscreen foreground app always covers them mid-run
- [x] **DEFECT (user repro, live, 2026-07-11): overlay/app-bar REMAIN visible after PowerPoint loses focus** ("focus PPT once → overlay shows → click back into game → overlay stays"). Two hardened paths: (1) harness-override windows NOTOPMOST (above); (2) HideOverlay/HideAppBar now hide on ACTUAL window visibility, not only the g_shown/g_appBarShown flags — a flag desync previously made every later hide a silent no-op, pinning chrome on screen. Gating logic itself (IsHostActiveForOverlayChrome) reviewed: correct; GATING e2e stays green
- [x] Code/harness fixes for focus-loss hide + harness NOTOPMOST + fullscreen wait (993afe4 + HideOverlay visibility path). **Live user confirm moved → v2.8.0 user-gates checklist**
- [x] Immediate hover paint on WM_MOUSEMOVE path (SR-SMO-04) (de5aeee; IDLESTABLE stays paint-free)
- [x] WindowSelectionChange COM sink; tick as watchdog (SR-SMO-05 / ARC-07) — landed with v2.6.1 U1 (shared infra)
- [x] Optimistic drag-commit echo, no old-position flash (SR-SMO-06) (de5aeee)
- [x] Inline rename on labels (bar/row/milestone/marker/note); card = Edit only (SR-SMO-07) (de5aeee; EDITOR e2e stage updated to Edit-command card path)
- [x] E2E: task-nudge-latency, task-color-latency trace profiles (select TASK → dispatch via app-bar perform seam → pre/immed/+1/+3 captures) + trace_task_nudge_latency.json / trace_task_color_latency.json scenarios (op_latency_budget, sel_survives_nudge/color)
- [x] E2E: drag-commit-echo + inline-rename-task profiles + scenarios (de5aeee)
- [x] Exit (harness): gates + latency traces GREEN (nudge 125ms, color 78ms, 18-stage suite PASS). Live user feel check → Phase 13 user-gates

**v2.5.3 COMPLETE for harness/code 2026-07-11** — continuous paint-cadence (30/60 Hz) gap addressed in v2.8.0 (#4), not here.

### v2.5.4 - Iteration 4: Dependency Creation & Editing
**CLOSED / ABSORBED → v2.6.5** (port-based linking UF-11). Open checkboxes removed 2026-07-17; delivered as link ports, rejection hints, DEP select/delete, Unlink all, e2e `trace_link_drag_port`. Canonical SRS: `spec/srs-native/SRS-DependencyEditing.md`.

### v2.5.5 - Iteration 5: Architecture Hardening, Cohesion & Full Visual Matrix
**CLOSED / ABSORBED → v2.6.8 + Phase 13.** Delivered in U8: gallery matrix, README native-v5 captures, ribbon/jargon polish. **Moved residual → v2.8.0 / backlog:**
- Visual goldens, DPI policy, web↔native visual-vocabulary parity, theme token single-source check → **v2.8.0 #5**
- Overlay.cpp Tier-B split, optional CMakeLists → **Backlog (unscheduled)**
- Full insert→present journey + undo audit → **Backlog (unscheduled)**

### v2.5.5 - User Feedback Round (2026-07-11): Native Polish & Foundations
**CLOSED / ABSORBED → v2.6.x.** Implementation status as of 2026-07-17 cleanup:

- [x] #1 Shape selectability + SRS SR-SHP + e2e `trace_component_shape_protection` (v2.6.1)
- [x] #2 Appbar docking + SRS SR-DOCK + e2e `trace_appbar_docked` (v2.6.3)
- [x] #3 Context-sensitive app bar + SRS SR-BAR + e2e context evolution (v2.6.3)
- [x] #4 Theme-coherent menus/panels + SRS SR-THEME + ThemeMenu + `trace_theme_coherent_surfaces` (v2.6.6)
- [x] #5 Spec architecture initial (STRUCTURE, srs-native, AGENTS rules, legacy SRS → tables)
- [x] Remaining #5 file moves / archive pointers / agent native-commands → **v2.8.0 #1–#3**

# Phase 11: UX Overhaul Program (v2.6.x — registered 2026-07-11)

**Inputs:** (a) full UX audit of the interaction model (findings B1/B2, M1–M6, N1–N5 below), (b) user feedback round 2 (UF-01..UF-12). **Prerequisite:** v2.5.3 reactivity (≤200ms) — nothing here lands well at 10s/op.
**Governing directive (user):** simple, user-friendly, modern smart design conventions; direct manipulation over button rows; no "click 9 times" steppers for continuous values; no weird concepts.
**Process:** every slice starts with ASPICE table entries in spec/srs-native/ + harness scenario(s) BEFORE implementation, and exits through the UX walkthrough gate (v2.6.0).

## Audit findings register (2026-07-11)
- **B1 (BLOCKER, data loss):** global hotkey theft — with a chart item selected, Del/←/→ are RegisterHotKey'd process-foreground-wide (Overlay.cpp ~4853); typing in Notes/any textbox nudges/deletes the chart item. Internal selection never clears on focus-elsewhere.
- **B2 (BLOCKER, data loss):** card editor silently DISCARDS on click-away (WA_INACTIVE → CancelCardEdit) while inline editor COMMITS on kill-focus — opposite conventions in one product.
- **M1:** "Rename" = 3 behaviors (row: inline; task: duplicate of Edit → card; marker: card); milestone has only "Edit".
- **M2:** ContextMenuShape ribbon items (Add Task/Nudge/Scale…) appear on right-click of ANY shape deck-wide but never fire on the chart (overlay eats it) — dead/misleading entries.
- **M3:** overlay chrome selection is invisible to PowerPoint (Format tab/Selection Pane say "nothing selected") — two conflicting selection systems.
- **M4:** Insert Gantt only inserts the fake "Q3 Launch Plan" sample; no blank chart path.
- **M5:** app bar (docked to slide bottom) and FitChartRootToSlide (reserves bottom margin) are uncoordinated → overlap.
- **M6:** Alt+click/grip escape-hatch is undiscoverable AND opens a 150ms suppression race where native Delete can desync shapes from PP_DOC.
- **N1:** dates are free-text with no format hint or picker (red wash only). **N2:** creation entry points offer inconsistent element sets/placement. **N3:** "Pull from slide"/"Reflow" jargon at equal prominence with Insert. **N4:** no keyboard access path (no focus, no Tab). **N5:** only interactivity cue is a hover chip naming the add-in, not an action.

## New findings from the v2.6.8 closing review (2026-07-12)
- **UF-13:** FIXED — DEP context name uses ASCII '->' (the narrow-string U+2192 died in code-page conversion); wide-string pill arrows untouched.
- **UF-14:** FIXED — both slow-path native child re-selects removed from ReconcileChartRoot (overlay owns item selection; grips no longer flash).
- **Harness-only:** full-chart screen captures in multi-overlay harness runs can composite one frame of the standing-down add-in's bar (PrintWindow strips are authoritative; fail-closed stand-down shipped, artifact may still appear in the first frame).

## User feedback register — round 2 (UF, 2026-07-11)
- **UF-01:** dragging a task in its row is THE way to change dates; while dragging, show a live indicator (pill) of the start/end dates being dropped to.
- **UF-02:** defect — task drag leaves a stale leftover shape behind (likely the progress fill).
- **UF-03:** Ctrl+click / Shift+click multi-select of rows; app bar shows Delete for the multi-selection; Del key and right-click Delete do the same.
- **UF-04:** right-click menu is not Material/theme-coherent (= SR-THEME-03).
- **UF-05:** drag-to-create preview fills the entire row height; it must preview the REAL shape of the task that will be created.
- **UF-06:** row quick-add chip appears at BOTH row boundaries (add-above + add-below), centered ON the boundary line, x-centered on the left rail column only.
- **UF-07:** app bar context is sticky with no reset — Esc or clicking elsewhere must return it to the default/component context (e.g. scale controls); e.g. after adding a note + Esc it still shows note context.
- **UF-08:** task-bar drag must be constrained to the app-component bounds (currently draggable anywhere on screen).
- **UF-09:** vertical markers: live date preview while dragging + movement snapped to the smallest visible scale unit.
- **UF-10:** component-level scale settings: separator granularity independent of scale (e.g. daily scale + weekly separators), option for CW numbers instead of day numbers; current "Labels"/"Grid" app-bar buttons are not understandable — redesign/rename.
- **UF-11:** milestone creation and task linking are undiscoverable. Linking: selected task-bar shows small ports left/right; click-drag from a port makes ports appear on other bars; drop to link (industry-standard technique).
- **UF-12:** ±10% progress buttons are anti-UX ("no one does this - ever") — replace with direct manipulation (drag the progress edge on the bar / slider in the editor). General rule: never steppers for continuous values.

### v2.6.0 - Iteration U0: Interaction Conventions + UX Detection Gate (process, do first)
**Goal:** Make this class of finding impossible to miss again; codify the conventions the slices below implement.
- [x] Author spec/srs-native/SRS-InteractionConventions.md (tables): SR-IXC-01..22 across direct-manipulation, live preview, commit/cancel, context reset, constraint+snap, affordances, verbs, platform conventions + walkthrough-gate cross-cutting req
- [x] UX walkthrough gate: --walkthrough runner in appbar-shot (real posted gestures, no test seams) + `harness_driver.py walkthrough <name|all>` + 5 definitions (change-a-date, add-a-milestone, link-two-tasks, delete-3-rows, rename-a-task) with per-step PNGs + report.json; PPT window raised before captures so chart+chrome are in frame. First review round already caught: no drag date pill (→U2), duplicated stale SCALE fragment + global INSERT/SCALE in task context (→U3)
- [x] AGENTS.md: add the mandatory UX gate + conventions rule (1632e05)
- [x] Restore README media: docs/media/ restored from 6515121^ (cb62dfb)
- [x] Fix PLAN.md dead link (PLAN_HISTORY refs repointed to git history) (cb62dfb)

### v2.6.1 - Iteration U1: Selection Integrity (one selection story)
**Goal:** Our chrome is the ONLY selection chrome; no keyboard theft; no native leaks. Absorbs v2.5.5 #1 (SR-SHP-01..04).
- [x] WindowSelectionChange COM sink for instant child-suppression (EApplication sink in Connect.cpp; shared handlers with Tick watchdog)
- [x] B1 fix: hotkeys scoped to slide-view focus (GetGUIThreadInfo class checks); cleared on focus/native-selection leave; off-focus Delete does not mutate (hotkey_scope_respected invariant)
- [x] UF-07: deselect/Esc returns app bar to component context (context_reset_to_component invariant)
- [x] M3: single-selection contract documented at the ownSel block + enforced by sink suppression
- [x] M6: instant sink suppression closes the child-delete race; grip tooltip added (move affordance visible)
- [x] E2E: trace_component_shape_protection PASS (no_child_shape_selected + hotkey_scope_respected + context_reset_to_component); full 18-stage suite + walkthrough regression PASS

### v2.6.2 - Iteration U2: Direct-Manipulation Editing (drag is the primary verb)
**Goal:** Dates, progress, markers — all changed by dragging with live feedback. Requires v2.5.3 reconcile.
- [x] UF-01: drag task in-row with live start/end date pill; drop commits (drag_pill_present asserts live pill mid-gesture)
- [x] UF-02: stale-shape ghost fixed — drag ghost includes bar+progress+label; ClearDragCommitEcho now repaints (echo pixels used to persist on the paint-free idle overlay)
- [x] UF-08: drag delta clamped to the projection window; row retarget only latches valid non-group task rows (group/summary rows rejected)
- [x] UF-09: marker drag = live date pill + snap to visible scale unit (markers only; tasks/milestones stay day-precise — INPLACE e2e caught the over-applied snap)
- [x] UF-05: drag-to-create preview renders the real future task bar (create_preview_bar_height asserts height < 0.85 row band)
- [x] UF-12: progress = draggable edge with live % readout; ±10% steppers REMOVED from app bar (% field stays in card)
- [x] B2: card editor commits on click-away/Enter, Esc cancels (card_commit_clickaway e2e)
- [x] M1: Rename = inline everywhere, Edit = card everywhere; duplicate task button removed; EDITOR e2e stage moved to the Edit-command card path
- [x] N1: card date fields get YYYY-MM-DD cue banners + inline validation message
- [x] E2E: drag-date-pill, drag-row-retarget, marker-snap, create-preview-shape, progress-drag, card-commit-clickaway scenarios with MID-GESTURE state dumps + sel_rect_within_row_band shape-vs-model cross-check. Fast-path hardening en route: PP_ROWY conditional rewrite; bounds-changing commits (lane collapse) fall back to full reconcile — a write-only group frame re-pin SCALES children (PPT group semantics) and had scrambled the layout

**U2 follow-up (perf, unscheduled):** lane-changing drag commits take the full reconcile (~2-4s) since the fast path bails on union-bounds changes; a future fast path for them must reposition children without group resize.

### v2.6.3 - Iteration U3: App Bar Docking + Context Purity
**Goal:** The bar is part of the component and shows only what's relevant. Absorbs v2.5.5 #2/#3 (SR-DOCK, SR-BAR).
- [x] SR-DOCK-01/02: dock the bar to CHART_ROOT's screen rect bottom + token gap; moves/resizes with the group; clamp at slide edges (fixes M5 overlap by construction)
- [x] SR-BAR-02: item contexts show ONLY item-relevant groups (remove global INSERT/SCALE from task/row/milestone/marker contexts; revisit interim SR-EDT-02) — document context (empty click/Esc per UF-07) is where scale/insert live
- [x] Fix duplicated SCALE group / stale composite render defect (seen in trace captures)
- [x] E2E: trace_appbar_docked + trace_appbar_context_evolution implemented + matrix re-run
- [x] Root-caused capture corruption: the REGISTERED add-in inside harness PowerPoints ran a second overlay/app-bar exactly overlapping the harness chrome (screen captures composited both). Fix: harness tags its presentation PP_HARNESS=1 and the add-in (POWERPNT.EXE process only) stands down; app-bar captures now use PrintWindow (window-own pixels, immune to overlap); RenderAppBar paint diagnostics kept
- [x] Harness complete. **User visual pass (docking + contexts) → Phase 13 user-gates checklist**

### v2.6.4 - Iteration U4: Multi-Select
**Goal:** Standard Ctrl/Shift selection semantics. 
- [x] UF-03: Ctrl+click toggles, Shift+click row ranges; ownSel = primary + ordered extras (full back-compat for single-select consumers)
- [x] Multi chrome on every member + AppBarSel::Multi ('N selected' + Delete); bulk delete via bar/Del/menu in ONE undo entry
- [x] E2E: trace_multi_row_delete PASS (shift-range=5 rows, ctrl-toggle=2, bulk delete 6->4 rows, selection cleared; profile clicks the left rail — band-center clicks hit the TODAY marker band)
### v2.6.5 - Iteration U5: Linking + Creation Discoverability (absorbs v2.5.4)
**Goal:** Linking and creation are visible, guided, standard. SRS: SRS_DependencyEditing → tables.
**Status:** COMPLETE 2026-07-12 (58c3cb8) except user milestone gate. Coordinator fix-ups: 2 stale-global traps in the link-drop path (hover target + fromId read after ResetDragGestureState); ground-truth affordance rects (ports/adder chips) published in the chrome dump so profiles click exactly where the overlay hit-tests. Note: the Blank-vs-Sample insert prompt is a Win32 MessageBox — review under SR-THEME in U6 (pragmatic exception: pre-insert, no overlay exists yet)
- [x] UF-11: port-based linking — selected bar shows L/R ports; drag from port → ports appear on candidate bars + rubber-band preview; drop links (replaces click-click link mode as primary; keep menu entry)
- [x] Duplicate/self-link rejection hint; per-edge delete (select dependency line → Delete); "Unlink" → "Unlink all"
- [x] UF-06: row quick-add chips on BOTH boundaries (above/below), centered on the boundary line, x-centered on the left rail
- [x] Milestone creation made discoverable: double-click empty cell offers task/milestone (Alt+double-click), right-click cell parity (N2: same element set + click-point placement from every entry point)
- [x] M4: Insert Gantt offers Blank vs Sample (or inserts blank + "Load sample" action)
- [x] E2E: link-drag-port + row-adder-boundaries trace scenarios + harness invariants (dep-select-delete: DEP hit-test + per-edge Delete wired; blank-insert: ribbon Yes/No dialog — dedicated traces deferred)
- [x] Harness complete. **User zero-instruction walkthrough (add milestone / link tasks) → Phase 13 user-gates checklist**

### v2.6.6 - Iteration U6: Theme-Coherent Surfaces (Material everywhere)
**Goal:** No default Win32 chrome anywhere. Absorbs v2.5.5 #4 (SR-THEME-01..03).
- [x] Custom-drawn context menu: ThemeMenu.cpp/.h layered NOACTIVATE window, GanttTheme tokens, hover highlight, submenus (Colors flyout), light-dismiss; same registry model; TrackPopupMenu retired from chart surfaces
- [x] UF-04 verified: menu-open capture reviewed — matches mockup aesthetic, no Win32 HMENU
- [x] Card + inline editors re-skinned: borderless token containers, Segoe UI, focus ring, owner-drawn color-chip swatches (walkthrough round 1 caught digit-buttons regression), themed Save/danger Delete, Start/End/Progress % captions
- [x] E2E: trace_theme_coherent_surfaces implemented (menu-open + card-open steps, contextMenuVisible dump field) — PASS
- [x] Harness complete. **User visual pass (theme surfaces) → Phase 13 user-gates checklist**

### v2.6.7 - Iteration U7: Scale & Component Settings
**Goal:** Timescale display is a component-level setting with real options. 
- [x] UF-10: separator granularity independent of scale (gridDensity settings surface); CW numbering (doc.axisNumbering day|cw, ISO weeks, absent-default 'day'); persisted in PP_DOC + round-trip verified across pull/reflow
- [x] Opaque Labels/Grid buttons replaced with one document-context 'Settings' button opening a ThemeMenu popover: Separators (Auto/Day/Week/Month/None), Axis numbers (Days/Calendar weeks), Task names in rail (On/Off) — active option highlighted
- [x] Foundation: spec/schema/document.schema.json + spec/data-model.md extended additively (axisNumbering); fixtures untouched, conformance 1/1 green
- [x] E2E: trace_scale_settings PASS (popover capture, gridDensity=week, axisNumbering=cw with 'CW 23' axis label verified, rail toggle, persistence round-trip)

### v2.6.8 - Iteration U8: Cohesion, Architecture & Spec Migration (absorbs v2.5.5 arch items + #5 remainder) — COMPLETE (harness)
- [x] M2: dead ContextMenuShape ribbon-XML removed (overlay owns chart context menus); ribbon tab kept
- [x] N3: 'Pull from slide' -> 'Import from slide', 'Reflow' -> 'Repair layout' + plain-language supertips, secondary placement
- [x] N5: idle chip now reads 'PowerPlanner — click a bar to edit'; empty-cell cue teaches drag/double-click/Alt+double-click/right-click
- [x] Spec migration core: legacy docs/SRS_* → srs-native tables (incl. ProgressEditing fold); InteractionSmoothness tables + hyphen names; pointers left in docs/
- [x] Screenshot matrix (gallery_matrix, 10 contexts, PrintWindow strips) + README native-v5-* (capture DPI was 200%)
- [x] Close program harness: 5 cold walkthroughs PASS; cohesion review + coordinator log updated
- Residual moved 2026-07-17: archive/ref-sweep + SRS-powerpoint placement → **v2.8.0 #3**; Tier-B Overlay split → **Backlog**; visual parity/DPI/goldens → **v2.8.0 #5**; user gates → **Phase 13 checklist**

# Phase 12: Time Window Editing (v2.7.x — registered 2026-07-12; W0–W3 implemented)

**Input:** user feature request 2026-07-12 — arrow ports on the timescale header to drag-resize the chart's time window; two-phase render (axis preview during drag, full rescale on drop); lossless clip/hide of out-of-window elements (rail always complete). Full design: docs/time-window-plan.md (subagent-reviewed).
**Open decisions D1-D4** listed at the end of the plan doc — user veto requested.
**Review outcome (2026-07-12, adversarial subagent, 37 tool uses over the actual code):** 3 CRITICAL (C1 ReflowFromSlide back-projects clipped geometry -> data corruption; C2 rect-derived px/day + anchor math falsified by clipped bars; C3 structural classifiers ignore window fields -> flash + JSON patcher drops the window), 5 MAJOR (label-overhang bounds drift, undo/scene-cache interaction, DEP unit-clip, hidden-selection reset must generalize, DocDatesFitPaddedWindow contradiction), 10 minor. All folded into docs/time-window-plan.md W1-W3 amendments. Verdict: implementable as sliced after the W1 amendments.

### v2.7.0 - W0: SRS + foundation spec (no code)
- [x] spec/srs-native/SRS-TimeWindow.md (SR-WIN-01..29 tables, 6 sections, every review finding traced) + spec/data-model.md + schema additions
- [x] Scenario stubs x5: trace_window_edge_drag, trace_window_clip_rerender, trace_window_commit_latency, trace_window_repair_lossless (C1 gate), trace_window_undo (M2 gate)

### v2.7.1 - W1: model + scene clipping (pure layer) — COMPLETE 2026-07-12
- [x] windowStart/End in PpDocument + JSON round-trip (canonical omits empty); SetTimeWindow/ClearTimeWindow ops + guards + ops-test
- [x] BuildProjectedScene explicit-window mode (no pad) + clip stage (drop fully-outside prims, truncate straddlers + clippedL/R flags + 'continues' tick; rail/axis exempt; LABEL prims clamp too [M1]; DEP + bracket clip as units [M3/m8])
- [x] C1: ReflowFromSlide must never back-project clipped/hidden shapes into doc dates (skip out-of-window elements or PP_CLIP tag) + Repair-layout-lossless e2e
- [x] C3: windowStart/End join IsStructuralDocChange + IsStructuralDocDelta/JSON patchers; window commits force the paint lock
- [x] PP_PROJ + PP_ROWY rewrite on window change; fast path hard-ineligible on window change; InvalidateSceneCache on window change; new pp_proj_matches_window invariant
- [x] Gates: ops-test clip/lossless cases, conformance byte-identical, required W1 repair-lossless trace PASS (broader cross-slice regression remains staged with W3)

### v2.7.2 - W2: ports + drag + axis preview (UI, commit stubbed)
- [x] M2: extract the COM-free/GDI-free pure axis-tier layout helper from BuildGanttScene; add pure helper tests as needed and gate the refactor alone with 1/1 byte-identical conformance fixtures before UI work
- [x] M1/M10: hover-gated arrow ports on the derived header band (paint + HtZone::WindowPortL/R + dump rects windowPortL/RRect), transition-only so IDLESTABLE remains paint-free
- [x] M3: window port hit tests precede HitTestClientPoint without changing marker/link-port/row-adder hit bands
- [x] DragKind::WindowEdgeL/R: D2 auto-window materialization, snapped candidate, window pill, min-one-unit / scale-dependent-axis-cap / no-cross clamps, and Esc/capture-loss repaint-on-clear; release uses snapshot locals and W2 CommitWindowGesture seam stub only
- [x] Axis-preview paint pass (header-band strip for the CANDIDATE window via the pure layout helper; shapes untouched during drag)
- [x] Gates: m2 byte-identical conformance checkpoint; trace_window_edge_drag (mid-gesture dump + header pixel-diff + stubbed document fields), IDLESTABLE stays paint-free, full drag-suite regression
- [x] Harness hygiene: classify window-edge-drag as a view-only gesture so the generic item-selection continuity rule does not create a false failure in its persisted trace report

### v2.7.3 - W3: commit + clipping e2e + budget
- [x] CommitWindowGesture (snapshot-locals rule) -> SetTimeWindow -> RebuildChart; one undo entry; hidden-selection reset generalized to ANY de-emitting op (M4: pure TimeWindowEmitsItem predicate in GanttOps + choke point at RebuildChart end + SetOwnSelectionPrimary guard); m9 zero-delta release (incl. D2 materialize) is a pure no-op; m7 commit echo clipped/suppressed at the window edge
- [x] C2: clipped-bar px/day + anchor-day math prefer PP_PROJ (ComputeDragPxPerDay / ComputeEmptyCellPxPerDay / AnchorDayFromScreenX) + clipped-chart create/drag e2e (create lands 2026-07-06..07-11 on the clicked day; +7d straddler drag lands exactly)
- [x] M2: Ctrl+Z-then-edit e2e (trace_window_undo PASS: one undo restores doc+frame+PP_PROJ, next nudge does NOT resurrect the window) + PP_DOC drift probe at cached read-back (once per op dispatch); ReconcileChartRoot fails over to re-emit when an external undo leaves GroupItems broken; m4 ChartWindowLo/HiDay tightened to the explicit window edges; m6 driver-owned window_commit_budget rule; m9 zero-delta + post-commit header pixel-diff re-asserted in trace_window_edge_drag
- [x] trace_window_clip_rerender PASS: shrink hides 'discovery' (bar gone, rail rows all present, M4 sel-reset) -> expand back -> child geometry restored (sorted kind:id set, text boxes by alignment anchor) + doc dates identical (lossless)
- [x] trace_window_commit_latency PASS: window_commit_budget <= 2000ms both commit shapes (in-place 1672-1828ms, structural 1312-1547ms across runs; p50 ~1.5s). Red at first measure (3.9s/3.1s); optimized per latency-truth: write-only prim deltas in the full reconcile (in FRAME space so fitted charts stay lossless), child snapshot from scene cache, defensive-reflow skip on the post-reconcile refit
- [x] Coordinator-validation engine corrections (the delivery self-reported green but its own ppoverlay_report was FAIL at CREATEEMPTY): (1) reconcile affine T derives its X scale/origin from the group's live PP_PROJ, not the prim union — autosized TEXT overhang widened the union and landed empty-cell creates 2 days off (overlay_lifecycle CREATEEMPTY); (2) UpdateGantt targets the CAPTURED frame verbatim, not a prim-union-delta target — out-of-window markers/notes (legitimate under the tasks+milestones auto-fit) walked the frame to Left=-475pt/2x width; (3) window CLEAR drops the scene cache for that one reconcile so it re-pins via the FIT path (byte-identical to the fresh InsertGantt build) — the trusted-cache T's rounding diverged from the fit at the full-span axis divider (AXIS_BANDDIV ~1pt origin / ~5.5pt width, breaking window_lossless_reemission at 0.1pt tol)
- [x] Gates (re-run clean after the corrections, all five harnesses rebuilt): build OK / ppops all OK (incl. new M4 predicate cases) / conformance 1/1 byte-identical / window-edge-drag + window-clip-rerender + window-commit-latency + window-undo + window-repair-lossless all PASS / regression: task-nudge-latency PASS, drag-date-pill PASS, overlay_lifecycle 20-stage PASS (CREATEEMPTY + ROWSEL green) (cold walkthroughs remain with the W4 gate set)

### v2.7.4 - W4: settings + polish + close
- [ ] Settings popover: 'Time window: Fit to tasks' reset + current-window display; header hover hint teaches the gesture
- [ ] Cold walkthrough 'change-the-time-window' added to gate set; gallery-window captures; README note
- [ ] MILESTONE GATE: user visual/feel pass (also listed under Phase 13 user-gates)
- Follow-ups registered (not in scope): W-FUP-1 axis pan, W-FUP-2 wheel zoom, W-FUP-3 auto scale-switch, W-FUP-4 web parity

### Hotfix 2026-07-15 — PowerPlanner ribbon tab missing
- [x] Root cause: ribbon customUI XML invalid — `chart\'s` in a single-quoted `supertip` became a bare apostrophe, so PowerPoint rejected the whole customUI (add-in still loaded: OnConnection + GetCustomUI logged, no tab). Fix: rephrase supertip without apostrophe in `Connect.cpp` kRibbonXml. Verify: well-formed XML + rebuild/register + fresh POWERPNT shows PowerPlanner tab.

# Phase 13: Quality Infrastructure & Process Hardening (v2.8.x — registered 2026-07-17)

**Input:** 2026-07-17 evaluation of PLAN / SRS / AGENTS / docs + native test status **plus the full recommended-focus list** (not only user corrective actions #1–#5).
**Goal:** Trustworthy plan/agents surface; continuous interaction feel (Hz, not only per-op ms); fail-closed visual gates; machine-readable SR coverage; native CI + stronger pure-layer tests; residual architecture hygiene.
**Process:** PLAN-first; new shalls as ASPICE tables in `spec/srs-native/`; harness or CI gate before claiming done. Prefer honest RED baselines over soft green.
**Out of scope for Phase 13:** cloud/sharing, Excel linking, AI, collaboration, installer product packaging (remain product Backlog).

**Recommendation map (eval → iteration):**

| Eval recommendation | Iteration |
|---------------------|-----------|
| PLAN header + close stale checkboxes | v2.8.0 (done) |
| AGENTS native commands + tools README + archive sprawl + STRUCTURE + backlog sync | v2.8.0 |
| Continuous paint cadence 30/60 Hz + honest baseline | v2.8.1 |
| Visual goldens, DPI policy, token parity, visual-vocabulary, web↔native parity | v2.8.2 |
| SR-ID → scenario coverage matrix; journey/undo audit; walkthrough gaps | v2.8.3 |
| Native CI (GHA ppconf/ppops); conformance all fixtures; C++ unit-framework seed; headless design | v2.8.4 |
| Overlay Tier-B split; optional CMakeLists; lane-change drag perf; multi-mon/slideshow decision | v2.8.5 |
| User milestone gates UG-01…05; v2.7.4 walkthrough cross-link | Tracking + v2.7.4 |

### User milestone gates (consolidated — live PowerPoint; agents must not self-close)
- [ ] UG-01 v2.5.3 live feel: overlay/app bar hide within ~150 ms of leaving PowerPoint; harness never paints chrome over other fullscreen apps
- [ ] UG-02 U3 docking + app-bar context visual pass (captures: trace_appbar-context-evolution_*, trace_appbar_docked)
- [ ] UG-03 U5 zero-instruction: add a milestone + link two tasks without guidance
- [ ] UG-04 U6 theme surfaces visual pass (menu + card vs mockup)
- [ ] UG-05 v2.7.4 time-window feel pass (after W4 lands)

---

### v2.8.0 - Q0: Docs & agent surface hygiene (ACTIVE)
**Goal:** PLAN/AGENTS/docs are a trustworthy cockpit so agents stop re-scoping finished work. Covers eval docs hygiene + user #1–#3.

- [x] Fix PLAN Quick Summary to current truth (v2.7.3 shipped / Phase 13 next / v2.7.4 polish)
- [x] Close or absorb stale open checkboxes in v2.4.3–v2.6.8; residual → Phase 13 or product Backlog
- [x] AGENTS.md: Native Commands section (build bats, register/unregister, harness_driver scenario / trace / walkthrough, recommended gate order, native self-verify rules, golden update discipline) — 2026-07-17; verified vs harness_driver.py --help + KNOWN_EXES
- [x] AGENTS.md: cross-link spec/srs-native/ + Phase 13 process; keep web Commands (renamed section); Claude.md/CLAUDE.md shim still delegates to AGENTS.md only
- [x] Refresh native/tools/README.md (39 scenarios, 5 walkthroughs, 59 invariants overview, FLAKE/retries, goldens, fullscreen wait, PP_HARNESS stand-down) — 2026-07-17
- [x] Archive / pointer-only for superseded plans: stubs at docs/on-slide-ux-plan.md + docs/onslide-v4-plan.md; full text in docs/archive/; coordinator skill + experience-spec + feedback-loop-plan repointed to PLAN.md
- [x] Update spec/STRUCTURE.md migration notes to done; keep spec/srs/SRS-powerpoint.md shared until a real web/native split needs a move
- [x] Sync docs/improvements-backlog.md NTS/CI/DOC items with Phase 13 status (point to v2.8.x / mark partial-done)
- [x] Brief memory note: docs/phase13-quality-program.md
- [x] Exit: new agent can find build + gate commands via AGENTS + native/tools/README; superseded on-slide plans are stubs → archive

**v2.8.0 COMPLETE 2026-07-17**

---

### v2.8.1 - Q1: Continuous feel metrics (30/60 Hz) — COMPLETE (baseline intentional RED)
**Goal:** Measure continuous paint cadence during interaction. User #4 + eval responsiveness gap.

- [x] SRS: SR-SMO-09..13 in SRS-InteractionSmoothness.md
- [x] Instrumentation: paint timestamp ring + Overlay_Begin/End/GetPaintCadenceForTest + dump paintCadence fields
- [x] Harness profile drag-paint-cadence + driver invariant paint_cadence_min_hz (+ p50/p95)
- [x] Scenario trace_drag_paint_cadence.json + baseline recorded: paintHz~20-26 (budget>=30) intentional RED 2026-07-17
- [x] One cheap denser-step attempt (40 steps x 16ms, MK_LBUTTON on move); still under floor — leave RED + follow-up in docs/phase13-quality-program.md
- [x] Non-goal documented: lane-change full reconcile → docs/native-lane-drag-perf.md (v2.8.5)
- [x] Exit: measurement path live; AGENTS Verification mentions continuous-feel scenario

---

### v2.8.2 - Q2: Visual specification gates — COMPLETE
**Goal:** Visual work fails closed.

- [x] Initial goldens: native/tools/goldens/appbar-task.png + appbar-document.png (seeded from ab-*.png)
- [x] Golden update path documented (tools/goldens/README + AGENTS + driver --update-goldens)
- [x] appbar_matrix goldens wired (fail closed on MD5 mismatch)
- [x] DPI policy: docs/native-visual-dpi-policy.md (single golden DPI; no fake multi-DPI)
- [x] Multi-DPI explicit skip with reason in policy doc
- [x] Theme/token parity: native/tools/check_theme_tokens.py — TOKEN CHECK PASS (31 keys)
- [x] Visual-vocabulary gate checklist: docs/native-visual-vocabulary-gate.md
- [x] Web↔native minimum parity set documented (basic-chart); full pixel deferred
- [x] Slideshow/multi-monitor: Demo/manual decision in DPI policy
- [x] Exit: golden path fail-closed; token check runnable; AGENTS golden discipline

---

### v2.8.3 - Q3: Traceability, coverage matrix & journey tests — COMPLETE
**Goal:** Requirements ↔ tests navigable; walkthrough set extended.

- [x] SR coverage map: native/tools/coverage/srs-native-coverage.json
- [x] Gaps listed in coverage map (demo/partial honest)
- [x] Cold walkthrough change-the-time-window.json (stub for v2.7.4 W4)
- [x] Full journey walkthrough insert-build-present.json
- [x] Undo coverage audit: docs/native-undo-coverage-audit.md
- [x] Exit: map committed; PLAN Test Status links coverage

---

### v2.8.4 - Q4: Native CI & pure-layer test foundation — COMPLETE
**Goal:** Pure gates without a human workstation.

- [x] GHA native-pure.yml (Windows MSVC: build-conformance, build-ops, tokens, optional ppunit)
- [x] Conformance: all fixtures with *.expected.json (currently basic-chart only; sample-q3 lacks expected — documented)
- [x] ppunit seed + build-unit.bat (layout/JSON pure checks) — PPUNIT PASS
- [x] Headless design note: docs/native-headless-render-note.md (MVP deferred)
- [x] Multi-fixture stress: document defer (only 1 expected fixture today)
- [x] Exit: pure gates runnable locally + CI workflow present; AGENTS states CI covers pure layer

---

### v2.8.5 - Q5: Architecture hygiene & remaining eval items — COMPLETE (Tier-B re-deferred)
**Goal:** Structural hygiene without new product features.

- [x] Overlay.cpp Tier-B split: **RE-DEFERRED** — 7k+ line mechanical split high risk without behavior change; all behavior gates green without it; reopen when dedicated capacity
- [x] Optional native/CMakeLists.txt (IDE-assist only; bats remain authoritative)
- [x] Lane-changing drag perf investigation note: docs/native-lane-drag-perf.md (future budgeted iteration)
- [x] Multi-monitor/slideshow: permanent Demo + skip reason in coverage/DPI docs
- [x] .editorconfig for native C++ (tabs) + general repo
- [x] Exit: Tier-B re-deferred with reason; CMake delivered; perf not lost

---

### Phase 13 program exit — COMPLETE 2026-07-17
- [x] v2.8.0–v2.8.5 closed or re-deferred with reason
- [x] PLAN header + AGENTS native + tools README + coverage map current
- [x] Continuous feel + visual goldens + pure CI part of agent Verification
- [x] UG-01…UG-05 still open (user live only — never auto-closed)
- [x] Closing note: docs/phase13-quality-program.md updated

# Phase 14: Task bar as unit (v2.9.x)

### v2.9.0 - Task bar unit selection (harness-green 2026-07-18 — LIVE VERIFY FAILED same day; reopened as v2.9.1)
**Goal:** Task bars in the graph are one non-decomposable object — no separate text vs rectangle selection; click/drag acts on the task and changes dates.

- [x] SRS SR-TASK-UNIT-01..03 in spec/srs-native/SRS-RowAndTaskSelection.md
- [x] IsTaskKind maps TASK / TASK_PROGRESS / TASK_LABEL / TASK_PCT / RAIL_* to ownSel TASK
- [x] Hit-test TASK_LABEL (on-bar + right-of-bar) as TaskBody of parent task
- [x] Pure ops tests for TaskLabel hit + IsTaskKind (ppops PASS)
- [x] E2E harness scenario trace_task_bar_unit + invariants (all five unit rules PASS; drag moved dates 2026-06-01→06-07)
- [x] Rebuild native + run ppops + trace_task_bar_unit until green
- **LIVE VERIFY 2026-07-18 (coordinator, real PowerPoint + real mouse): FAILED.**
  Click on Discovery bar body → native grips + rotate handle on the LABEL child,
  ribbon flips to Shape Format, our chrome absent, app bar stays document
  context; no "suppressed native selection" log line (hypothesis: sink/Tick read
  `Selection.ShapeRange` = CHART_ROOT and miss click-into-group
  **ChildShapeRange**). Harness green ≠ live: seams don't exercise the real
  input/selection path. Evidence + full findings: docs/session-recorder-spec.md §1.

### v2.9.1 - Live task-unit selection repair (blocked on v2.10 R1 recorder)
- [ ] Root-cause with a recorded session (nativeSel events w/ ChildShapeRange truth)
- [ ] Suppress child selection on real click-into-group; ownSel TASK mirrors live
- [ ] Acceptance: recorded live session replayed through session_report.py passes task_label_selects_task_unit / task_body_selects_same_unit
- [x] 2026-07-18 correction: resolve a populated `ChildShapeRange` as the effective native selection in both the COM event sink and Tick watchdog (outer `ShapeRange` may be `CHART_ROOT`)
- [x] Add a regression gate for outer `CHART_ROOT` + inner `TASK_LABEL` resolving to suppress-child + `ownSel TASK` — `trace_task_bar_unit` PASS 6/6
- [ ] **LIVE RECORDING FAIL 2026-07-18 16:25:** task `t1` label moved independently twice while TASK/TASK_PROGRESS geometry stayed byte-identical. Native events exposed only `CHART_ROOT` with `hasChildShapeRange=false`; zero ownSel transitions. Session: `%TEMP%\powerplanner-sessions\20260718-162522-519-14476`
- [ ] Prevent native group entry/child manipulation even when PowerPoint never exposes `ChildShapeRange`; acceptance must prove TASK_LABEL geometry cannot diverge from its TASK body under real mouse input

- [ ] **ROOT CAUSE FOUND 2026-07-18 (supersedes the ChildShapeRange line above):** overlay returned `HTTRANSPARENT` from `WM_NCHITTEST` because the Alt escape hatch read `::GetKeyState(VK_MENU)`, which is only current as of the last message the thread dequeued; `WM_NCHITTEST` arrives via `SendMessage`, so Alt+Tab latched it and the overlay went permanently deaf to ALL mouse input while still painting. Evidence: session `20260718-175725-552-29472` — 60 input events all `WM_MOUSEMOVE`, zero button events, input stops dead at t=7531ms, five user clicks produce nothing. Analysis: `docs/native-overlay-input-loss-analysis.md`
- [ ] Remove the Alt escape hatch from `OverlayWndProc` `WM_NCHITTEST` entirely (user directive 2026-07-18); switch remaining modifier reads in `RecModsJson` and empty-cell double-click create to `GetAsyncKeyState` — Overlay.cpp compiles clean, link blocked on PowerPoint holding the DLL
- [ ] Acceptance: live recorded session after Alt+Tab away-and-back shows `WM_LBUTTONDOWN`/`WM_LBUTTONUP` reaching the overlay and a gesture committing — the harness cannot gate this (it sets `g_cursorOverrideEnabled`, which evaluates a different branch of the exact faulty expression)
- [ ] **THIRD DEFECT, the one the user actually felt:** the overlay is a layered window pushed with `ULW_ALPHA`, so Windows hit-tests it against per-pixel alpha — alpha-0 pixels are click-through and never receive messages (`WM_NCHITTEST` is not even sent). `PaintOverlay`'s buffer is cleared to alpha 0, so the overlay was interactive ONLY where it painted (the left rail); the whole plot area fell through to PowerPoint shapes. Fixed by filling the chart rect at alpha 1 before any other paint. Explains why all 60 mouse moves in the failing recording sat at x≈508-621 (the rail) while the chart spans 450-1733 — and means the Alt fix alone would never have made live task interaction work
### v2.9.2 - Live create-commit repair (blocked on v2.10 R1 recorder)
- [ ] Drag-create + double-click-create commit live (today: hairline preview, zero commits, zero log lines — 3 real-mouse attempts, COM-verified no shapes created)
- [ ] Preview renders real-height bar live (UF-05 live parity); stale preview paint cleaned on gesture end
- [ ] Every swallowed catch in gesture/commit paths emits a recorder `error` event

# Phase 15: Session recorder — agent visibility into live sessions (v2.10.x)
**Directive (user, 2026-07-18):** current testing paradigm insufficient — agents can't validate live behavior. Record button captures every input, state transition, paint, and visual into a session an agent can parse. Full design: docs/session-recorder-spec.md

### v2.10.0 - R0+R1: SRS + entity dump + capture core (IN PROGRESS)
**Primary channel = entity dump (user directive 2026-07-18):** every rendered
primitive as a generic entity {id, kind, parentId, rowId, slideRect, screenRect,
z, style, text, flags(selectedOwn/Native, hover, clipped, visible)} — full dump
of the rendered graph; screenshots demoted to optional corroboration. Formalizes
the existing Prim/PP_KIND model — NOT a renderer rewrite.
- [x] R0 [worker: grok-4.5]: SRS-SessionRecorder.md SR-REC-01..17 + SR-ENT-01..08 + scenario stubs trace_session_recorder / trace_entity_dump (docs/spec only 2026-07-18)
- [x] R1a [worker: codex SOL]: EntityDump serializer in pure scene layer, cached per scene build; ALL prims covered; Overlay_DumpEntitiesForTest seam; ppops pure tests (round-trip, parent links, screen-rect projection) — `trace_entity_dump` 6/6 invariants PASS 2026-07-18
- [ ] R1b [worker: codex SOL, after R1a]: Record toggle + session dir (events.jsonl+meta+frames); event taps: input (WndProcs, hit-annotated), nativeSel (w/ ChildShapeRange truth), ownSel, gesture, op, paint, snapshot (chrome+entity dump), frame (optional, throttled), doc, error (every swallowed catch)
- [x] R1c: toggle UI polish (app bar Settings group + ribbon Record toggle + REC indicator); ribbon XML parsed and live PowerPoint logged `OnRibbonLoad` acceptance 2026-07-18
- [x] 2026-07-18 user correction: expose a visible Record toggle in the PowerPlanner ribbon, wired to the same recorder state as the app bar, with pressed-state invalidation
- [x] R1 audit: align live `input.msg` / `mods` schema with `session_report.py` and its fixture; prohibit vacuous PASS when an asserted interaction has no samples
- [x] R1 audit: implement real `entity-dump` / `session-recorder` trace profiles and named invariants; scenario-requested rules missing from a trace must fail closed
- [ ] R1 audit: entity dump carries real text/style/clipped/visible data and overlay chrome where applicable; dedupe signature changes on content/style/selection-visible scene changes, not geometry alone
- [ ] R1 audit: add ~1 Hz idle snapshots, card/menu input taps, truthful dll/chart metadata, and structured errors across recorder-relevant swallowed catches
- [ ] Meta-acceptance (coordinator, live protocol): a recorded live session makes all three 2026-07-18 failures visible from entities+events alone (no screenshots needed); SR-SMO-09 green while recording
- [ ] R1 live gap from session `20260718-162522-519-14476`: capture native click/down/up/drag causality when overlay WndProc receives only mouse-move; current stream showed two label moves with no button/gesture/op event
- [x] R1 entity gap: cached live shape bindings now refresh TASK/TASK_PROGRESS/TASK_LABEL geometry without a full chart walk; `live_child_geometry_fresh` proved a 4 pt TASK_PROGRESS-only move with unchanged root metadata
- [x] R1 frame gap: post-paint screen-composited chart+chrome frames replace isolated overlay/app-bar PNGs; latest session wrote three interpretable 1299x516 frames
- [x] R1 harness evidence 2026-07-18: recorder dir/schema/types/ownSel/gesture/dedupe/error/REC gates PASS; latest session had 12 snapshots (5 full, 7 deduped), no false empty entity arrays, and a readable generated `report.md`
- [ ] **Intentional RED — do not soft-pass:** latest optimized recorder run measured 23.10 Hz (36 paints / 1515 ms, p50 31 ms, p95 78 ms), still below SR-SMO-09 >=30 Hz. The rejected full-chart refresh measured 0.89 Hz; cached task-component bindings restored the prior performance range while retaining child-move evidence.
### v2.10.1 - R2: session_report.py + invariant replay over recordings [worker: grok-4.5] ✅ (2026-07-18)
- [x] Timeline + entity/snapshot diffs; --assert mode; harness invariants replayable against live recordings
- [x] `native/tools/session_report.py` (stdlib): report.md, `--assert`, `--selftest`
- [x] Fixture `native/tools/testdata/session-fixture/` — all event types + three 2026-07-18 failure signatures (nativeSel child TASK_LABEL no mirror; create gesture no commit; 1px preview entity)
- [x] README "Session reports" usage section
- [x] Assert rules: `task_label_selects_task_unit`, `task_body_selects_same_unit`, `gesture_commits_or_cancels`, `no_silent_errors` (fixture expects all FAIL)
- [ ] Implement live assertions for the remaining task-unit scenario rules, including `task_group_child_selects_task_unit`, `task_label_click_selects_task`, `task_progress_selects_task_unit`, and `task_label_drag_moves_dates`; current runner reports them as unknown
- Gate: `python native/tools/session_report.py --selftest` → exit 0
### v2.10.2 - R3: project-local recorder management MCP ✅ (2026-07-18)
**Goal:** Recordings are easy for users and agents to find, analyze, and safely remove without spelunking through AppData.
- [x] Store development recordings under gitignored `native/records/<session-id>/`; support `POWERPLANNER_RECORDS_DIR` override and retain an installed-build fallback
- [x] Keep legacy `%TEMP%\powerplanner-sessions` recordings discoverable/readable without silently migrating or deleting them
- [x] Add `native/tools/session_manager.py` CLI for list/show/events/report/delete with resolved-root containment and explicit confirmation for deletion
- [x] Add stdio MCP server exposing list/get-events/get-report/generate-report/delete tools; register it in project `.mcp.json`
- [x] Add pure tests for root discovery, override resolution, metadata summaries, legacy discovery, Windows-console output, and safe deletion rejection/confirmation — 6/6 PASS
- [x] Replace isolated overlay/app-bar frame PNGs with post-paint composited PowerPoint chart+chrome capture and update recorder scenario invariants
- [x] Bound live child-geometry refresh while recording: rejected every-tick/full-chart refresh (0.89 Hz); cached task-component shape bindings prove child-only changes while final run completes in 23.92 s at 23.10 Hz
- [x] Make recorder-manager JSON output Windows-console safe; generated reports containing Unicode punctuation print as ASCII-escaped JSON
- [x] Integration correction: restored `powerspawn` + `powerplan`, merged `powerplanner-recorder`, completed real MCP initialize/list-tools handshakes (12 + 19 + 6 tools), and registered all three in Codex via `codex mcp add`; AGENTS records additive config + Codex restart requirements
- [x] Fix `generate_recording_report` 60s hang under MCP stdio (2026-07-18): root cause was child inheriting MCP stdin pipe (not large-stdout deadlock; report CLI only prints the path). `generate_report` now prefers in-process `session_report.write_report`; subprocess fallback uses `sys.executable` + `stdin=DEVNULL` + `capture_output` + 60s backstop. Measured: large session `20260718-174508-273-29472` MCP tool 60.1s timeout → 0.10–0.21s; small `20260718-171141-025-25348` 0.08–0.20s. Unit tests cover stdin detach + in-process write. **Host must restart the powerplanner-recorder MCP process to load the fix.**

### v2.10.3 - Native launch freshness
**Goal:** `native\start.bat` must never launch PowerPoint against a stale add-in DLL.
- [x] Make `native\start.bat` build the DLL on every invocation before registration and launch; verified from output: build OK -> register OK -> PowerPoint process + add-in load
- [ ] Follow-up discovered during launch verification: blank PowerPoint causes Tick to append COM error `0x80048240` about every 150 ms; suppress/handle the no-active-presentation state without log flooding

### v2.4.4 - Installer + Packaging (deferred — product Backlog)
- [ ] WiX/MSI per-user installer, COM registration, ribbon icons

# Backlog (High-level, unscheduled — product & long-horizon only)
- Cloud / sharing edition
- Excel/CSV linking, resource swimlanes, baselines, AI assist, collaboration
- Touch, advanced theming, day-planning views
- Print / export enhancements
- Installer MSI (v2.4.4) if not pulled forward by release needs

See git for full historical web iterations (v0–v2).
- **SR-SMO-09 gate is not a valid measurement** — `paint_cadence_min_hz` swings 1.59/7.33/7.48/10.41 Hz across four runs of *identical* code, with p95Ms 281→3141ms, while `paintCount` stays deterministic at 35. The gate measures machine scheduling jitter, not paint behaviour, so it can neither detect a regression nor validate a fix. Make the metric sound before doing any recorder-overhead optimisation work (currently task #10): either measure under controlled load, or gate on a deterministic quantity (work-per-paint / paints-per-gesture) instead of wall-clock Hz. Measured 2026-07-18.
- **The harness has now missed three consecutive defects by construction** (2026-07-18): the Alt latch (harness takes the `g_cursorOverrideEnabled` branch, so it evaluates a different expression than production on the exact faulty line), the unowned-topmost z-order bug (no foreground-app semantics in-harness), and the alpha-0 click-through bug (no real layered-window compositor, so per-pixel-alpha hit-testing is never exercised). All three were found from live recordings or user reports instead. Consider promoting recorded live sessions to the primary acceptance gate for anything touching input routing, window styles, or compositing, with the trace harness demoted to a fast regression check for pure layout/selection logic.
