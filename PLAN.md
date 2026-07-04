# PowerPlanner Implementation Plan

## Quick Summary

**Current Version:** v2.3.1 — PowerPoint add-in alpha (Office.js) + landing site + PowerNote integration
**Next Milestone:** N7 — Pure On-Slide Editor (V2): the add-in's overlay captures all input over the chart (no raw shape selection); think-cell-grade drag/create/edit interactions directly on the slide. Plan: [docs/on-slide-ux-plan.md §4](docs/on-slide-ux-plan.md)

### Recent Achievements
- ✅ v0.1.0 — Project scaffold, types, stores, layout engine, SVG renderer, sample document, 15 unit tests passing
- ✅ v0.2.0 — Bars & direct editing: snap-to-scale, nudge keys, inline label editing (F2/double-click), toolbar (Task/Milestone/Row/Snap/Fit)
- ✅ v0.3.0 — Gantt vocabulary: drag-to-create dependencies with connector overlay, bracket button (creates from selection), deadline marker button, summary rows from groupId with collapse/expand, free-form marker selection/deletion
- ✅ v0.4.0 — Layout & UX polish: axis label thinning, responsive row gutter, label collision fallback (on-bar → right), hover state on bars, empty state, light/dark/print themes, responsive toolbar (icon-only on narrow), print stylesheet, milestone label placement
- ✅ **v1.0.0** — Persistence & portable HTML PUBLIC RELEASE: schema validation, YAML serializer/parser with roundtrip tests, autosave to localStorage, embedded-data Ctrl+S, File menu (Save / Save as / Open / Export JSON/YAML/SVG/PNG / Print), single-file build → `PowerPlanner.html` with inlined favicon, Playwright E2E smoke tests, 22 unit tests + 3 E2E tests passing
- ✅ v1.0.1 — Replace blocking `confirm()` autosave restore with non-blocking RestoreBanner component (top-center, Restore + Dismiss)
- ✅ v1.1.0 — **Tool modes + bottom toolbar + click-to-create**: new `useToolStore` with Select/Add-Task/Add-Milestone/Marquee/Pan tools; PowerNote-style bottom toolbar (`ToolPalette`) with tool group + scale group (D/W/M/Q/Y) + zoom group (−/readout/+/fit); click on canvas with Add-Task or Add-Milestone tool creates at clicked date+row; lasso marquee selection; wheel = zoom (no Ctrl required, Figma-style); Linear-style single-letter shortcuts (V/T/Y/H/R for tools, Shift+D/W/M/Q/Y for scales); "TODAY" pill label on today line; full visual refresh (Inter typography, layered greys, 8% opacity borders, tabular numerals)
- ✅ v1.2.0 — **Direct manipulation**: right-click context menu on every chart element (background → Add task/milestone/deadline-here; task → Edit / Color / Duplicate / Wrap-in-bracket / Delete; milestone/bracket/dependency/marker → context-appropriate actions); dependency type switcher in dep context menu (FS/SS/FF/SF with check); muted Notion-inspired color palette swatches (8 presets + custom picker) in task inspector; ShortcutsOverlay modal (? to open) listing all keyboard bindings grouped by Tools/Scale/Selection/Editing/Navigation/File; HelpCircle button in header; right-click on unselected element replaces selection (desktop convention)
- ✅ v1.3.0 — **Visual identity overhaul** ("Engineering Atelier"): new BrandLogo (3 offset bars + Power/Planner wordmark, indigo→violet→coral gradient used ONLY on the brand mark); Header redesigned with title-as-pill chip + grouped button clusters (Add / View / History); Inspector rewritten as elevated section cards with Stripe-style inset top highlight, Segmented controls for Scale (D/W/M/Q/Y) and Theme (Dark/Light/Print), new Statistics section with mono-numeral tiles (Tasks / Milestones / Span / Avg complete) and date-range footer; updated E2E test for new segmented theme control
- ✅ v1.4.0 — **Command Palette + Templates + Notes**: Linear-grade Cmd+K command palette with fuzzy filter, 8 command groups (Create / Tools / Scale / View / Theme / File / Templates / Edit / Help), grouped sticky headers, arrow-key + Enter navigation, footer count; 5 templates (Product Launch, Two-Week Sprint, Hiring Plan, Marketing Campaign, Blank) loadable from the palette; Task `notes` field (textarea in inspector + schema validation); 9 new unit tests covering template integrity (schema validity, row references, dep references, date ordering); E2E test for Cmd+K command flow
- ✅ **v2.0.0** — **PRO features + v2 PUBLIC RELEASE**: full Critical Path Method (forward/backward pass, 4 dependency types, per-component scoping, cycle detection, O(V+E)) with 7 unit tests, red-glow highlight when toggled; Baseline snapshot + drift visualization (capture current schedule, show as translucent rail under bars); Minimap overview rail at chart bottom (compact bars + viewport indicator, click-to-pan); commands wired into palette (View: critical path / baseline / minimap toggles; Edit: capture/clear baseline); new useViewStore for view toggles; README rewritten for v2; 38 unit + 4 E2E tests all passing
- ✅ v2.1.0 — Landing page at `cynacons.github.io/powerplanner` (Engineering Atelier Editorial Edition): hand-crafted animated SVG Gantt hero, stat strip with count-up numerals, 4-step how-it-works, 3×3 feature grid, 3-card comparison fold, 12-row shortcuts showcase, pricing strip with gradient word, GitHub Actions auto-deploy from `/site`
- ✅ v2.2.0 — Embeddable Gantt renderer (`src/embed/` barrel) + PowerNote integration: self-contained `<GanttRenderer document width height options>` with no store coupling; vendored into PowerNote at `src/vendor/powerplanner/`; new `'gantt'` node type, `GanttNode.tsx`, NavRail button, click-to-place handler creating a sample chart
- ✅ v2.3.0 — PowerPoint add-in (alpha): Office.js manifest XML, second Vite entry (`taskpane.html`), `src/taskpane/{main,TaskPaneApp,officeBridge}.tsx`; "Insert into slide" emits native rectangles for tasks, diamonds for milestones, elbow connectors for dependencies, text boxes for row labels + title; full JSON round-trip via `PP_DOC` tag on chart group; npm scripts `addin:certs/start/stop/validate`; docs at `docs/powerpoint-addin.md`
- ✅ v2.3.1 — Add-in dev ergonomics: split `npm run dev` (HTTP) vs `npm run dev:addin` (HTTPS + Office cert); taskpane browser fallback mount + PowerPointApi 1.4 runtime check; PNG ribbon icons + manifest `<Requirements>`; compact taskpane layout; troubleshooting section in `docs/powerpoint-addin.md`

### Key Objectives
- Ship a single-file portable HTML Gantt authoring tool (PowerNote-style distribution).
- Calendar-true bars, milestones, brackets, dependencies, today line.
- Direct editing: drag a bar to change dates, type a date to move a bar.
- Vector export (SVG / PNG / PDF) of presentation quality.
- One engine, four surfaces over time: portable HTML → web app → PowerNote node → PowerPoint add-in.

### Stack
- React 19 + TypeScript + Vite.
- SVG renderer (no Canvas/Konva for the chart itself).
- Zustand for state, Tailwind for app chrome.
- Vitest unit tests, Playwright E2E tests.
- Single-file portable HTML build (`vite.export.config.ts`), same pattern as PowerNote.

### Quick Links
- [Product Requirements](PRD.md)
- [README](README.md)
- [Specification (concept layer)](spec/README.md)
- [Native add-in notes](docs/native-addin.md)

---

## Architecture: one spec, two implementations

PowerPlanner is a **concept** with two **implementations**. The concept lives in
`spec/` (language-neutral); each implementation realizes it in its own medium.

```
spec/      ← the concept (source of truth): data-model, layout, visual-vocabulary,
             interaction, JSON Schema, conformance fixtures
src/       ← implementation A — WEB (React 19 + TS + SVG). The reference impl.
native/    ← implementation B — POWERPOINT (C++ COM add-in, think-cell style)
```

- **Abstract coordinates.** `spec/layout.md` defines layout in days + row slots,
  never pixels/points. Web maps day→px (`pxPerDay`); PowerPoint maps day→pt
  (`ptPerDay`). Device mapping is each implementation's final, owned step.
- **Conformance is the contract.** `spec/fixtures/` pairs canonical documents with
  expected abstract layout. Web verifies via `tests/unit/spec-conformance.test.ts`;
  native verifies the same fixtures against its C++ layout port. Same inputs →
  same outputs keeps the two from drifting.
- **The web app is the reference.** When spec and web disagree, that's a bug to
  reconcile in one of them — neither is free to drift from the other.
- `src/` and `native/` stay where they are (no `web/` move for now); the focus is
  the PowerPoint implementation, built spec-first.

---

## Format Rules

- Iterations: version number + brief title.
- Goal: one-line objective statement (optional).
- Status: Complete / In Progress (only if in progress).
- Tasks: simple checkbox items only (no sub-bullets, no implementation details).
- NO "Files Modified" sections (use git for that).
- NO "Impact" sections (tasks describe the work).
- NO verbose summaries.
- Close iterations chronologically — don't skip ahead.
- Move incomplete tasks to future iterations, don't leave them in closed ones.
- Update PLAN.md in real time as tasks complete.
- Smoke test before tagging any release: app launches without crashes, can create a task, can save, can reopen.

---

# Phase 1: Foundation (v0.1.x)
**Goal:** App shell, data model, calendar header rendering. No tasks yet — just the canvas, the scale, and the inspector frame.

### v0.1.0 — Project Scaffold
- [x] Initialize Vite + React + TypeScript project
- [x] Configure strict TypeScript, ESLint
- [x] Install Zustand, lucide-react
- [x] Set up Vitest config and unit tests
- [x] Set up Playwright config with desktop / tablet / mobile projects
- [x] Folder structure: `src/{types,stores,layout,renderer,app,utils,styles}`
- [x] AppShell: header | left inspector | chart canvas | bottom status
- [x] Smoke test: `npm run dev` launches cleanly

### v0.1.1 — Data Model & Stores
- [x] Define `Document`, `Task`, `Milestone`, `Bracket`, `Dependency`, `Marker`, `Row`, `Calendar`, `Style` types
- [x] `useDocumentStore` — document state + CRUD reducers + undo/redo
- [x] `useViewportStore` — scale, time range, pixel-per-day, pan/zoom
- [x] `useSelectionStore` — selected ids, multi-select
- [x] `utils/dates.ts` — date math (addDays, diffDays, snapTo, fiscalQuarterOf)
- [x] `utils/ids.ts` — nanoid wrapper
- [x] Sample document factory (6-task, 2-milestone chart for development)
- [x] Unit tests for dates and layout engine

### v0.1.2 — Calendar Header Rendering
- [x] SVG `<TimeAxis>` component: multi-level header
- [x] Scale switcher: day / week / month / quarter / year
- [x] Tick positioning math in `layout/timeAxis.ts`
- [x] Today line (auto, dashed)
- [x] Fiscal year start month configurable
- [x] Weekend stripes in day/week scales

### v0.1.3 — Chart Canvas + Pan/Zoom
- [x] SVG `<ChartArea>` fills the chart area (ResizeObserver)
- [x] Horizontal pan via drag on background
- [x] Zoom via Ctrl+wheel along the time axis, pointer-anchored
- [x] Shift+wheel horizontal pan
- [x] Clamp zoom to a sensible day-to-year range
- [x] Auto-fit to data on first load + manual Fit button

### v0.1.4 — Inspector Panel Frame
- [x] `<Inspector>` left panel: Document section with title + scale + fiscal year + theme
- [x] Selection tab populates with task / milestone / bracket fields
- [x] Wire title and scale to document/viewport stores

---

# Phase 2: Bars & Direct Editing (v0.2.x)
**Goal:** Task bars rendered by date. Drag to move. Drag edges to resize. Type a date and the bar moves.

### v0.2.0 — Row & Task Rendering
- [x] `<RowGutter>` — left column with row labels
- [x] Row vertical layout with sub-row stacking for overlaps
- [x] `<TaskBar>` SVG primitive — rectangle positioned by date math
- [x] Bar label rendered on the bar

### v0.2.1 — Selection & Inspector Wiring
- [x] Click bar to select; click background to deselect
- [x] Selection chrome (outline, edge handles)
- [x] Inspector "Selection" tab with label, row, start, end, percent, color
- [x] Editing any field updates the document; bar re-renders

### v0.2.2 — Drag to Move
- [x] Mouse down on bar body → drag → release
- [x] Drag math converts pixel delta to day delta via current scale
- [x] Snap to scale unit configurable (toggle in toolbar)
- [x] Document update on release; live preview while dragging
- [x] Undo entry created per drag

### v0.2.3 — Drag to Resize
- [x] Left and right edge handles
- [x] Resize updates start or end, never both
- [x] Minimum duration enforced (end ≥ start)

### v0.2.4 — Type-to-Move
- [x] Date inputs in inspector (HTML date type)
- [x] Typing a new start moves bar
- [x] Typing a new end resizes
- [x] Validation against bar end vs start

### v0.2.5 — Percent Complete
- [x] Inspector slider 0–100
- [x] Bar renders a darker filled inset for percent complete

### v0.2.6 — Multi-Select & Nudge
- [x] Shift-click adds to selection
- [x] Arrow keys nudge selection by 1 day; Shift+arrow = 7 days
- [ ] Lasso-rectangle on background to select (deferred to v0.4.x)
- [ ] Inspector "Multiple selection" mode for shared fields (deferred)

### v0.2.7 — Undo / Redo
- [x] Document history stack
- [x] Ctrl+Z / Ctrl+Shift+Z

### v0.2.8 — Inline Label Editing
- [x] Double-click bar to edit label inline (foreignObject)
- [x] Enter to commit, Escape to cancel
- [x] F2 to edit selected task label

### v0.2.9 — Toolbar Polish
- [x] Add Task button (N)
- [x] Add Milestone button (M)
- [x] Add Row button
- [x] Snap toggle button (S)
- [x] Fit-to-data button (Home)

---

# Phase 3: Gantt Vocabulary (v0.3.x)
**Goal:** Milestones, brackets, dependencies, markers. The full vocabulary of a real Gantt chart.

### v0.3.0 — Milestones
- [x] Diamond marker primitive
- [x] Anchored to a single date on a row
- [x] Drag to move; type date in inspector to move
- [x] Add via toolbar button (M)

### v0.3.1 — Brackets
- [x] Horizontal bracket primitive spanning a date range
- [x] Anchored to one or more rows
- [x] Drag body to shift, drag edges to resize
- [x] "Create bracket from selection" (auto-span to selected tasks)

### v0.3.2 — Dependency Arrows
- [x] Edge handles on bar (visible when selected) reveal "drag-to-connect" affordance
- [x] Dragging from one handle to another bar creates a dependency
- [x] Live preview line during drag (dashed accent color)
- [x] Drop on task to create dependency (FS or SS depending on origin handle)
- [x] Click dependency to select, Delete to remove
- [ ] Constraint enforcement (deferred to v0.5+)

### v0.3.3 — Custom Markers
- [x] Deadline lines: vertical line + label, anchored to a date
- [x] Add via toolbar button
- [x] Selectable and deletable

### v0.3.4 — Hierarchy (Summary Rows)
- [x] Rows with groupId belong to a parent row
- [x] Parent row renders a summary bar = min(start) → max(end) of children
- [x] Indentation in row gutter
- [x] Collapse / expand chevron in row gutter

---

# Phase 4: Layout & UX Polish (v0.4.x) — COMPLETE

### v0.4.0 — Auto Sub-Row Splitting
- [x] When two bars in the same row overlap, split the row into sub-rows
- [x] Sub-row assignment is deterministic (sorted by start, greedy fit)
- [x] Row height grows to fit all sub-rows

### v0.4.1 — Label Collision Resolution
- [x] On-bar → right fallback when label doesn't fit on bar
- [x] Milestone label placement (above/below/left/right)
- [x] Axis tick thinning (min spacing 60px/36px/16px)

### v0.4.2 — Working Days & Holidays
- [x] Calendar settings: weekend mask
- [x] Non-working days shaded in the chart background

### v0.4.3 — Theming
- [x] Light, dark, and print themes
- [x] Theme tokens in CSS variables
- [x] Inspector "Style" tab: theme picker
- [x] Per-task color override

### v0.4.4 — Keyboard Ergonomics
- [x] N — new task
- [x] M — new milestone
- [x] B — new bracket from selection
- [x] Delete / Backspace — remove selection
- [x] +/− — zoom in/out
- [x] Home — fit to data
- [x] F2 — edit task label
- [x] Esc — clear selection / cancel drag
- [x] S — toggle snap

### v0.4.5 — Responsive Layout
- [x] Responsive row gutter (200 → 120 on narrow)
- [x] Responsive inspector (320 → 260 → hidden on very narrow)
- [x] Toolbar icon-only mode at <900px
- [x] Print stylesheet (hide chrome, chart only)
- [x] Empty state when chart has no items
- [x] Hover effect on bars

---

# Phase 5: Persistence & Portable HTML (v0.5.x)
**Goal:** Ship the single-file portable HTML edition. Email it, archive it, open it offline.

### v0.5.0 — JSON / YAML Persistence
- [x] Serialize document to JSON (canonical, schemaVersion: 1)
- [x] Deserialize with schema validation (`SchemaError` on mismatch)
- [x] YAML import / export (human-readable form with custom parser)
- [x] File picker for `.html` / `.json` / `.yaml` open

### v0.5.1 — LocalStorage Auto-Save
- [x] Snapshot to localStorage debounced (800ms) while editing
- [x] "Restore previous session?" prompt on launch when a snapshot exists

### v0.5.2 — In-File Persistence (Embedded JSON)
- [x] Embed document JSON into HTML via `<script type="application/json" id="powerplanner-data">` tag on save
- [x] On launch, read embedded data tag and hydrate
- [x] File System Access API save (Chrome / Edge); download fallback elsewhere
- [x] Ctrl+S binding

### v0.5.3 — Single-File Build
- [x] `vite.export.config.ts` with `vite-plugin-singlefile`
- [x] Inline favicon as data URI (truly self-contained)
- [x] Auto-rename output → `PowerPlanner.html`
- [x] Build script `npm run build:template`
- [x] Verified opens directly (file://) and works offline
- [x] Output size: 263 KB (81 KB gzipped)

### v0.5.4 — PNG / SVG / PDF Export
- [x] File menu: Save, Save as, Open, JSON / YAML / SVG / PNG (1x, 2x) / Print
- [x] PNG via canvas rasterization of the SVG
- [x] SVG via direct serialization with inlined computed styles
- [x] PDF via browser print stylesheet (chart-only)

### v0.5.5 — Release v1.0 🎉
- [x] All unit + E2E tests passing (22 unit + 3 E2E)
- [x] Smoke test: portable HTML → open → render → all features work
- [x] README rewritten with feature list + keyboard shortcuts
- [x] Tag v1.0.0
- [x] Publish release with `PowerPlanner.html` asset

---

# Phase 6: PowerNote Integration (v0.6.x)
**Goal:** Gantt as an embeddable node type inside PowerNote.

### v0.6.0 — Compact Renderer Component
- [ ] Extract `<GanttRenderer document={...} viewport={...}>` as a standalone package export
- [ ] Smaller chrome, no app shell, fits a PowerNote node bounding box
- [ ] Read-only mode prop

### v0.6.1 — PowerNote Node Type
- [ ] Add `gantt` node type to PowerNote
- [ ] Payload = PowerPlanner document JSON
- [ ] Render via the compact renderer inside a Konva `<Html>` portal

### v0.6.2 — Inline Edit Bridge
- [ ] Double-click the node in PowerNote opens the full PowerPlanner inspector as a PowerNote overlay
- [ ] Saving the inspector writes back into the PowerNote node payload
- [ ] Undo/redo integrates with PowerNote history

### v0.6.3 — Test Coverage
- [ ] PowerNote E2E test: create page → add Gantt node → edit → save notebook → reopen
- [ ] Cross-repo coordination (PowerNote release notes updated)

---

# Phase 7: PowerPoint Add-In (v0.7.x)
**Goal:** Task pane add-in that emits native, vector-editable PowerPoint shapes.

### v0.7.0 — Office.js Add-In Scaffold
- [ ] Add-in manifest (taskpane host)
- [ ] Same Vite app served as the task pane
- [ ] Verify launch in PowerPoint desktop and PowerPoint web

### v0.7.1 — Insert Into Slide
- [ ] "Insert into slide" button in the task pane
- [ ] Walk the layout output; emit Office.js shapes (rectangles for bars, diamonds for milestones, connectors for dependencies, text frames for labels)
- [ ] Group all emitted shapes under a single named group with PowerPlanner metadata tags

### v0.7.2 — Pull From Slide
- [ ] If the active slide contains a PowerPlanner shape group, "Pull from slide" reads the metadata and reconstructs the document into the task pane
- [ ] Round-trip test: insert → modify in PowerPoint → pull → re-insert is stable

### v0.7.3 — Native Editability Guarantee
- [ ] Inserted shapes remain editable in PowerPoint after the add-in is uninstalled
- [ ] No images, no embedded SVGs — native PPT primitives only
- [ ] Verified across desktop PowerPoint and PowerPoint web

---

# Phase 8: Web / Cloud Edition (v0.8.x)
**Goal:** Hosted edition with accounts, cloud persistence, and link sharing — mirrors PowerTimeline.

### v0.8.0 — Firebase Setup
- [ ] Create Firebase project (dev + prod)
- [ ] Firestore schema: users, documents, shares
- [ ] Firebase Auth: email/password + Google
- [ ] Security rules: owner full access, share roles for read / comment

### v0.8.1 — Cloud Persistence
- [ ] "Save to cloud" alongside "Save to file"
- [ ] Document list page (My Charts)
- [ ] Auto-save with debounce on edits

### v0.8.2 — Sharing
- [ ] Public read-only share link (signed URL)
- [ ] Embed iframe option
- [ ] View counter (denormalized for performance)

### v0.8.3 — Landing Page & SEO
- [ ] Landing page mirroring PowerTimeline / PowerNote style
- [ ] OpenGraph meta tags, JSON-LD structured data
- [ ] robots.txt + sitemap

---

# Phase F: Foundation Layer (ASPICE-style specification) — IN PROGRESS
**Goal:** The foundation is the single source of truth: a set of ASPICE-like
software requirement specs (`spec/srs/SRS-<feature>.md`, requirement tables) plus
a visual specification, backed by the JSON Schema and conformance fixtures. Both
implementations — web (`src/`) and native (`native/`) — derive from it and are
traceable to it. See [spec/README.md](spec/README.md).

### F0 — Concept foundation — COMPLETE
- [x] `spec/data-model.md` — entities, fields, invariants (mirrors `src/types/document.ts`)
- [x] `spec/layout.md` — projection algorithm in abstract day/row-slot coordinates
- [x] `spec/visual-vocabulary.md` — element shapes, proportions, color, label rules
- [x] `spec/interaction.md` — editing model incl. the future on-slide PPT UI
- [x] `spec/schema/document.schema.json` — canonical JSON Schema (mirrors `schema.ts`)
- [x] `spec/fixtures/basic-chart.*` — golden document + expected abstract layout
- [x] Web conformance test (`tests/unit/spec-conformance.test.ts`) passing

### F1 — SRS specification (ASPICE) — COMPLETE
- [x] Define feature decomposition + requirement-table format (`spec/srs/README.md`)
- [x] Author all 9 `SRS-<feature>.md` files (chart-elements, time-axis, layout, editing,
      persistence, critical-path, baseline, theming-export, powerpoint) with traceable requirements
- [x] Trace each requirement to design (`spec/*.md`), the JSON Schema, fixtures, and tests
- [x] Visual specification approach decided + pipeline working: generated from the web
      renderer (`scripts/gen-visual-spec.ts` → `spec/visual/*.svg`, `npm run gen:visual`)
- [x] Independent ASPICE audit (2 review rounds): fixed code-accuracy mismatches (end≥start
      enforcement, fiscal-year ticks, color/Hex), gaps, and traces; residual gaps disclosed in open items
- [x] Acceptance met: every foundation feature has an SRS of verifiable, traceable requirements

### F2 — Conformance breadth (later)
- [ ] More fixtures: empty doc, collapsed groups, all four dependency types, multi-row brackets, baseline drift
- [ ] Generate `src/types/document.ts` types from the JSON Schema (or assert equivalence in a test)
- [ ] Wire the JSON Schema into web persistence validation (replace/augment hand-written `schema.ts`)
- [ ] **Visual-vocabulary parity (web ↔ native):** layout already conforms, but rendering
      diverges — web shows week ticks + weekend stripes and skips the summary bar; native
      shows month-only gridlines + a grey summary bar; brackets differ (web line vs native box).
      Reconcile against `visual-vocabulary.md`. Track via side-by-side renders
      (`scripts/rasterize-visual.mjs` web, `native/build-render.bat` native, both on `sample-q3`).

---

# Phase 9: Native PowerPoint Add-In (C++ COM) — IN PROGRESS
**Goal:** A native in-process COM add-in for PowerPoint desktop in the style of
think-cell: the Gantt chart lives on the slide as native shapes, and the editing
UI is drawn contextually over the slide (not a task pane). Windows-only, separate
codebase under `native/`. The Office.js add-in remains as a cross-platform
fallback. Architecture + roadmap in [docs/native-addin.md](docs/native-addin.md).

### N1 — Loadable COM Add-in Skeleton — COMPLETE
- [x] ATL in-proc COM DLL project under `native/` (x64, per-user registration)
- [x] `IDTExtensibility2` lifecycle; cache `PowerPoint.Application` on connect
- [x] `IRibbonExtensibility` "PowerPlanner" ribbon tab with an "Insert Gantt" button
- [x] Ribbon callbacks routed through hand-implemented `IDispatch`
- [x] `build.bat`/`build.ps1` (compile/register/unregister); DLL loads in PowerPoint, button reaches native code (verified: ribbon tab renders, click fires `DoInsertGantt`)

### N2 — Native Shape Emission — COMPLETE
- [x] Split `GanttModel` (UTF-8 data) + `GanttLayout` (pure spec/layout.md in abstract
      day/row-slot units, no COM) out of the freehand port
- [x] C++ conformance harness passes `spec/fixtures/` at scale = 1 — matches the web golden
      exactly (`native/conformance/`, `build-conformance.bat`)
- [x] Emitter maps abstract layout → slide points; emits tasks (rounded rects), summary
      bars, milestones (diamonds), brackets, dependency elbows, row labels + title as native shapes
- [x] Shapes grouped under a tagged `CHART_ROOT`; tagged `PP_KIND`/`PP_ID`
- [x] Verified end-to-end: render harness (`native/render/`) drives PowerPoint via automation,
      emits the chart, exports the slide to PNG (19 shapes, correct layout)

### N3 — Data Model + Round-Trip — COMPLETE
- [x] Shared JSON layer (`GanttJson`): canonical serialize + parse; round-trip stable
      (conformance harness asserts `canonical(parse(canonical)) == canonical`; nlohmann/json)
- [x] Insert embeds the serialized document on the chart root (`PP_DOC` + `PP_VERSION` tags)
- [x] `ReadGanttFromSlide` reads `PP_DOC` back; "Pull from slide" ribbon button parses + reports it
- [x] Verified: render-harness slide round-trip PASS (insert → read `PP_DOC` → parse → re-serialize == original, 1756 chars)
- [ ] (follow-on) Load an external document via a file picker instead of the built-in sample

### N4 — On-Slide Contextual UI — COMPLETE (core)
- [x] Layered, click-through overlay window (`Overlay.cpp`) drawn over the slide
- [x] Aligned to the selected chart via `DocumentWindow::PointsToScreenPixelsX/Y` (zoom/scroll-correct)
- [x] Selection-driven via a 150ms polling timer: shows our frame + handles + "PowerPlanner" badge for a
      selected PowerPlanner shape (PP_KIND), inert/hidden otherwise; re-syncs (tracks zoom/scroll/selection)
- [x] Verified: `overlay-test` harness drives PowerPoint, selects the chart, screen-captures the overlay
- [ ] (→ N5) Contextual *interactive* controls (clickable add-task / drag-dates) — needs hit-testing + editing

### N5 — Live Linkage ("Agents") — COMPLETE (core)
- [x] Inverse projection (`DaysToDate` + stored `PP_PROJ` tag): shape position → dates
- [x] `ReflowFromSlide`: reads each task bar's position back into dates, updates the document,
      and reflows (re-emit) so dependent connectors/summary and `PP_DOC` stay in sync
- [x] "Reflow" ribbon button (`OnReflowGantt`)
- [x] Verified: `reflow-test` moves a bar +14 days → dates shift exactly +14 days, connectors
      reflow, `PP_DOC` updated (REFLOW PASS); reflowed chart exported
- [x] (follow-on) Auto-reflow on the polling timer (detect a committed edit and reflow without the button)
- [x] (UI Polish) Think-cell style U-brackets, zero margins, Segoe UI typography, and deadline/today line markers in native renderer

### N5.5 — On-Slide Contextual Editing (V1) — COMPLETE (2026-06-30)
Delivered via the `onslide-coordinator` loop (13 gated units, log at
[docs/on-slide-coordinator-log.md](docs/on-slide-coordinator-log.md)):
- [x] Native right-click Gantt menu (add/delete/nudge/%/scale), floating mini-toolbar,
      row-hover highlight + "+" insert, double-click inline title/row-label editing,
      chart-wide Material overlay, pure document-ops engine (`GanttOps`) with headless
      test seam, auto-reflow on drag release
- [x] Key architecture finding: no subclassable per-slide window → all interaction
      runs through our overlay + 150ms poller (`FALLBACK_POLLING_ONLY`)

### N7 — Pure On-Slide Editor (V2, think-cell interaction capture) — COMPLETE (2026-07-04)
The chart's shapes are now a render target only: the overlay captures ALL mouse input
over the chart, suppresses PowerPoint-native selection of chart internals, and
provides the full think-cell interaction set. 15 gated units (10 planned + 2
discovery spikes + 3 review-driven fix units), each committed with `[todo: <id>]`;
10-stage COM harness green from clean rebuild. Log:
[docs/on-slide-coordinator-log.md](docs/on-slide-coordinator-log.md), plan:
[docs/on-slide-ux-plan.md §4](docs/on-slide-ux-plan.md)
- [x] U1 capture-surface · U2 selection-suppression · U3 alpha-overlay
- [x] U4 own-selection-model · U5 drag-move-resize · U6 drag-row-and-create
- [x] U7 rebuild-in-place (one gesture = one undo) · U8 floating-editor
- [x] U9 keyboard-and-cursors (scoped hotkeys) · U10 dpi-and-monitors
- [x] + overlay-context-menu (semantic right-click), fix-capture-hardening,
      fix-reconcile-robustness, disco-undo-entry (GROUPING_WORKS),
      disco-keyboard-focus (HOTKEY)
- [ ] Follow-ups (small): milestone recolor op; edge/milestone drag harness
      coverage; manual DPI matrix run (plan §4.2.1); TrackPopupMenu flicker check

### N8 — Row-Centric Editing + Bottom App Bar (V3) — IN PROGRESS (started 2026-07-04)
User-feedback iteration on V2. Decisions: app bar = primary contextual surface with
right-click as shortcut (one shared command map) · uniform rows with optional
hierarchy · generic movable markers · fit-to-slide under a reserved title zone.
Plan: [docs/on-slide-ux-plan.md §6](docs/on-slide-ux-plan.md); coordination via
the same gated sub-agent loop as N7 (log: docs/on-slide-coordinator-log.md).
- [x] V3-1 fit-to-slide — done f2d9796 + fix-fit-persistence b7ca884 (frame
      survives edits; uniform scale so text never stretches)
- [x] V3-2 marker-model-ops (e795ef7) · V3-3 marker-drag (d4eae41) — movable Today/deadline/custom lines shipped
- [ ] V3-4 text-model · V3-5 text-interaction (free + anchored text elements)
- [ ] V3-6 label-placement (on-bar vs left-rail task labels)
- [ ] V3-7 row-uniform-ux (add/indent/outdent — everything is a row)
- [ ] V3-8 grid-scale-options (five real scales Y/Q/M/W/D + separator density/style)
- [ ] V3-9 appbar-shell · V3-10 appbar-actions (bottom contextual app bar)
- [ ] V3-11 dependency-ux (link mode from app bar)
- [ ] V3-12 context-menu-v3 (menus rebuilt from shared command map)
- [x] V3-13 fix-overlay-scoping (BUG: chrome floats over other apps — hide unless
      PowerPoint foreground) — done, e81c182
- [ ] V3-14 material-theme (Material 3 palette shared by shapes + chrome)
- [x] V3-15 harness-input-isolation (input-neutral + fully offscreen gates) — done, 2ce3c9d

### N6 — Installer + Packaging
- [ ] WiX/MSI per-user installer, COM registration, ribbon icons

---

# Backlog (Unscheduled)

These are confirmed-on-the-roadmap-but-not-yet-scheduled items. Promote into a phase above when prioritized.

- Excel / CSV linking (bind tasks to external spreadsheet rows)
- Resource swimlanes view
- Baselines (snapshot + drift visualization)
- Critical path computation and highlight
- Real-time collaboration (web edition)
- Comments anchored to tasks
- AI assist (Gemini): "Draft a Gantt from this paragraph", "Compress the schedule by 2 weeks"
- Fork / merge workflow for chart variants
- Two-way PowerNote data binding (Gantt tasks referencing PowerNote-defined entities)
- Style preset gallery
- Touch gestures (pinch zoom, two-finger pan) polish
- Print stylesheet for browser print
