# PowerPlanner Product Requirements Document

## Executive Summary
PowerPlanner is a Gantt chart authoring tool built for people who actually present plans. It produces calendar-accurate, editable Gantt charts with the polish of a hand-built slide — task bars, milestones, brackets, dependency arrows, deadlines, today lines, percent-complete fills — while keeping every element directly editable by dragging or typing. PowerPlanner ships first as a **single-file portable HTML app** (offline, no install, no account), then expands into a hosted web app, a PowerNote node type, and a PowerPoint task-pane add-in that emits native, vector-editable shapes.

## Product Vision
Planning tools today force a choice: spreadsheet-driven Gantt apps that look like Gantt apps, or hand-drawn boxes in slide software that look great but break the moment a date moves. PowerPlanner removes that trade-off. The same chart is:
- **Calendar-true** — bars are positioned and sized by date, not by drag-and-drop guesswork.
- **Directly editable** — drag a bar to change its dates, or type a date to move the bar. No data sheet round-trips required.
- **Presentation-grade** — output is vector, sharp, and styled for slides and reports, not a "Gantt grid."
- **Portable** — the whole app fits in a single HTML file you can email, archive, or open offline. The same data model and renderer power every downstream surface (web, PowerNote, PowerPoint).

PowerPlanner is for project leads, consultants, PMOs, and engineers who present timelines often, hate retyping the same chart in three tools, and want their Gantt charts to be both correct and beautiful.

## Delivery Surfaces

PowerPlanner targets one engine across four surfaces. Each surface reuses the same data model, layout engine, and SVG renderer.

| Surface | Status | Description |
|---|---|---|
| **Portable HTML** | Primary (v0.x) | Single self-contained `PowerPlanner.html`. Open in any browser. Notes embedded in the file via Ctrl+S, mirroring PowerNote's model. |
| **Web app** | Phase 8 | Hosted edition with accounts, cloud persistence, sharing — mirrors PowerTimeline. |
| **PowerNote node** | Phase 6 | Gantt chart as an embeddable node inside a PowerNote canvas — same data model, rendered into PowerNote's Konva surface. |
| **PowerPoint add-in** | Phase 7 | Office.js task pane. Edits in PowerPlanner, "Insert into slide" emits native PowerPoint vector shapes that remain editable in PowerPoint without PowerPlanner installed. |

## Core Features

### Calendar & Time Scale
- Horizontal time axis with multi-level header (e.g., Year / Quarter / Month, or Year / Month / Week, or Month / Week / Day).
- Scales: day, week, month, quarter, year, fiscal year (custom start month).
- Auto-fit time range to data, or set explicit start/end dates.
- Zoom and pan along the time axis; snap to scale unit.
- Working-day calendar (skip weekends optional, custom holidays optional).
- Today line that updates automatically; pinnable static deadline lines.

### Tasks & Bars
- Each task is a row with a start date, end date, label, and optional metadata.
- Bars rendered as horizontal rectangles positioned and sized strictly by date.
- Drag the bar body to shift dates (preserve duration); drag bar edges to resize.
- Type a date in the side panel to update the bar; the bar moves.
- Percent-complete fill (0–100%) rendered inside the bar.
- Bar labels: on bar, left of bar, right of bar, or hidden — configurable globally and per task.
- Color per task, with named palette and theme-aware defaults.

### Milestones
- Diamond markers anchored to a single date.
- Label position configurable (above, below, left, right).
- Same drag/type editing model as bars.

### Brackets & Groups
- Brackets visually span a date range across one or more rows (phases, sprints, releases).
- Drag and resize like bars; auto-shrink to contained tasks (optional).
- Independent label and style.

### Dependencies & Connectors
- Arrow connectors between tasks (finish-to-start, start-to-start, finish-to-finish, start-to-finish).
- Created by dragging from one task's edge handle to another.
- Optional "enforce constraint" mode: moving the predecessor pushes the successor.

### Hierarchy
- Tasks organized into rows; rows organized into groups (summary tasks).
- Summary row span auto-derived from children (min start → max end).
- Collapse/expand a group to hide/show child rows.
- Indentation in the row label gutter.

### Markers & Annotations
- Today line (auto), deadline lines (manual, named, dated).
- Free-form text annotations anchored to a date or a task.
- Legend block (optional, auto-generated from used styles).

### Auto-Layout
- Vertical stacking: tasks within a row never visually overlap; rows expand to multiple sub-rows when bars conflict (configurable).
- Horizontal labels relocate when they'd collide with neighbors or the chart edge.
- Connector routing avoids bar overlap where possible.

### Direct Editing UX
- Click-to-select, drag-to-move/resize, double-click-to-edit-label.
- Side inspector panel with date pickers, color, label, percent complete, dependencies.
- Multi-select (shift-click, lasso) for batch edits.
- Undo/redo (per-document history).
- Keyboard shortcuts for create, delete, nudge, zoom, today-line toggle.

### Persistence & Export
- **In-file persistence** (portable HTML): Ctrl+S embeds the chart JSON into the HTML file itself, PowerNote-style.
- **Auto-save** to `localStorage` every 30s while editing.
- **Open file**: drag-drop or file picker for previously saved `.html` documents.
- **Import/export JSON** (canonical data format) and YAML (human-readable).
- **Export PNG and SVG** at any resolution; SVG preserves text as text.
- **Export PDF** for single-page chart sharing.
- **Export to PowerPoint** (Phase 7): native vector shapes, fully editable in PowerPoint after the add-in is uninstalled.

### Styling
- Theme: light / dark / print (high-contrast, no glow).
- Per-task style overrides; global style tokens for palette, fonts, line weights, bar height.
- Style presets ("Roadmap", "Consultant deck", "Engineering schedule").

## User Stories

### Chart Authoring
- As a user, I can create a new blank chart and add my first task in under 10 seconds.
- As a user, I can add a task by clicking on the canvas and typing a label and dates.
- As a user, I can drag a bar to change its dates and see the date inspector update in real time.
- As a user, I can type a new start date and watch the bar reposition exactly.
- As a user, I can add a milestone, give it a name, and anchor it to a date.
- As a user, I can group tasks into a phase with a bracket spanning their range.
- As a user, I can connect Task A's end to Task B's start with a dependency arrow.
- As a user, I can mark a task 60% complete and see the bar half-filled.

### Time Scale
- As a user, I can switch between Day / Week / Month / Quarter / Year scales without losing data.
- As a user, I can zoom in to inspect a sprint and zoom out to see a whole year.
- As a user, I can set a fiscal year start month and have all quarter labels follow.
- As a user, I can show or hide weekends and custom non-working days.

### Persistence & Portability
- As a user, I can download a single `PowerPlanner.html` file and start using the app immediately offline.
- As a user, I can press Ctrl+S and have my chart embedded inside the HTML file itself.
- As a user, I can email the saved file to a colleague who opens it and edits the chart with no install.
- As a user, I can export the chart as PNG, SVG, or PDF for inclusion in a report.

### Integration
- As a PowerNote user, I can drop a "Gantt" node onto my canvas and edit it inline.
- As a PowerPoint user, I can open the PowerPlanner task pane, edit my chart, and click "Insert into slide" to get a native, editable shape group.
- As a web user, I can sign in, save my charts to the cloud, and share a read-only link.

## Technical Architecture

### Frontend Layer
**Core Technologies:**
- React 19+ with TypeScript for type safety and modern concurrent features.
- Vite for fast development and a single-file production build (portable HTML target).
- SVG-based chart rendering for crisp vector output and high export quality.
- Tailwind CSS for app chrome and inspector panels (chart itself is hand-rendered SVG).

**Why SVG (not Canvas/Konva):**
- Gantt content is overwhelmingly rectangles, lines, and text — SVG's sweet spot.
- Text remains selectable and accessible.
- SVG export is a structural pass-through, not a rasterize step.
- Vector export to PowerPoint shapes maps SVG primitives 1:1.

**State Management:**
- Zustand for app state (chart document, viewport, selection, inspector state).
- One canonical chart document; renderer and inspector both read from it.
- Undo/redo via document snapshots (small documents make this cheap and bulletproof).

### Data Model
A single JSON document represents a chart. Versioned schema; designed for human readability and future Git-style diffs.

```jsonc
{
  "schemaVersion": 1,
  "title": "Q3 Launch Plan",
  "calendar": {
    "scale": "week",
    "fiscalYearStart": 1,
    "workingDays": [1, 2, 3, 4, 5],
    "holidays": ["2026-07-04"]
  },
  "rows": [
    { "id": "row-1", "label": "Design", "groupId": null }
  ],
  "tasks": [
    {
      "id": "task-1",
      "rowId": "row-1",
      "label": "Wireframes",
      "start": "2026-06-01",
      "end": "2026-06-14",
      "percentComplete": 40,
      "color": "indigo"
    }
  ],
  "milestones": [
    { "id": "ms-1", "rowId": "row-1", "label": "Design freeze", "date": "2026-06-15" }
  ],
  "brackets": [
    { "id": "br-1", "label": "Phase 1", "start": "2026-06-01", "end": "2026-07-31", "rowIds": ["row-1"] }
  ],
  "dependencies": [
    { "id": "dep-1", "from": "task-1", "to": "task-2", "type": "finish-to-start" }
  ],
  "markers": [
    { "id": "mk-1", "type": "deadline", "label": "Board review", "date": "2026-07-30" }
  ],
  "style": { "theme": "light", "preset": "default", "tokens": { } }
}
```

### Layout Engine
- Pure function: `(document, viewport) → renderable layout`.
- Computes pixel positions for the time axis, every bar, milestone, bracket, label, and connector.
- Handles label collision resolution (label position fallbacks: on-bar → right → left → above).
- Handles row sub-row splitting when bars in the same row overlap (configurable).
- Memoized; recomputes only when the document or viewport changes.

### Rendering
- Single React SVG component reads the layout output and emits primitives.
- Layered rendering: time axis → brackets → bars → milestones → connectors → markers → labels → selection chrome.
- No per-element React component for tasks until the document exceeds a threshold — flat primitive rendering scales further.

### Persistence Layer
**Portable HTML (v0.5.x):**
- File System Access API for save (Chrome / Edge); download fallback for other browsers.
- Document JSON serialized into a `<script type="application/json" id="powerplanner-data">` tag embedded in the HTML on save.
- On load, the app reads its own embedded data tag and hydrates.
- Mirrors PowerNote's self-contained-file pattern.

**Auto-save:**
- `localStorage` snapshot every 30s while editing; restore prompt on next launch.

**Web app (v0.8.x):**
- Firebase Firestore for document storage, mirroring PowerTimeline's model.
- One document per chart; sharing via signed URL.

### Export Pipeline
- **PNG**: rasterize the SVG via canvas at configurable DPI.
- **SVG**: serialize the rendered SVG directly (text stays as text).
- **PDF**: SVG → PDF via a small client-side library (no server round-trip).
- **PowerPoint (Phase 7)**: map SVG primitives to OOXML shapes via Office.js; bars → autoshapes, milestones → diamonds, connectors → connector shapes, text → text frames. Output remains editable in PowerPoint with no PowerPlanner runtime.
- **JSON / YAML**: direct serialization of the document.

### Integration Architecture

**PowerNote node (Phase 6):**
- A new PowerNote node type `gantt` whose payload is the PowerPlanner document JSON.
- PowerPlanner ships a "compact renderer" component (SVG, same renderer, smaller chrome) embeddable inside a PowerNote Konva `<Html>` portal.
- Editing in PowerNote opens the full PowerPlanner inspector as an overlay.

**PowerPoint add-in (Phase 7):**
- Office.js task pane add-in. The task pane hosts the same React app.
- "Insert into slide" walks the layout output and calls `Office.js` shape APIs to create native shapes on the active slide.
- The add-in also supports "Pull from slide" — reading a previously inserted shape group back into a PowerPlanner document via shape metadata tags.

**Web app (Phase 8):**
- React app deployed to Firebase Hosting.
- Firebase Auth (email/password + Google).
- Firestore for documents; storage rules per owner.
- Public read-only share links via signed URLs.

### Testing
- **Vitest** unit tests for the layout engine (date math, scale math, collision resolution, percent-complete math, dependency constraint propagation).
- **Playwright** E2E tests across desktop and mobile viewports for chart creation, editing, persistence, and export.
- **Visual regression** snapshots for the renderer at fixed documents/scales (light, dark, print themes).
- **SRS-style** per-feature requirements docs (`docs/SRS_*.md`) — mirrors the convention used across the other power* projects.

### Build & Distribution
- **Portable HTML build** (`vite.export.config.ts`): inlines all JS/CSS/assets into a single `PowerPlanner.html`. Same pattern as PowerNote.
- **Web build**: standard Vite production build, deployed to Firebase Hosting.
- **PowerPoint add-in build**: the same Vite app, packaged with an Office add-in manifest.
- **GitHub Releases**: each tagged version publishes the portable HTML asset.

## Success Criteria

### User Experience
- A new user creates and exports their first chart in under 5 minutes.
- Dragging a bar feels immediate — no perceptible lag at chart sizes up to 200 tasks.
- Date typed in the inspector → bar moves in the same frame.
- Saved `.html` files open on any modern browser with zero install.

### Functional
- Calendar math is correct: a 14-day task starting on a Monday ends on the right date in every scale.
- A chart authored at "Week" scale renders identically in pixel positions when viewed at "Day" scale (only the header changes).
- Exported PNG / SVG / PDF match the on-screen chart pixel-for-pixel (within rounding).
- A saved-then-reopened chart is bit-identical to the in-memory document.

### Performance
- Initial load (portable HTML): <2s on a cold cache, <500ms on warm cache.
- Layout recompute: <16ms for documents with 200 tasks (one frame).
- Drag/resize: 60fps for documents with 500 tasks.
- Export PNG of a 200-task chart at 2x DPI: <3s.

### Quality Gates
- Type-check passes (strict mode).
- Lint passes.
- All unit tests pass.
- Playwright suite passes across desktop / tablet / mobile projects.
- Smoke test (app launches, can create a task, can save, can reopen) is required before any release tag.

## Future Features

These are not committed but live on the roadmap once the core engine is stable:

### Excel / CSV Linking
- Bind tasks to rows of an external spreadsheet so chart updates when the source updates.

### Resource View
- Per-resource swimlanes; load balancing visualization.

### Baselines
- Save a baseline, then visualize drift between current schedule and baseline bars.

### Critical Path
- Compute and highlight the critical path through the dependency graph.

### Collaboration (Web edition)
- Real-time multi-cursor editing.
- Comments anchored to tasks.
- Sharing with view / comment / edit roles.
- Fork / merge workflow for chart variants (mirrors PowerTimeline).

### AI Assist
- "Draft a Gantt from this paragraph" — natural-language schedule extraction.
- "Compress the schedule by 2 weeks" — AI-suggested re-planning.
- (Reuses the Gemini integration pattern proven in PowerTimeline.)

### PowerNote Deep Integration
- Two-way data binding: a Gantt node embedded in a PowerNote page can reference tasks defined elsewhere in the notebook.

## Non-Goals

### Out of Scope
- Full project management suite (issue tracking, time tracking, billing, resourcing accounting).
- Server-side rendering of the chart (the renderer is client-only; this is intentional).
- Real-time collaboration in the portable HTML edition (web edition only).
- Mobile-native applications — responsive web is sufficient.
- Importing from proprietary project-management binary formats — JSON / YAML / CSV are the supported interchange formats.
- "Resource leveling" automated optimization — out of scope until baseline + critical path land.
- A first-party plugin marketplace.

### Explicitly Not a Replacement For
- Spreadsheet-driven project schedulers used for cost/time tracking. PowerPlanner is a **chart authoring** tool, not a project accounting tool.
