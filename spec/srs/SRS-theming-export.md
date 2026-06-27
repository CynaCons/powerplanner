# SRS — Theming & Export

Requirements for visual themes and presentation-grade export. Feature tags:
`THEME` (themes) and `EXP` (export).

Traces up to: `../visual-vocabulary.md`, `../data-model.md` (StyleSettings).

## Theming

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-THEME-001 | The system shall support three themes: light, dark, and print. | Screen (light/dark) and paper (print) need different palettes. | Review | data-model (ThemeName); visual-vocabulary |
| SRS-THEME-002 | Themes shall be expressed as token sets (background, surface, text, grid, accent) so every element recolours coherently. | Centralised tokens keep the chart visually consistent. | Review | visual-vocabulary §themes |
| SRS-THEME-003 `[web]` | The print theme shall be high-contrast and chrome-free (chart only). | Printed output must be legible and free of app UI. | Demo | visual-vocabulary; print stylesheet |
| SRS-THEME-004 | A per-element `color` shall override the theme accent for that element. | Categorical coloring within a theme. | Review | SRS-ELEM-004 |

## Export `[web]`

The PowerPoint surface emits native shapes (see `SRS-powerpoint.md`) rather than
exporting image/vector files, so these requirements apply to the web surface.

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-EXP-001 `[web]` | The system shall export the chart as SVG with computed styles inlined, so the file renders standalone. | Vector output for decks/docs without external CSS. | Demo | fileIo/export; spec/visual generator |
| SRS-EXP-002 `[web]` | The system shall export PNG at selectable scale (1x, 2x) by rasterizing the SVG. | Raster output for tools that need bitmaps; 2x for retina. | Demo | export |
| SRS-EXP-003 `[web]` | The system shall produce print/PDF output via the print theme (chart only). | Presentation-grade hard copy. | Demo | print stylesheet |
| SRS-EXP-004 | Exported output shall be presentation-grade: crisp primitives, stable spacing, readable labels. | The product promise is "charts you can actually present". | Review | visual-vocabulary |

## Open items

- Export requirements verify by Demo; an SVG-serialization snapshot test would
  promote SRS-EXP-001 to Test (the `spec/visual` generator is a step toward this).
