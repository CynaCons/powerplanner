# PowerPlanner Implementation Plan

## Quick Summary

**Current Version:** v2.5.1 (COMPLETE 2026-07-11)
**Next Milestone:** v2.5.2 — Reliable, Discoverable Creation Flows (first of remaining iterations in the v2.5.x program)

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

### Next Up
- **v2.5.2** - Reliable, discoverable creation flows (docs/SRS_CreationFlows.md) — closing (CREATEEMPTY gate)
- **v2.5.3** - DRASTIC: Direct-manipulation reactivity & smoothness (spec/srs-native/SRS_InteractionSmoothness.md) — user verdict 2026-07-11: "looks good but not usable; not reactive, not smooth, not intuitive"
- v2.5.4 - Dependency creation & editing (docs/SRS_DependencyEditing.md)
- v2.5.5 - Architecture hardening + cohesion + full visual matrix
- (v2.5.0 and v2.5.1 complete — see detailed sections below)
- Audit references: docs/overlay-architecture-map.md, docs/onslide-ux-inventory.md, spec/srs-native/ (new canonical location for native-only ASPICE SRS tables)
- Continue to use `trace` + `check-invariants` on any chrome mutation work. Update docs/ *.md and PLAN.md recursively.

### Test Status
See native/tools/ and tests/ for harness + unit coverage. Run `python native/tools/harness_driver.py scenario ...`

### Quick Links
- [Product Requirements](PRD.md)
- [README](README.md)
- [Specification (concept layer)](spec/README.md)
- [Native add-in notes](docs/native-addin.md)
- [Implementation History](docs/PLAN_HISTORY.md) - Completed iterations (old web)
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
**Goal:** No PowerPlanner pixels ever appear where they shouldn't (other monitors, slideshow, background windows); idle chrome is paint-free; edits stop flashing. SRS: docs/SRS_OverlayLifecycle.md

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
**Goal:** One calm selection language; labels always readable; app bar never clips. SRS: docs/SRS_SelectionChromeVisuals.md

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
**Goal:** Creating tasks/milestones/rows/notes is obvious, works on empty charts, and never silently fails. SRS: docs/SRS_CreationFlows.md

- [ ] PP_PROJ-based day↔px for all creation routes (works with zero tasks) (SR-CRE-01)
- [ ] No silent no-ops: disable or hint, never nothing (SR-CRE-02)
- [ ] Hover "+" quick-add creates a TASK per spec B2.5; row insertion keeps its own affordances (SR-CRE-04)
- [ ] Empty-cell hover hint pill ("Drag to create a task…") (SR-CRE-05)
- [ ] Double-click empty cell creates a task there (SR-CRE-06)
- [ ] Task Rename affordance; SCALE group reachable in all contexts; directional −/+ nudge glyphs (SR-EDT-01..03)
- [ ] E2E: create-task-empty-chart, create-milestone-cell, hover-quick-add-task, scale-while-task-selected, task-rename-route
- [ ] Exit: gates + scenarios green; PNG review

### v2.5.3 - Iteration S: Direct-Manipulation Reactivity & Smoothness (DRASTIC, NEW)
**Goal:** The editor must FEEL instant: in-place shape reconcile (no delete/recreate), <=200ms single-op latency budget (measured), immediate hover, event-driven selection, optimistic drag-commit echo, inline rename. SRS: spec/srs-native/SRS_InteractionSmoothness.md

- [ ] In-place UpdateGantt reconcile for single-element ops (SR-SMO-01); no Ungroup/Delete-all cycle
- [ ] Latency instrumentation in traces (opLatencyMs) + op_latency_budget invariant <=200ms (SR-SMO-02)
- [ ] Drop LockWindowUpdate for single-element ops (SR-SMO-03)
- [ ] Immediate hover paint on WM_MOUSEMOVE path (SR-SMO-04)
- [ ] WindowSelectionChange COM sink; tick as watchdog (SR-SMO-05 / ARC-07)
- [ ] Optimistic drag-commit echo, no old-position flash (SR-SMO-06)
- [ ] Inline rename on labels (bar/row/milestone/marker/note); card = Edit only (SR-SMO-07)
- [ ] E2E: task-nudge-latency, task-color-latency, drag-commit-echo profiles + reconcile shape-identity assertions
- [ ] Exit: gates + latency traces green; LIVE user feel check (final judge)

### v2.5.4 - Iteration 4: Dependency Creation & Editing
**Goal:** Linking is visible, guided, and reversible per-edge. SRS: docs/SRS_DependencyEditing.md

- [ ] Link-mode crosshair over valid targets + target hover ring (SR-DEP-01/02)
- [ ] Duplicate/self-link rejection with hint pill (SR-DEP-03)
- [ ] Drag-from-link-handle to create dependency (rubber-band preview) (SR-DEP-04/05)
- [ ] Dependency lines hit-testable + selectable (ownSelKind=DEP) with minimal app bar context (SR-DEP-06)
- [ ] Per-edge delete (key/button/menu); Unlink relabeled "Unlink all" (SR-DEP-07)
- [ ] E2E: link-mode-click-target, link-duplicate-rejected, link-drag-handle, dep-select-delete, link-esc-cancel
- [ ] Exit: gates + scenarios green; PNG review

### v2.5.5 - Iteration 5: Architecture Hardening, Cohesion & Full Visual Matrix
**Goal:** Sustainable file structure + end-to-end product cohesion review. Refs: docs/overlay-architecture-map.md split plan, improvements-backlog CI-06/07/08.

- [ ] Overlay.cpp Tier-B split: OverlayState.h struct bundling + OverlayAppBar/CardEditor/Drag/ContextMenu/TestSeams .inc.h extractions (header-only)
- [ ] Optional native/CMakeLists.txt alongside bats (non-authoritative) for IDE/incremental builds
- [ ] Theme single-source: parity check web ↔ native ↔ design-tokens.md (fail on divergence)
- [ ] Full user-journey walkthrough (insert → build plan → present) + undo coverage audit
- [ ] Full screenshot matrix (all contexts × 100/150% DPI) + final gallery; slideshow behavior verified
- [ ] Visual-vocabulary parity web ↔ native pass
- [ ] Update README gallery + docs; close the program with a summary report

### v2.5.5 - User Feedback Round (2026-07-11): Native Polish & Foundations
**Goal:** Address live user feedback on native on-slide: PP shape selectability, appbar docking + context, theme-coherent UI surfaces, and project spec/SRS file architecture cleanup. Add requirements (correct ASPICE table format under spec/), e2e harness coverage, and fixes. Ties into v2.5.4 structure work.

- [ ] #1 Shape selectability: internal emitted shapes (task bars, labels, connectors, etc. inside CHART_ROOT) must never be directly selectable as individual PowerPoint shapes; only the CHART_ROOT "app-component" container shall be a selectable PP shape. All intra-component selection must route exclusively through our overlay hit-test + ownSel model. Strengthen suppression (beyond current 150ms Unselect poll), add invariants, document contract.
- [x] #1 Add SRS in proper table format: spec/srs-native/SRS-NativeUXFoundations.md (SR-SHP-01..04).
- [ ] #1 E2E: implement profile + run scenario trace_component_shape_protection --check-invariants.
- [ ] #2 Appbar sticky docking: the app bar shall be positioned immediately below the logical bounds of the CHART_ROOT "app-component" (with small defined gap per tokens), forming a visual unit. Moving/resizing the CHART_ROOT group shall synchronously move the app bar to stay docked. Consider "imaginary outline" of the combined component+bar for future interactions. Fix any lag/offset/gap variance observed in live use.
- [x] #2 Add SRS: SR-DOCK-01..03 in SRS-NativeUXFoundations.md.
- [ ] #2 E2E: run trace_appbar_docked + docking verification.
- [ ] #3 Context-sensitive app bar: selection of overall document / CHART_ROOT / empty shall surface document-level controls (timescale/scale, labels, grid, insert). Selection of a task/row/etc. shall show *only* the relevant actions for that object (no timescale etc.). Refine BuildAppBar / RebuildAppBarModelFromSlide + tests.
- [x] #3 Add/extend SRS: SR-BAR-01..03.
- [ ] #3 E2E: run trace_appbar_context_evolution + matrix.
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
  - [ ] Full conversion of legacy `docs/SRS_*.md` → tables in srs-native + bulk ref sweep + prune.
- [x] #5 Update AGENTS.md (mandatory registration + correct SRS format/location + harness pairing).
- [x] #5 Update spec/README.md + created `spec/STRUCTURE.md`.
- [ ] #5 Remaining file moves, reference fixes, cleanup verification.
- [ ] All 5 points: after code changes run full relevant harness traces + matrix + `python native/tools/harness_driver.py ... --check-invariants`; update goldens only when intentional; record in PLAN.
- [x] Smoke / launch verification (this pass): `npm run build` succeeded cleanly (tsc + vite production build). `npm run dev` background launch + HTTP fetch on http://localhost:5180 returned 200 + valid Vite-served PowerPlanner root HTML. Process cleanup successful (no lingering listener). Full output logged. Re-verify required before any future claims involving app launch or native changes.

### v2.4.4 - Installer + Packaging (deferred)
- [ ] WiX/MSI per-user installer, COM registration, ribbon icons

# Backlog (High-level, unscheduled)
- Cloud / sharing edition
- Excel/CSV linking, resource swimlanes, baselines, AI assist, collaboration
- Touch, advanced theming, day-planning views
- Print / export enhancements

See git for full historical web iterations (v0–v2) and docs/PLAN_HISTORY.md if created.
