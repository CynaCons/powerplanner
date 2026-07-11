# PowerPlanner Specification Structure

This document defines the canonical layout of the specification and requirements tree. It is the "redrawn" architecture after the 2026-07-11 feedback round.

## Layers (from generic to specific)

1. **Foundation (shared concepts — `spec/` root)**
   - `data-model.md` — entities, fields, invariants (language neutral)
   - `layout.md` — abstract day/row projection algorithm
   - `visual-vocabulary.md` — how elements are drawn (shapes, sizes, colors, labels)
   - `interaction.md` — core operations, selection/editing model
   - `schema/document.schema.json` — machine contract
   - `fixtures/` + `*.expected.json` — conformance goldens (used by both web + native)
   - `visual/` — generated reference SVGs/PNGs from web renderer

2. **Shared Requirements (apply to web + native) — `spec/srs/`**
   - ASPICE tables: `SRS-*.md`
   - One "shall" per row, with ID, Rationale, Verification, Trace
   - Examples: SRS-chart-elements.md, SRS-editing.md, SRS-powerpoint.md (the PPT parts that are surface-specific but still in shared because foundational)
   - See `srs/README.md` for format and process

3. **Native-specific Requirements — `spec/srs-native/`**
   - Same table format.
   - Only concerns that cannot or should not apply to web: overlay chrome, app bar docking & context, suppression of native child shape selection, custom theme-coherent menus/panels, lifecycle gating, native hit-test, etc.
   - Primary file for 2026-07-11 feedback: `SRS-NativeUXFoundations.md` (SHP, DOCK, BAR, THEME, etc.)
   - Must be paired with harness e2e scenarios under `native/tools/scenarios/`

4. **Implementation Notes, Plans, Guides — `docs/`**
   - `native-addin.md`, `powerpoint-addin.md`
   - `onslide-*-plan.md`, `overlay-architecture-map.md`, `onslide-ux-inventory.md`
   - `design-tokens.md` (normative values, consumed by native GanttTheme.h and mockup)
   - `mockup/onslide-mockup.html` (approved visual+interaction reference)
   - `SRS_*.md` (legacy — being migrated/converted into `spec/srs-native/` tables)
   - Do **not** author new "shall" statements here.

5. **Web Implementation** — `src/`
   - Consumes foundation + shared SRS.
   - No separate srs-web yet (future if divergence grows).

6. **Native Implementation** — `native/PowerPlannerAddin/`
   - C++ COM, Gantt* + Overlay.cpp
   - Consumes foundation + shared SRS + `spec/srs-native/`

## Process Rules (enforced in AGENTS.md + PLAN.md)

- New feedback or discovery → add to active iteration in `PLAN.md` **first**.
- Requirements → table in the correct `spec/srs*` location using the canonical columns.
- Native UX changes → SRS entry + harness scenario/trace profile + re-run with `--check-invariants`.
- Theme coherence (menus, panels, chrome) is non-negotiable; cite SR-THEME-*.
- Keep `spec/` as the clean contract layer. `docs/` supports but does not replace it.
- After changes to structure, update this file, `spec/README.md`, `AGENTS.md`, and `PLAN.md`.

## Migration Notes (2026-07-11)
- Legacy `docs/SRS_*.md` (RowAndTaskSelection, SelectionChromeVisuals, etc.) will be converted to tables and moved or merged under `spec/srs-native/`.
- References in PLAN and docs updated progressively.
- Goal of v2.5.4 / v2.5.5: sustainable, non-redundant layout.

This structure ensures clarity between common Gantt concepts and platform (native) specifics while focusing effort on the native on-slide experience.
