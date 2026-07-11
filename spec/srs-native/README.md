# Native SRS (PowerPoint On-Slide)

This directory holds **native-specific** ASPICE-style Software Requirements Specifications for the PowerPoint COM add-in (the "native" implementation under `native/PowerPlannerAddin/`).

These refine or extend the shared foundation requirements in `../srs/`. They do **not** apply to the web implementation.

## Format
Use exactly the same table format as `../srs/` (see `../srs/README.md`):

| ID | Requirement | Rationale | Verification | Trace |

IDs: `SR-<FEAT>-NNN` or `SRS-NAT-<FEAT>-NNN` (choose a stable short tag per file, e.g. SHP for shape protection, DOCK, BAR, THEME, LIFE, CHR, etc.). Never reuse IDs.

## Scope
- On-slide overlay, app bar, context menus, editors and panels
- Shape emission, grouping, and PowerPoint selection semantics (CHART_ROOT vs children)
- Docking / positioning of auxiliary windows relative to the chart component
- Theme coherence of all in-slide UI surfaces (must match design-tokens.md + GanttTheme.h + approved mockup)
- Lifecycle, visibility, performance of chrome
- Native-specific creation/editing affordances and invariants
- Conformance to think-cell-style interaction model

## Traceability & Process
- Every entry must have a down-trace to code (Overlay.cpp, GanttBuilder.cpp, GanttHitTest.*, GanttAppBar.h, etc.), harness scenario / trace profile, or test.
- New work from feedback must:
  1. Be added to PLAN.md first.
  2. Receive an SRS table entry here (or shared srs/ if it generalizes).
  3. Be covered by e2e harness run (`python native/tools/harness_driver.py ...`).
- Verification for native UX is typically: harness trace + invariants + PNG review + "Demo" in PowerPoint until full automation exists.

## Current Files
- SRS-NativeUXFoundations.md — covers 2026-07-11 user feedback points (component shape selectability, sticky appbar, context appbar, theme-coherent surfaces).
- (Future) Split files by concern aligned with v2.5.x iterations and docs/onslide-ux-inventory.md.

When a native SRS generalizes to both surfaces, move the requirement to `../srs/` and update traces.
