# PowerPlanner — Improvements Backlog (handoff)

A broad brainstorm of improvements and tests, grouped by area and ID-prefixed so
they're easy to reference/assign. Not prioritized; pick by value. Items reference
real files where useful. "(spec)" means it should be reflected in `spec/`.

---

## A. Native add-in — features
- NAT-01 Auto-reflow on the polling timer (detect a committed bar move/resize and reflow without the Reflow button). Builds on N4 timer + N5 `ReflowFromSlide`.
- NAT-02 Clickable on-slide controls in the overlay (add-task, delete, nudge ±1 day, change %): hit-test the overlay window (drop `WS_EX_TRANSPARENT` on control regions). (→ extends N4/N5)
- NAT-03 Drag a bar *via the overlay* (our own drag handles) rather than PowerPoint's, snapping to days live.
- NAT-04 "Insert from file" — file-open dialog to load a `GanttDocument` JSON instead of the built-in sample (closes the N3 follow-on).
- NAT-05 Editable document in a task pane (Office-style) or a native dialog: title, scale, add/remove rows/tasks.
- NAT-06 Milestone + dependency creation on-slide (draw a dependency by dragging between bars).
- NAT-07 Update-in-place reflow (move only the changed shapes) instead of delete+re-emit, to preserve selection/animation and be faster.
- NAT-08 Support multiple charts per slide (scope `CHART_ROOT`/overlay to the selected chart by group id).
- NAT-09 Respect `calendar.scale`/`fiscalYearStart` in the native axis (currently month-only gridlines).
- NAT-10 Weekend/non-working-day shading in the native renderer (SRS-TAX-031).
- NAT-11 Today line + deadline markers in the native renderer.
- NAT-12 Critical-path highlight toggle in the add-in (reuse the CPM algorithm, ported to C++).
- NAT-13 Baseline drift rail in the native renderer.
- NAT-14 Honor per-element `color` (optional "branded" mode) vs the Material theme default.
- NAT-15 Undo integration — make insert/reflow a single native undo step where the OM allows.

## B. Native add-in — UI / Material polish
- NUI-01 Dark Material theme + follow PowerPoint's theme/Office dark mode.
- NUI-02 Per-group color tiers (Material tonal palette) for readability while staying calm.
- NUI-03 Precise corner radius on bars (`Shape.Adjustments[1]`) for consistent small Material radii.
- NUI-04 Typography pass: font family (e.g. set bars/labels to a clean sans), sizes, weights, tighter text-frame insets/anchoring.
- NUI-05 True square brackets (think-cell style) instead of an outline box around rows.
- NUI-06 Row banding (subtle alternating fill) as an alternative/companion to dividers.
- NUI-07 Axis: minor (week) ticks + thinning to match `spec/layout.md` §9.
- NUI-08 Better label collision handling (on-bar → right → left fallback) per SRS-ELEM-006.
- NUI-09 Milestone label placement options (above/below/right) and de-overlap.
- NUI-10 Legend / title block styling; optional subtitle + date range footer.
- NUI-11 Overlay: animated/eased reposition, hover affordances, and a contextual mini-toolbar.
- NUI-12 High-DPI correctness audit for the overlay across monitors/scale factors.

## C. Native add-in — architecture / renderer
- ARC-01 Port the layout's date math to share a single source with the conformance fixtures (done) and add a C++ unit-test runner (e.g. doctest) beyond the conformance harness.
- ARC-02 Extend `Scene`/`PptRenderer` with: gradients off by default, group/z-order hints, opacity, dashed lines, text wrapping, multi-run text.
- ARC-03 A second `Scene` backend that renders to SVG/GDI+ for headless preview (decouples tests from PowerPoint).
- ARC-04 Replace the freehand `PP_PROJ` string with JSON via `GanttJson` (consistency).
- ARC-05 Centralize PowerPoint OM access behind a thin wrapper to ease testing/mocking.
- ARC-06 Error surface: structured logging levels + a user-visible diagnostics command (dump `%TEMP%` log).
- ARC-07 COM event sink (`EApplication::WindowSelectionChange`) as an alternative/in-addition to polling (lower idle cost).
- ARC-08 Memory/lifetime audit (SAFEARRAY/BSTR/_variant_t) + run under Application Verifier.

## D. Native add-in — packaging / distribution
- PKG-01 WiX/MSI per-user installer (N6) with ribbon icons and clean uninstall.
- PKG-02 Authenticode code-signing for the DLL + installer (SmartScreen-clean).
- PKG-03 CI job to build the x64 DLL and attach it to GitHub Releases (so "another PC" gets a prebuilt binary).
- PKG-04 32-bit build variant (for 32-bit Office) or a bitness-detection note/installer.
- PKG-05 AppSource-style listing assets if ever distributing the Office.js variant.
- PKG-06 Version stamping (DLL version resource) + an About box.

## E. Web app — features
- WEB-01 Resource swimlanes / assignee view.
- WEB-02 Excel/CSV linking (bind tasks to spreadsheet rows).
- WEB-03 AI assist ("draft a Gantt from this paragraph", "compress schedule by 2 weeks").
- WEB-04 Comments anchored to tasks.
- WEB-05 Style preset gallery.
- WEB-06 Auto-scheduling from dependencies (push successors when a predecessor moves).
- WEB-07 Constraint enforcement for dependencies (deferred since v0.3).
- WEB-08 Grouping/collapse UX polish; drag rows to reorder/reparent.
- WEB-09 Export to PPTX directly (bridge to the native shape vocabulary) / export to MS Project XML / iCal.
- WEB-10 Print/PDF pagination for large charts.
- WEB-11 Today-relative views ("this quarter") and quick date-range presets.
- WEB-12 Multi-select bulk edit (shared-field inspector — deferred since v0.2).

## F. Web app — UX / polish
- WUX-01 Touch gestures (pinch-zoom, two-finger pan) polish.
- WUX-02 Keyboard a11y for all toolbar/command-palette actions; visible focus rings.
- WUX-03 Onboarding / empty-state guidance + sample templates surfaced.
- WUX-04 Inspector validation + inline error messages (e.g., end < start).
- WUX-05 Mini-map polish + overview zoom.
- WUX-06 Snapping options (to week/month) UI.
- WUX-07 Performance HUD/dev toggle for large charts.

## G. Shared foundation (spec / schema / parity)
- FND-01 (spec) Conformance breadth fixtures: empty doc, collapsed groups, all 4 dependency types, multi-row brackets, baseline drift, single-day task, very long horizon (F2).
- FND-02 Generalize `tests/unit/spec-conformance.test.ts` to iterate ALL fixtures (currently hardcoded to `basic-chart`).
- FND-03 Generate `src/types/document.ts` from `spec/schema/document.schema.json` (or assert equivalence in a test).
- FND-04 Wire the JSON Schema into runtime web validation (replace/augment `schema.ts`), incl. enforcing `end >= start` and hex `color` (SRS-ELEM-002 open item).
- FND-05 Web↔native **visual-vocabulary parity** (F2): reconcile summary bars, bracket style, axis density, weekend shading so both render the same; track via side-by-side renders.
- FND-06 Native conformance harness should validate the doc against the JSON Schema too (SRS-PERS-001 on the native side).
- FND-07 Author remaining visual figures for every fixture (`npm run gen:visual`) + annotate callouts keyed to requirement IDs.
- FND-08 Traceability check: a script that verifies every SRS requirement's Trace links resolve (files/sections/tests exist).
- FND-09 Schema versioning + a migration path story (currently hard-rejects non-v1).

## H. Data model / engine
- ENG-01 Calendar-aware durations (working-days vs calendar-days) option.
- ENG-02 Fiscal-aligned **year** axis boundaries (today calendar-aligned; SRS-TAX-011 open item).
- ENG-03 Holiday shading distinct from weekends (SRS-TAX-031 open item).
- ENG-04 Sub-day granularity (hours) behind a flag.
- ENG-05 Dependency lag/lead times.
- ENG-06 Layout perf: precompute date→day caches; avoid repeated `localeCompare` in hot loops.

## I. Performance & scalability
- PERF-01 Virtualize rendering for large charts (web SVG) — only render visible rows/bars.
- PERF-02 Native: batch shape creation / minimize OM round-trips; measure insert time for 200+ tasks.
- PERF-03 Benchmark suite for the layout engine (web + native) with large synthetic docs.
- PERF-04 Single-file HTML size budget check in CI (currently ~263KB).

## J. Tests — unit
- TST-01 Raise web unit coverage: bracket layout, marker layout, summary edge cases, collapsed groups, all dependency types (currently `basic-chart` only end-to-end).
- TST-02 Date utils edge cases: leap years, year boundaries, fiscal quarters across year start, DST-free UTC guarantees.
- TST-03 Critical-path: cycles, multiple components, all 4 dependency-type constraints, negative float.
- TST-04 Persistence: YAML round-trip property tests; HTML embed/parse round-trip; malformed-input rejection messages.
- TST-05 Inverse projection (native `DaysToDate`/reflow) unit tests independent of PowerPoint.
- TST-06 `GanttJson` (native) round-trip unit test for every fixture (not just basic-chart).
- TST-07 Snapshot tests for the SVG renderer output on the fixtures.

## K. Tests — E2E / integration
- E2E-01 Expand Playwright: drag-to-move, drag-resize, drag-to-connect, marquee select, undo/redo.
- E2E-02 Command palette flows for every command group.
- E2E-03 Export flows (SVG/PNG/print) produce non-empty, valid output.
- E2E-04 Autosave/restore banner; open/save round-trip via File menu.
- E2E-05 Theme switching (light/dark/print) visual snapshots.
- E2E-06 Embed renderer (`src/embed`) parity test vs the main app on the shared sample.
- E2E-07 Visual-regression CI (Playwright screenshots) for web; flag diffs.

## L. Tests — native conformance & harnesses
- NTS-01 Add a real C++ test framework (doctest/Catch2) for `GanttLayout`, `GanttJson`, inverse projection.
- NTS-02 Native conformance over ALL fixtures (today the harness skips fixtures lacking `*.expected.json`).
- NTS-03 Headless render test via an SVG/GDI+ backend (no PowerPoint dependency) for CI.
- NTS-04 Round-trip fuzz: random docs → insert → pull → equals.
- NTS-05 Reflow fuzz: random bar moves → reflow → dates match positions.
- NTS-06 Overlay coordinate test across DPI/zoom levels (assert `PointsToScreenPixels` mapping).

## M. Tooling / DevEx / CI
- CI-01 GitHub Actions: run `npm test` + `npm run lint` + `tsc -b` on PRs.
- CI-02 GitHub Actions: build the native DLL (windows runner, MSVC) + run the conformance harness.
- CI-03 GitHub Actions: build the single-file HTML and upload as an artifact; size check.
- CI-04 Regenerate `spec/visual/*` and fail if they drift from committed (visual-spec freshness gate).
- CI-05 Pre-commit hooks (lint/format/typecheck).
- CI-06 Normalize line endings (`.gitattributes`) — the repo currently warns LF→CRLF constantly.
- CI-07 `clang-format`/`.editorconfig` for the native C++.
- CI-08 A `native/CMakeLists.txt` (or .vcxproj/.sln) so the add-in opens in an IDE, alongside the batch scripts.
- CI-09 Make the GIF/screenshot generation a documented `npm run` target set for refreshing gallery assets.

## N. Docs
- DOC-01 Architecture overview diagram (spec → web → native) in the README/docs.
- DOC-02 Contributor guide (build web, build native, run tests, run harnesses).
- DOC-03 Native add-in developer guide: scene/renderer model, how to add a primitive, how reflow works.
- DOC-04 Keep `PLAN.md` and `docs/native-addin.md` in sync; cross-link the SRS.
- DOC-05 Troubleshooting expansion (add-in not loading, bitness, blocked DLL) — partly in install guide.
- DOC-06 Changelog/release notes automation.

## O. Accessibility & i18n
- A11Y-01 Screen-reader labels + roles across the web app chrome and SVG (title/desc).
- A11Y-02 Color-contrast audit for both themes (WCAG AA).
- A11Y-03 Full keyboard operability incl. drag alternatives (nudge keys exist; extend).
- I18N-01 Localizable strings (month names, UI labels) and locale-aware date formatting.
- I18N-02 RTL layout support.

## P. Cloud / web edition (Phase 8, not started)
- CLD-01 Firebase project + Firestore schema (users, documents, shares).
- CLD-02 Auth (email/password + Google); security rules.
- CLD-03 Cloud save + "My Charts" list; debounced autosave.
- CLD-04 Public read-only share links + embed iframe; view counter.
- CLD-05 Landing/SEO (OpenGraph, JSON-LD, sitemap, robots).
- CLD-06 Real-time collaboration (presence, CRDT/merge).

## Q. PowerNote / Office.js add-in
- PN-01 Keep the embeddable `<GanttRenderer>` in sync with the spec visual vocabulary.
- PN-02 Office.js add-in: reach feature parity with the native emission where the sandbox allows; document the ceiling.
- PN-03 Two-way PowerNote data binding (Gantt tasks referencing PowerNote entities).

## R. Reliability / error handling
- REL-01 Web: guard against malformed embedded data; friendly recovery UI.
- REL-02 Native: handle "no active window / wrong view / read-only" gracefully with clear messages.
- REL-03 Native: cap PP_DOC tag size; chunk or external-store large documents.
- REL-04 Defensive limits (max tasks/rows) with clear messaging.

## S. Security / privacy
- SEC-01 Sanitize any HTML/text injected into the single-file build.
- SEC-02 Native: validate/sanitize PP_DOC before parsing (untrusted slides).
- SEC-03 Document data-at-rest expectations (everything local unless cloud edition).
- SEC-04 Dependency audit (`npm audit`) cadence; pin/refresh.
