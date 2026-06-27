# PowerPoint add-in (v2.3.0 alpha)

PowerPlanner ships as an Office.js task-pane add-in for PowerPoint. The
add-in hosts the full editor inside a PowerPoint task pane and emits
**native, vector-editable shapes** into the active slide — not a
screenshot, not an embedded SVG.

## What works in this alpha

- Task pane mounts the full PowerPlanner editor inside PowerPoint
- "Insert into slide" emits per-task rectangles, per-milestone diamonds,
  dependency arrows (elbow connectors), and row-label text boxes
- All shapes are tagged (`PP_KIND`, `PP_ID`) for round-trip
- The full document JSON is stored on the group shape as `PP_DOC`
- "Pull from slide" reads the `PP_DOC` tag back into the editor

## What's deferred

- Brackets, baselines, today line, deadline markers (alpha emits tasks
  + milestones + dependencies + row labels only)
- AppSource submission

## Local setup

```bash
# 1. Install the dev cert that Office requires (run once per machine)
npm run addin:certs

# 2. Start the HTTPS dev server (add-in only) in one terminal
npm run dev:addin

# 3. In a separate terminal, sideload + launch PowerPoint
npm run addin:start

# Stop sideload when done
npm run addin:stop
```

`npm run dev` serves the main app over **HTTP** for everyday editing and
Playwright tests. The add-in manifest points at **HTTPS**, so use
`npm run dev:addin` when sideloading — it reuses the Office localhost cert
from `office-addin-dev-certs`.

### Troubleshooting

- **Blank task pane** — confirm `https://localhost:5180/taskpane.html` loads
  in a browser (you should see a “Browser preview” banner and the editor).
  If that works but PowerPoint is blank, run `npm run addin:stop` then
  `npm run addin:start` to refresh the sideload cache.
- **Insert/Pull disabled** — requires PowerPoint Desktop with **PowerPointApi
  1.4+** (Microsoft 365 builds from mid-2022 onward). The pane shows a
  warning when the API set is missing.
- **Runtime logs** — `%TEMP%\OfficeAddins.log.txt` on Windows.
- **Validate manifest** — `npm run addin:validate` (icons must be PNG).

## Sideload in PowerPoint Web

1. Open PowerPoint Online → **Home → Add-ins → More add-ins → Upload My
   Add-in**.
2. Pick `manifest.xml` from the repo root.
3. The PowerPlanner button appears on the Home ribbon.

## Production deployment

Host the built `dist/` (with `taskpane.html`) on any HTTPS-capable static
host. Update the manifest's URLs from `https://localhost:5180/...` to your
public origin, and the manifest's `<AppDomains>` to match. Distribute via:

- **Sideload** — send the manifest XML + a one-pager (simplest)
- **M365 admin center centralized deployment** — org-wide rollout
- **AppSource** — public marketplace listing (requires review)

## Architecture

```
manifest.xml                    Office add-in manifest (XML schema v1.1)
taskpane.html                   Office.js loader + React root
src/taskpane/main.tsx           Mounts <TaskPaneApp/> on Office.onReady
src/taskpane/TaskPaneApp.tsx    Wraps <App/> + insert/pull controls
src/taskpane/officeBridge.ts    Native shape emission + round-trip read
```

The bridge reuses PowerPlanner's `layoutDocument()` function unchanged,
just with `pxPerDay` interpreted as points-per-day. Slide coordinates are
in **points** (1pt = 1/72"), not EMU; a 16:9 slide is 960 × 540 pt.

## Round-trip protocol

Every inserted shape carries two tags:
- `PP_KIND` — `TASK` | `MILESTONE` | `DEP` | `ROW_LABEL` | `TITLE`
- `PP_ID` — the document ID of the originating entity

The enclosing group carries:
- `PP_KIND = CHART_ROOT`
- `PP_VERSION = 1`
- `PP_DOC = <full JSON document>`

"Pull from slide" finds the `CHART_ROOT` group and parses `PP_DOC`. If the
user ungroups the shapes manually, individual `PP_ID` tags survive — a
future iteration can use them as a fallback reconstruction path.
