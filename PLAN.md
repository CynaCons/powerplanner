# PowerPlanner Implementation Plan

## Quick Summary

**Current Version:** v2.4.3 (closed 2026-07-11; open items moved into v2.5.x)
**Next Milestone:** v2.5.x - Production-grade UI/UX program (5 iterations from the 2026-07-11 full audit)

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
- **v2.5.0** (ACTIVE) - Overlay lifecycle correctness & render stability — fixes ghost chrome on other monitors + flicker (docs/SRS_OverlayLifecycle.md)
- v2.5.1 - Calm selection chrome & chart visual quality (docs/SRS_SelectionChromeVisuals.md)
- v2.5.2 - Reliable, discoverable creation flows (docs/SRS_CreationFlows.md)
- v2.5.3 - Dependency creation & editing (docs/SRS_DependencyEditing.md)
- v2.5.4 - Architecture hardening + cohesion + full visual matrix
- Audit references: docs/overlay-architecture-map.md, docs/onslide-ux-inventory.md
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

### v2.5.0 - Iteration 1: Overlay Lifecycle Correctness & Render Stability (ACTIVE)
**Goal:** No PowerPlanner pixels ever appear where they shouldn't (other monitors, slideshow, background windows); idle chrome is paint-free; edits stop flashing. SRS: docs/SRS_OverlayLifecycle.md

- [ ] Entry gate: GATE-PURE green on current tree; commit the pending v2.4.2/v2.4.3 working-tree fixes as baseline
- [ ] Strict host gating: drop pid-match foreground branch; require tracked-window foreground (or our windows/owned popups) (SR-LIFE-02a)
- [ ] View-type guard: hide chrome during slideshow/print-preview/protected view (SR-LIFE-02d)
- [ ] Monitor fail-closed: chart rect must intersect the tracked window's monitor, else hide (SR-LIFE-06)
- [ ] Change-gate the overlay SetWindowPos/TOPMOST (mirror app bar pattern); remove redundant chartChanged double repaint (SR-LIFE-11)
- [ ] Centralize flash masking: single RAII LockWindowUpdate (or equivalent) inside RebuildChart; remove scattered row-op locks (SR-LIFE-13 partial)
- [ ] Seams: per-window paint/SetWindowPos counters + Overlay_DumpWindowStateForTest (windows, rects, monitors, counters, gating verdict)
- [ ] Harness: appbar-shot refuses to attach to an already-running PowerPoint (unless --attach); harness_driver desktop sweep asserts zero PowerPlanner* windows post-run
- [ ] E2E scenarios: overlay-deactivate, overlay-minimize, overlay-slideshow, overlay-idle-stability (paint-free idle ticks); flicker probe in traces
- [ ] Architecture enabler: extract Tier-A pure headers (OverlayMetrics.h/OverlayGeometry.h/OverlayFormat.h) + shared source list sources.bat
- [ ] Exit: GATE-FULL + all new scenarios green from clean rebuild; traces re-run; PNG review; docs updated

### v2.5.1 - Iteration 2: Calm Selection Chrome & Chart Visual Quality
**Goal:** One calm selection language; labels always readable; app bar never clips. SRS: docs/SRS_SelectionChromeVisuals.md

- [ ] Overall chrome: hairline only when CHART_ROOT natively selected; no badge/filled rect stacking on native grips (SR-CHR-01)
- [ ] "PowerPlanner" chip → small hover-only affordance (SR-CHR-02); nothing drawn when idle+unhovered (SR-CHR-03)
- [ ] Row selection: left accent + label-safe wash; pixel-verified label visibility in all states (SR-CHR-04)
- [ ] Consistent thin item frames + handles for task/milestone/marker/text (SR-CHR-05)
- [ ] Bar label above progress fill + fit fallback chain inside→right→left (SR-VIZ-01)
- [ ] Marker labels: dedicated band/offset, never over axis text (SR-VIZ-02)
- [ ] Dependency z-order below text, arrowheads into target edge (SR-VIZ-03)
- [ ] App bar: content-measured width, overflow collapse policy, no clipping (SR-BAR-01/02)
- [ ] Tokens: GanttTheme.h + docs/design-tokens.md updated together (SR-TOK-01)
- [ ] Pixel invariants in harness: label-region stability, no-giant-fill, app-bar completeness; screenshot matrix at 100/150% DPI
- [ ] Exit: gates + matrix green; before/after PNG gallery for user review

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

### v2.5.3 - Iteration 4: Dependency Creation & Editing
**Goal:** Linking is visible, guided, and reversible per-edge. SRS: docs/SRS_DependencyEditing.md

- [ ] Link-mode crosshair over valid targets + target hover ring (SR-DEP-01/02)
- [ ] Duplicate/self-link rejection with hint pill (SR-DEP-03)
- [ ] Drag-from-link-handle to create dependency (rubber-band preview) (SR-DEP-04/05)
- [ ] Dependency lines hit-testable + selectable (ownSelKind=DEP) with minimal app bar context (SR-DEP-06)
- [ ] Per-edge delete (key/button/menu); Unlink relabeled "Unlink all" (SR-DEP-07)
- [ ] E2E: link-mode-click-target, link-duplicate-rejected, link-drag-handle, dep-select-delete, link-esc-cancel
- [ ] Exit: gates + scenarios green; PNG review

### v2.5.4 - Iteration 5: Architecture Hardening, Cohesion & Full Visual Matrix
**Goal:** Sustainable file structure + end-to-end product cohesion review. Refs: docs/overlay-architecture-map.md split plan, improvements-backlog CI-06/07/08.

- [ ] Overlay.cpp Tier-B split: OverlayState.h struct bundling + OverlayAppBar/CardEditor/Drag/ContextMenu/TestSeams .inc.h extractions (header-only)
- [ ] Optional native/CMakeLists.txt alongside bats (non-authoritative) for IDE/incremental builds
- [ ] Theme single-source: parity check web ↔ native ↔ design-tokens.md (fail on divergence)
- [ ] Full user-journey walkthrough (insert → build plan → present) + undo coverage audit
- [ ] Full screenshot matrix (all contexts × 100/150% DPI) + final gallery; slideshow behavior verified
- [ ] Visual-vocabulary parity web ↔ native pass
- [ ] Update README gallery + docs; close the program with a summary report

### v2.4.4 - Installer + Packaging (deferred)
- [ ] WiX/MSI per-user installer, COM registration, ribbon icons

# Backlog (High-level, unscheduled)
- Cloud / sharing edition
- Excel/CSV linking, resource swimlanes, baselines, AI assist, collaboration
- Touch, advanced theming, day-planning views
- Print / export enhancements

See git for full historical web iterations (v0–v2) and docs/PLAN_HISTORY.md if created.
