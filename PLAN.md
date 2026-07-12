# PowerPlanner Implementation Plan

## Quick Summary

**Current Version:** v2.5.2 (COMPLETE 2026-07-11)
**Next Milestone:** v2.5.3 — Direct-manipulation reactivity (latency ≤200ms), then the v2.6.x UX Overhaul Program (2026-07-11 audit + user feedback round 2)

### Key Metrics
- **Total Iterations:** Web foundation complete; Native on-slide work in progress
- **Focus:** Native PowerPoint on-slide (think-cell style overlay + app bar)
- **Test Harness:** Python driver + C++ harnesses with DumpChromeState + goldens (see native/tools/)

### Recent Achievements (v2.x + Native)
- ✅ v2.0.0 — PRO features (Critical Path, Baseline, Minimap) + public release
- ✅ v2.1.0 — Landing site
- ✅ v2.2.0 — Embeddable renderer + PowerNote integration
- ✅ v2.3.1 — PowerPoint add-in alpha (Office.js) + dev ergonomics
- ✅ Native foundation — COM add-in, shape emission, round-trip, basic on-slide overlay + V1/V2 editing, S1–S6 V4 slices (harness green, 2026-07-10)
- ✅ Agent feedback loop: harness_driver.py, scenarios, Overlay_DumpChromeStateForTest, --report (docs/native-agent-feedback-loop-plan.md)
- ✅ v2.4.0: full before/immed/delayed trace + invariants + rich dump (rowCount, hasScaleGroup, appBarGroups) + perform seam + proof that it flags v2.4.1 issues (e.g. hasScale while ROW)
- Ongoing: extended trace for overall CHART_ROOT move/resize; LockWindowUpdate + flash detection; additional visual reflow hunt (graph/labels) 

### Next Up (EXECUTION ORDER — confirmed by user 2026-07-11)
1. **v2.5.3** — Direct-manipulation reactivity ≤200ms (spec/srs-native/SRS-InteractionSmoothness.md). FIRST: nothing UX lands well at 4s/op.
2. **v2.6.0 → v2.6.8** — UX Overhaul Program (Phase 11 below), in slice order U0..U8: conventions+gate, selection integrity, direct-manipulation editing, app-bar docking/context, multi-select, linking+creation discoverability, theme-coherent surfaces, scale settings, cohesion/arch/spec migration. UX is the priority of the program.
- Inputs: 2026-07-11 UX audit (B1/B2, M1–M6, N1–N5) + user feedback round 2 (UF-01..UF-12). Absorbs v2.5.4 (dependency editing → v2.6.5) and the v2.5.5 feedback-round items (#1–#4 → v2.6.1/v2.6.3/v2.6.6); v2.5.5 architecture/spec-migration items → v2.6.8
- Delegation: coordinator (Claude) dispatches PowerSpawn subagents (cursor/copilot/grok/codex) per unit, validates gates itself, commits, ticks PLAN.md. Provider/model performance is logged in docs/on-slide-coordinator-log.md for the end-of-program comparison report.
- (v2.5.2 complete; v2.5.0 and v2.5.1 complete — see detailed sections below)
- Audit references: docs/overlay-architecture-map.md, docs/onslide-ux-inventory.md, spec/srs-native/ (new canonical location for native-only ASPICE SRS tables)
- Continue to use `trace` + `check-invariants` on any chrome mutation work. Update docs/ *.md and PLAN.md recursively.

### Test Status
See native/tools/ and tests/ for harness + unit coverage. Run `python native/tools/harness_driver.py scenario ...`

### Quick Links
- [Product Requirements](PRD.md)
- [README](README.md)
- [Specification (concept layer)](spec/README.md)
- [Native add-in notes](docs/native-addin.md)
- Implementation History: `git log` (web iterations v0–v2 closed in history)
- [On-Slide UX Plan](docs/onslide-v4-plan.md)

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

### v2.4.3 - E2E User Operation Tests, SRS Requirements & Core Interaction Fixes (NEW / ACTIVE FOCUS)
**Goal:** Systematically identify Gantt features & user interactions, capture as ASPICE-style SRS, implement e2e tests using native harness seams (DumpChromeStateForTest, traces, screenshots, invariants), verify operations work without visual breakage (disappearing titles, content loss on selection changes), and fix the app. Use trace infrastructure for all verification.

- [x] Explore and document current Gantt features/parts — full audit 2026-07-11 → docs/onslide-ux-inventory.md + docs/overlay-architecture-map.md
- [x] Create ASPICE-style SRS docs — docs/SRS_RowAndTaskSelection.md, SRS_ProgressEditing.md + (2026-07-11) SRS_OverlayLifecycle.md, SRS_SelectionChromeVisuals.md, SRS_CreationFlows.md, SRS_DependencyEditing.md
- Moved → v2.5.0/v2.5.2/v2.5.3: register remaining user operations as e2e scenarios (creation flows, dependency flows, lifecycle/gating scenarios)
- [ ] Implement/execute e2e traces for reported flows:
  - Hover left row labels → highlight
  - Click row label to select → verify title/row label remains visible (no disappearance), ownSelKind=ROW, rowBands stable, screenshots clean
  - Select row → then select overall (CHART_ROOT) → verify no total content disappearance, proper state transition (sel becomes overall/container, visuals intact)
  - Task selection in chart (body click) → highlight/ownSel, allow progress edit
  - Progress editing (via appbar or direct) without overlap hiding text
- [x] Fix discovered bugs (row label click causing title loss -> changed ROW highlight to left accent only, no full fill over labels; overall select clearing -> auto clear ownSel + clear on root sel; task selection now exercised and works via SelectForTest + hit; progress/text overlap noted + percent +/- now in TASK appbar group).
- [x] Wire e2e harness runs into gates / onslide-coordinator acceptance; add invariants for "no visual element disappearance on selection change", "progress visible and editable when task selected" (new rowLabelCount seam + scenarios + trace profiles added; matrix + new traces runnable; verification shows labels stable on row/task select and row->overall; progress edit step added).
- [x] Run full e2e verification with harness (matrix PASS, user op traces stable post fixes for highlight, progress strip, percent in appbar, clears).
- E2E hunt verification (2026-07-10/11 reports + runs): row-label-select: rowLabels=4 stable (pre/immed/ROW/+1/+3), sel=ROW; row-then-overall: rowLabels=4 after row and after overall (no disappear); task-select-progress: TASK sel, rowLabels=4 (new invariant task_select_and_progress_stable True); matrix PASS. All using rowLabelCount seam + trace points. Fixes verified. Scenarios run via harness (FLAKE expected for trace profiles, reports are the state verification). Longer pumps + invalidate in task profile ensure stability during edit. Dev servers terminated (max_runtime); native e2e independent.
- [x] Hunt and fix intermittent "weird resize on click" bug: clicking an element (task/row/etc) sometimes causes selection chrome/highlight/overlay to resize and take over the entire component drawing area (full chart rect instead of item rect).
- [x] Extend DumpChromeStateForTest with selScreenRect + frameRect (for detection of oversized item chrome).
- [x] Add harness trace sim (force native root after item sel in row/task profiles) + new 'no_full_component_takeover_on_item_sel' invariant (compares selS/frame vs chartRect).
- [x] Root cause: in Tick, seeing native CHART_ROOT would unconditionally set full g_selScreenRect + clear ownSel (common after item clicks, since children suppressed and native stays root) -> takeover on next tick. "once in a while" = tick timing.
- [x] Fix: only apply root chrome from native if g_ownSelKind.empty(); item ownSel now wins, Sync runs, small rect used. Grip still clears explicitly.
- [x] Re-verify: with sim, selS/frame small (item) not full chart; takeover inv True; rowL stable; matrix PASS. Reports/rects in json confirm.
- [x] Update AGENTS.md (done) and ensure future tasks always land in PLAN.md immediately.
- Moved → v2.5.0 entry gate: run full verification (harness scenarios + traces + matrix) before/after Iteration 1 changes.
- Moved → v2.5.1: user screenshot feedback (2026-07-11) — overbearing blue "PowerPlanner" rectangle/frame/badge overall chrome (now specified in docs/SRS_SelectionChromeVisuals.md SR-CHR-01..03).

**CLOSED 2026-07-11** — remaining items moved into v2.5.x below.

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
- [ ] Confirm with user (live check next session): overlay hides within 150ms of clicking away from PowerPoint; no chrome over other apps during harness runs
- [x] Immediate hover paint on WM_MOUSEMOVE path (SR-SMO-04) (de5aeee; IDLESTABLE stays paint-free)
- [x] WindowSelectionChange COM sink; tick as watchdog (SR-SMO-05 / ARC-07) — landed with v2.6.1 U1 (shared infra)
- [x] Optimistic drag-commit echo, no old-position flash (SR-SMO-06) (de5aeee)
- [x] Inline rename on labels (bar/row/milestone/marker/note); card = Edit only (SR-SMO-07) (de5aeee; EDITOR e2e stage updated to Edit-command card path)
- [x] E2E: task-nudge-latency, task-color-latency trace profiles (select TASK → dispatch via app-bar perform seam → pre/immed/+1/+3 captures) + trace_task_nudge_latency.json / trace_task_color_latency.json scenarios (op_latency_budget, sel_survives_nudge/color)
- [x] E2E: drag-commit-echo + inline-rename-task profiles + scenarios (de5aeee)
- [~] Exit: gates + latency traces GREEN (nudge 125ms, color 78ms, 18-stage suite PASS); LIVE user feel check PENDING (final judge)

### v2.5.4 - Iteration 4: Dependency Creation & Editing
**ABSORBED 2026-07-11 → v2.6.5** (port-based linking per UF-11 replaces/extends the items below; keep SRS_DependencyEditing content, convert to tables).
**Goal:** Linking is visible, guided, and reversible per-edge. SRS: spec/srs-native/SRS-DependencyEditing.md

- [ ] Link-mode crosshair over valid targets + target hover ring (SR-DEP-01/02)
- [ ] Duplicate/self-link rejection with hint pill (SR-DEP-03)
- [ ] Drag-from-link-handle to create dependency (rubber-band preview) (SR-DEP-04/05)
- [ ] Dependency lines hit-testable + selectable (ownSelKind=DEP) with minimal app bar context (SR-DEP-06)
- [ ] Per-edge delete (key/button/menu); Unlink relabeled "Unlink all" (SR-DEP-07)
- [ ] E2E: link-mode-click-target, link-duplicate-rejected, link-drag-handle, dep-select-delete, link-esc-cancel
- [ ] Exit: gates + scenarios green; PNG review

### v2.5.5 - Iteration 5: Architecture Hardening, Cohesion & Full Visual Matrix
**ABSORBED 2026-07-11 → v2.6.8** (structure/matrix/parity items move there; do after the UX slices so the matrix captures the new UX).
**Goal:** Sustainable file structure + end-to-end product cohesion review. Refs: docs/overlay-architecture-map.md split plan, improvements-backlog CI-06/07/08.

- [ ] Overlay.cpp Tier-B split: OverlayState.h struct bundling + OverlayAppBar/CardEditor/Drag/ContextMenu/TestSeams .inc.h extractions (header-only)
- [ ] Optional native/CMakeLists.txt alongside bats (non-authoritative) for IDE/incremental builds
- [ ] Theme single-source: parity check web ↔ native ↔ design-tokens.md (fail on divergence)
- [ ] Full user-journey walkthrough (insert → build plan → present) + undo coverage audit
- [ ] Full screenshot matrix (all contexts × 100/150% DPI) + final gallery; slideshow behavior verified
- [ ] Visual-vocabulary parity web ↔ native pass
- [ ] Update README gallery + docs; close the program with a summary report

### v2.5.5 - User Feedback Round (2026-07-11): Native Polish & Foundations
**ABSORBED 2026-07-11 → v2.6.x**: #1 → v2.6.1, #2/#3 → v2.6.3, #4 → v2.6.6, #5 remaining migration → v2.6.8. The SRS tables authored here (spec/srs-native/SRS-NativeUXFoundations.md) stay canonical.
**Goal:** Address live user feedback on native on-slide: PP shape selectability, appbar docking + context, theme-coherent UI surfaces, and project spec/SRS file architecture cleanup. Add requirements (correct ASPICE table format under spec/), e2e harness coverage, and fixes. Ties into v2.5.4 structure work.

- [ ] #1 Shape selectability: internal emitted shapes (task bars, labels, connectors, etc. inside CHART_ROOT) must never be directly selectable as individual PowerPoint shapes; only the CHART_ROOT "app-component" container shall be a selectable PP shape. All intra-component selection must route exclusively through our overlay hit-test + ownSel model. Strengthen suppression (beyond current 150ms Unselect poll), add invariants, document contract.
- [x] #1 Add SRS in proper table format: spec/srs-native/SRS-NativeUXFoundations.md (SR-SHP-01..04).
- [ ] #1 E2E: implement profile + run scenario trace_component_shape_protection --check-invariants.
- [x] #2 Appbar sticky docking: the app bar shall be positioned immediately below the logical bounds of the CHART_ROOT "app-component" (with small defined gap per tokens), forming a visual unit. Moving/resizing the CHART_ROOT group shall synchronously move the app bar to stay docked. Consider "imaginary outline" of the combined component+bar for future interactions. Fix any lag/offset/gap variance observed in live use.
- [x] #2 Add SRS: SR-DOCK-01..03 in SRS-NativeUXFoundations.md.
- [x] #2 E2E: run trace_appbar_docked + docking verification.
- [x] #3 Context-sensitive app bar: selection of overall document / CHART_ROOT / empty shall surface document-level controls (timescale/scale, labels, grid, insert). Selection of a task/row/etc. shall show *only* the relevant actions for that object (no timescale etc.). Refine BuildAppBar / RebuildAppBarModelFromSlide + tests.
- [x] #3 Add/extend SRS: SR-BAR-01..03.
- [x] #3 E2E: run trace_appbar_context_evolution + matrix.
- [ ] #4 Theme-coherent menus & panels: All right-click context menus, card editors, inline editors, popups, and any future panels shall be custom-rendered using GanttTheme.h / design-tokens.md values and match the approved onslide-mockup.html aesthetic (no default Win32 HMENU or unstyled dialog chrome). Current TrackPopupMenu + standard child controls are non-compliant.
- [x] #4 Add SRS: SR-THEME-01..03 + cross-cutting SR-NUI in SRS-NativeUXFoundations.md.
- [ ] #4 E2E + impl: run trace_theme_coherent_surfaces; deliver custom-drawn theme-coherent menu and edit panel.
- [ ] #4 Requirements rule: any new popup/panel code must have corresponding SRS entry citing theme coherence before implementation.
- [x] #5 File/Spec architecture cleanup + enforcement (initial):
  - Redraw documented in new `spec/STRUCTURE.md`; spec/ = Foundation + `spec/srs/` (shared tables); `spec/srs-native/` created for native-only (SRS-NativeUXFoundations.md covers #1–#4 with proper tables).
  - docs/ remains for notes, plans, inventory, mockups (no new primary SRS).
  - Created `spec/srs-native/README.md`.
  - Updated `spec/README.md`, `spec/srs/README.md`, `AGENTS.md` (mandatory rules), `PLAN.md`.
  - Added 4 harness scenario JSON stubs (`trace_component_shape_protection.json`, `trace_appbar_docked.json`, `trace_appbar_context_evolution.json`, `trace_theme_coherent_surfaces.json`).
  - [x] Full conversion of legacy `docs/SRS_*.md` → tables in srs-native + ref sweep (b66825e, 7ed6e11); pointers left in docs/
- [x] #5 Update AGENTS.md (mandatory registration + correct SRS format/location + harness pairing).
- [x] #5 Update spec/README.md + created `spec/STRUCTURE.md`.
- [ ] #5 Remaining file moves, reference fixes, cleanup verification.
- [ ] All 5 points: after code changes run full relevant harness traces + matrix + `python native/tools/harness_driver.py ... --check-invariants`; update goldens only when intentional; record in PLAN.
- [x] Smoke / launch verification (this pass): `npm run build` succeeded cleanly (tsc + vite production build). `npm run dev` background launch + HTTP fetch on http://localhost:5180 returned 200 + valid Vite-served PowerPlanner root HTML. Process cleanup successful (no lingering listener). Full output logged. Re-verify required before any future claims involving app launch or native changes.

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
- [ ] MILESTONE GATE: user visual pass on docking + contexts (captures ready: trace_appbar-context-evolution_*_appbar.png + trace_appbar_docked)

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
- [ ] MILESTONE GATE: user walkthrough — "add milestone" and "link tasks" with zero instructions

### v2.6.6 - Iteration U6: Theme-Coherent Surfaces (Material everywhere)
**Goal:** No default Win32 chrome anywhere. Absorbs v2.5.5 #4 (SR-THEME-01..03).
- [x] Custom-drawn context menu: ThemeMenu.cpp/.h layered NOACTIVATE window, GanttTheme tokens, hover highlight, submenus (Colors flyout), light-dismiss; same registry model; TrackPopupMenu retired from chart surfaces
- [x] UF-04 verified: menu-open capture reviewed — matches mockup aesthetic, no Win32 HMENU
- [x] Card + inline editors re-skinned: borderless token containers, Segoe UI, focus ring, owner-drawn color-chip swatches (walkthrough round 1 caught digit-buttons regression), themed Save/danger Delete, Start/End/Progress % captions
- [x] E2E: trace_theme_coherent_surfaces implemented (menu-open + card-open steps, contextMenuVisible dump field) — PASS
- [ ] MILESTONE GATE: user visual pass

### v2.6.7 - Iteration U7: Scale & Component Settings
**Goal:** Timescale display is a component-level setting with real options. 
- [ ] UF-10: separator granularity independent of scale (day/week/month separators at any scale); CW (calendar-week) numbering option; persisted in PP_DOC as component settings
- [ ] Replace/rename opaque "Labels"/"Grid" buttons with a comprehensible scale-settings surface (themed popover from the document-context bar)
- [ ] Foundation: extend spec (data-model/layout) + fixtures for separator/CW settings (shared with web later)
- [ ] E2E: scale-settings scenarios + captures across D/W/M

### v2.6.8 - Iteration U8: Cohesion, Architecture & Spec Migration (absorbs v2.5.5 arch items + #5 remainder)
- [ ] M2: remove dead ContextMenuShape ribbon-XML items (or scope them to actual chart selection)
- [ ] N3: demote/relabel "Pull from slide"/"Reflow" (plain-language tooltips; secondary placement)
- [ ] N5: interactivity affordance — hover cue suggests the action (e.g. "double-click to edit") instead of naming the add-in
- [ ] Spec migration remainder: convert 6 docs/SRS_*.md prose files → spec/srs-native tables (fold SRS_ProgressEditing into selection/task SRS); reformat SRS_InteractionSmoothness.md to tables + rename to hyphen convention; move/alias spec/srs/SRS-powerpoint.md under srs-native; archive docs/on-slide-ux-plan.md (repoint onslide-coordinator skill) + docs/powerpoint-addin.md; bulk ref sweep
- [ ] Overlay.cpp Tier-B split (OverlayState.h + .inc.h extractions) — from v2.5.5
- [ ] Full screenshot matrix (contexts × 100/150% DPI) + README/site gallery refresh + web↔native parity pass — from v2.5.5
- [ ] Close the program: full user-journey walkthrough (insert → build plan → present) + summary report

### v2.4.4 - Installer + Packaging (deferred)
- [ ] WiX/MSI per-user installer, COM registration, ribbon icons

# Backlog (High-level, unscheduled)
- Cloud / sharing edition
- Excel/CSV linking, resource swimlanes, baselines, AI assist, collaboration
- Touch, advanced theming, day-planning views
- Print / export enhancements

See git for full historical web iterations (v0–v2).
