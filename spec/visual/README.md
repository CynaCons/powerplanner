# Visual Specification

Reference figures showing exactly how a chart should look. These are **generated
from the web reference implementation** (`src/embed/GanttRenderer`), not
hand-drawn — so the visual spec cannot drift from what the app actually produces.

## How it's generated

`scripts/gen-visual-spec.ts` renders every conformance fixture document
(`spec/fixtures/*.json`) through `<GanttRenderer>` via `renderToStaticMarkup`
and writes a standalone SVG per fixture, plus `index.json` (the figure manifest).

```
npm run gen:visual      # regenerate spec/visual/*.svg from the fixtures
```

The SVGs use CSS-variable fills with literal fallbacks
(e.g. `var(--pp-accent, #4f46e5)`), so each file renders correctly on its own
(open it in a browser) and also adapts to a host's theme variables when embedded.

## What's here

- `<fixture>.svg` — the canonical render of each fixture document.
- `index.json` — `{ figure, fixture, width, height }` per figure.

## Relationship to the SRS

A fixture is the input, its expected layout (`*.expected.json`) is the
machine-checked output, and its `*.svg` here is the human-checkable visual. SRS
requirements cite the fixture; this directory shows what that fixture looks like.
Regenerate after any change to the renderer or the fixtures, and review the SVG
diff as part of the change.

## Status / next

- Generated figure per fixture: working (`basic-chart.svg`).
- Later (F1/F2): annotated callouts keyed to requirement IDs
  (e.g. `SRS-ELEM-010 → the diamond`), and an interactive gallery viewer.
