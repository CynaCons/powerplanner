# Native visual / DPI policy (Phase 13 v2.8.2)

## Golden images

- Location: `native/tools/goldens/`
- Compare: MD5 + file size via `python native/tools/harness_driver.py golden …`
- Update only with `--update` / `--update-goldens` when the visual change is **intentional**; record in PLAN.md

## Capture DPI

- Harness reports should record host DPI when available (see driver/report fields).
- **Golden DPI:** single reference DPI for committed goldens. On headless/agent rigs this is often the display scale at capture time (historically 200% on the authoring machine).
- **Do not** claim a multi-DPI matrix unless captures at each DPI exist and are compared.
- Prefer 100% when the operator can set it; otherwise document the single golden DPI in the golden set README.

## Multi-DPI

- Optional smoke: if a second DPI is available, run one `gallery_matrix` or appbar capture and compare manually.
- **Skip (2026-07-17):** multi-DPI automation not available headlessly → explicit skip; single golden DPI only.

## Web ↔ native parity

- Minimum set: `spec/fixtures/basic-chart.json` → web `spec/visual/basic-chart.svg` vs native gallery document context PNG.
- Full pixel parity deferred; checklist Review gate + fixture hashes when goldens land.
- Token parity: `python native/tools/check_theme_tokens.py`

## Slideshow / multi-monitor

- **Decision:** remain **Demo / manual** until a dedicated multi-monitor or slideshow COM rig exists (v2.8.5 notes). Not claimed automated.
