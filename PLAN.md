# PowerPlanner Implementation Plan

## Quick Summary

**Current Version:** v0.4.0 — Layout & UX polish shipped
**Next Milestone:** v1.0.0 — Persistence & portable HTML release

### Recent Achievements
- ✅ v0.1.0 — Project scaffold, types, stores, layout engine, SVG renderer, sample document, 15 unit tests passing
- ✅ v0.2.0 — Bars & direct editing: snap-to-scale, nudge keys, inline label editing (F2/double-click), toolbar (Task/Milestone/Row/Snap/Fit)
- ✅ v0.3.0 — Gantt vocabulary: drag-to-create dependencies with connector overlay, bracket button (creates from selection), deadline marker button, summary rows from groupId with collapse/expand, free-form marker selection/deletion
- ✅ v0.4.0 — Layout & UX polish: axis label thinning, responsive row gutter, label collision fallback (on-bar → right), hover state on bars, empty state, light/dark/print themes, responsive toolbar (icon-only on narrow), print stylesheet, milestone label placement

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
- [ ] Serialize document to JSON (canonical, schemaVersion: 1)
- [ ] Deserialize with schema validation; clear error on mismatch
- [ ] YAML import / export (human-readable form)
- [ ] Drag-drop a `.json` / `.yaml` onto the canvas to load

### v0.5.1 — LocalStorage Auto-Save
- [ ] Snapshot to localStorage every 30s while editing
- [ ] "Restore previous session?" prompt on launch when a snapshot exists
- [ ] Manual "Clear local data" in inspector

### v0.5.2 — In-File Persistence (Embedded JSON)
- [ ] Embed document JSON into the HTML file via a `<script type="application/json" id="powerplanner-data">` tag on save
- [ ] On launch, read embedded data tag and hydrate
- [ ] File System Access API save (Chrome / Edge); download fallback elsewhere
- [ ] Ctrl+S binding

### v0.5.3 — Single-File Build
- [ ] `vite.export.config.ts` with inline-everything config
- [ ] Build script `npm run build:template` → `dist-template/PowerPlanner.html`
- [ ] Verify the built file opens directly (file://) and works offline
- [ ] Verify save/reopen round-trips byte-for-byte

### v0.5.4 — PNG / SVG / PDF Export
- [ ] "Export" menu: PNG (1x/2x/4x), SVG, PDF
- [ ] PNG via canvas rasterization of the SVG
- [ ] SVG via direct serialization (text remains text)
- [ ] PDF via a small client-side library
- [ ] Export bounds = chart bounds (no app chrome)

### v0.5.5 — Release v1.0
- [ ] All unit + E2E tests passing
- [ ] Smoke test: download HTML → open → create chart → save → re-open → export
- [ ] Tag v1.0.0
- [ ] Publish release with `PowerPlanner.html` asset
- [ ] README quick start updated

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
