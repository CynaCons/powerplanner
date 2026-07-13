# README Visual Uplift — Plan (for grok build)

**Goal:** Refresh the GitHub README so it showcases PowerPlanner's *latest and most
beautiful* visuals — the curated `gallery-*.png` cards rendered 2026-07-12 — instead
of the older, smaller `native-v5-*` app-bar crops. This is a **docs-only** change:
`README.md` + `docs/media/` assets. No source, spec, or native code changes. Do **not**
commit or push — the coordinator gates and commits.

## Background / current state (from asset survey)

- `README.md` has a `## Gallery` section with two subsections: **Web app** and
  **PowerPoint add-in (native, on-slide editor)**. It currently references 8 images,
  all under `docs/media/`:
  - `web-demo.gif`, `web-app.png` (web)
  - `native-v5-chart.png`, `native-v5-taskbar.png`, `native-v5-rowbar.png`,
    `native-v5-milestonebar.png`, `native-v5-settings.png` (native)
  - `native-reflow.png` (reflow)
- Fresher, higher-quality curated cards exist under `native/build/` (dated 2026-07-12,
  ~60–78 KB each) but are **gate artifacts** (ephemeral / gitignored) — they must be
  **copied into `docs/media/`** to be referenceable by the README:
  - `gallery-task.png` — a task selected, app bar in context
  - `gallery-row.png` — a row selected
  - `gallery-milestone.png` — a milestone selected
  - `gallery-marker.png` — a marker selected
  - `gallery-document.png` — document (nothing selected) context
  - `gallery-settings.png` — document settings popover
  - `gallery-dep-link.png` — dependency link between two tasks
  - `gallery-multi.png` — multi-select
  - `gallery-context-menu.png` — right-click context menu
  - `gallery-card.png` — task detail card
- The newest v2.7.x **time-window editing** feature has **no screenshot yet**
  (`gallery-window.png` is a W4 deliverable). Do NOT fabricate one — leave it out.

## Tasks

1. **Copy assets.** Copy each of the 10 `native/build/gallery-*.png` listed above into
   `docs/media/` keeping the same filenames. (If `native/build/gallery-card-appbar.png`
   exists and you use it, copy it too.) These become tracked repo assets.
2. **Rewrite the native gallery subsection** (`### PowerPoint add-in …`) to feature the
   new cards. Prefer a compact HTML `<table>` (2–3 columns) of thumbnails with short
   captions, or a clean vertical sequence — whichever renders well on GitHub. Give each
   image meaningful **alt text** describing the selection context (task / row / milestone
   / marker / document settings / dependency link / multi-select / context menu). Lead
   with the strongest single hero card (`gallery-task.png` or `gallery-card.png`).
3. **Keep** the web subsection (`web-demo.gif` hero + `web-app.png`) and the reflow image
   (`native-reflow.png`) as-is.
4. **Retire the superseded crops.** The `native-v5-taskbar/rowbar/milestonebar/settings`
   app-bar crops are smaller and less representative than the new full-context cards —
   replace their role with the gallery cards. You may keep `native-v5-chart.png` if it
   still reads well as an establishing shot, otherwise swap it for a gallery card. Do not
   delete the old files from disk; just stop referencing the retired ones.
5. **No broken links.** Every image path the README references must exist on disk after
   your changes.

## Constraints

- Touch only `README.md` and files under `docs/media/`. Do not modify `src/`, `spec/`,
  `native/`, or any build script.
- Do not run git commit / push. Do not modify `powerspawn` or `.claude/`.
- Keep image widths reasonable for GitHub (use HTML `width="..."` on `<img>` if helpful).

## Acceptance gate

- **Command:** from repo root, extract every image path referenced in `README.md` and
  assert each file exists. A passing run prints `READMELINKS OK`. Example check:
  ```bash
  python - <<'PY'
  import re, os, sys
  s = open('README.md', encoding='utf-8').read()
  paths = re.findall(r'!\[[^\]]*\]\(([^)]+)\)', s) + re.findall(r'<img[^>]*src="([^"]+)"', s)
  missing = [p for p in paths if not p.startswith('http') and not os.path.exists(p)]
  print('MISSING:', missing) if missing else print('READMELINKS OK')
  sys.exit(1 if missing else 0)
  PY
  ```
- **Pass signal:** `READMELINKS OK` and the native gallery subsection now references the
  new `docs/media/gallery-*.png` cards.
- **Report back:** which files you copied, the new README section markup, and the gate
  output.
