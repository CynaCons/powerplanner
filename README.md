# PowerPlanner

**Calendar-true Gantt charts you can actually present.**

PowerPlanner is a Gantt chart authoring tool that produces presentation-grade timelines — task bars, milestones, brackets, dependency arrows, deadlines, today lines, percent-complete fills — with every element directly editable by dragging or typing.

It ships as a **single-file portable HTML app** — no install, no account, no server. Open the file in any modern browser and start planning.

## Get Started

1. Download **`PowerPlanner.html`** from the [latest release](https://github.com/CynaCons/powerplanner/releases/latest).
2. Open it in any browser (Chrome / Edge / Safari / Firefox).
3. Start editing.
4. Press **Ctrl+S** to save — your chart is embedded back into the same HTML file.

Share the file to share your chart. The recipient gets a fully editable copy with no install.

## Features

- **Calendar-true bars** — bars are positioned and sized strictly by date, not by guesswork.
- **Direct editing** — drag a bar to change its dates, drag the edges to resize, or type a date in the inspector.
- **Snap to scale** — drops align to day / week / month / quarter / year. Toggle on/off.
- **Milestones, brackets, dependencies, markers** — full Gantt vocabulary.
- **Drag-to-connect dependencies** — when a task is selected, drag from its edge handle to another task.
- **Summary rows** — rows with a `groupId` get an automatic summary bar spanning all child tasks.
- **Today line** + deadline markers, weekend shading, working-day calendar.
- **Time scales** — Day / Week / Month / Quarter / Year, with custom fiscal year start.
- **Auto sub-row stacking** — overlapping bars within a row split into sub-rows automatically.
- **Inline label editing** — double-click a bar (or F2) to rename.
- **Themes** — light / dark / print.
- **Export** — PNG, SVG, JSON, YAML; print/PDF; save embedded HTML.
- **Auto-save** — to localStorage every second while editing.
- **Self-contained** — the saved HTML file is both the editor and the data.
- **Offline-first** — no server, no cloud dependency.

## Keyboard Shortcuts

| Key | Action |
|---|---|
| `N` | New task |
| `M` | New milestone |
| `B` | New bracket (from selected tasks, or empty) |
| `Del` / `Backspace` | Delete selection |
| `F2` | Rename selected task |
| `←` / `→` | Nudge selection by 1 day (`Shift` = 7 days) |
| `+` / `−` | Zoom in / out |
| `Home` | Fit chart to data |
| `S` | Toggle snap-to-scale |
| `Esc` | Clear selection / cancel inline edit |
| `Ctrl+Z` / `Ctrl+Shift+Z` | Undo / Redo |
| `Ctrl+S` | Save (embeds chart into the HTML file) |

## How It Works

Each PowerPlanner file is a standalone HTML application. When you save, the chart is serialized to JSON and embedded inside a `<script type="application/json" id="powerplanner-data">` tag in the same HTML file. Reopen the file to continue editing. The renderer is pure SVG, so exports are crisp at any resolution.

## Roadmap

PowerPlanner is built so the same engine powers multiple surfaces:

- ✅ **Portable HTML** (this release)
- 🟡 **Web app** — hosted edition with accounts and sharing
- 🟡 **PowerNote node** — embeddable Gantt node inside [PowerNote](https://github.com/CynaCons/PowerNote)
- 🟡 **PowerPoint add-in** — Office.js task pane that emits native, vector-editable shapes

See [PLAN.md](PLAN.md) for the full roadmap and [PRD.md](PRD.md) for product details.

## Development

```bash
npm install
npm run dev              # Dev server at http://localhost:5173
npm run build:template   # Build standalone PowerPlanner.html → dist-template/
npm test                 # Unit tests (vitest)
npm run test:e2e         # Playwright E2E tests
```

## Stack

React 19 + TypeScript + Vite + SVG renderer + Zustand. Single-file build via `vite-plugin-singlefile`.

## License

MIT.
