# Memory — capturing the web app for showcases and gates

One dated fact per heading. Corrected in place. The showcase rules themselves
live in `AGENTS.md`; this is the mechanical knowledge for getting a usable
image out of this app.

## What already exists (2026-10-23)

- `scripts/shot.mjs <url> <out.png> [w] [h]` — launches Chromium, 2x
  `deviceScaleFactor`, waits for `networkidle` plus 1200 ms, full-viewport PNG.
  Good for a whole-app frame; for a showcase, clip to an element instead.
- `scripts/record-web.mjs`, `scripts/record-gif.mjs` — motion capture, for
  gestures a still cannot show (a drag with live preview).
- `scripts/rasterize-visual.mjs` — the `spec/visual/*.svg` reference figures.
- `scripts/gen-visual-spec.ts` (`npm run gen:visual`) — regenerates those
  figures by rendering each fixture through `<GanttRenderer>`, so they cannot
  drift from what the app produces.
- `docs/media/gallery-*.png` — the existing gallery set (card, context menu,
  dependency link, document, marker, milestone, multi-select, row, settings,
  task).

## Node resolves Playwright from the script's own location

A capture script run from outside the repo cannot `import '@playwright/test'` —
resolution starts at the *script's* directory, not the cwd. Write the script
into the repo root, run it, delete it. Never leave capture scripts behind.

## Drive state through the store, not the chart

The chart surface is SVG and the layout is derived: the agent states dates and
the renderer places bars. Setting up a capture by clicking through the chart is
slow and brittle. Use the document store, or the `powerplanner-web` agent
bridge with `apply_batch` (one undo frame, all-or-nothing).

## The app bar floats over the chart, and nothing scrolls behind it

`ChartArea` sizes its SVG to `clientHeight` and clips — there is no vertical
scrolling. The planned scroll-padding mitigation for the floating command bar
was therefore impossible, and padding would have corrupted the SVG sizing. For
captures this means the band under the bar is usually empty (rows lay out from
the top), but a dense chart will have content occluded. Frame accordingly.
(2026-08-10, from v2.13.1 / C1)

## Render both themes before publishing

Theme bugs hide in the gap between source and output, and the status quo has
caught this: two states that both render near-white prove nothing. Capture light
and dark, and give one state a visible override if the difference is otherwise
invisible.

## Port 5180

`npm run dev` is HTTP on 5180. `npm run dev:addin` is HTTPS on the same port and
belongs to the parked add-in — do not reach for it for web captures.
