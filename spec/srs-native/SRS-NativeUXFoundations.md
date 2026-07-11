# SRS — Native On-Slide UX Foundations (Selection, Docking, Context, Theming)

Native-specific requirements for the PowerPoint COM add-in on-slide experience. Feature tags: SHP (shape/component), DOCK (appbar positioning), BAR (contextual app bar), THEME (coherence), SEL (selection model).

Traces up to: `../srs/SRS-powerpoint.md`, `../interaction.md`, `docs/onslide-experience-spec.md`, `docs/design-tokens.md`, `native/PowerPlannerAddin/GanttTheme.h`.

Reference impl: `native/PowerPlannerAddin/` (Overlay.cpp, GanttBuilder.cpp, GanttHitTest.*, GanttAppBar.h, GanttCommandRegistry.h).

## Component as Selectable Shape (PP semantics) — SHP

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-SHP-01 | The emitted chart shall expose exactly one selectable PowerPoint shape to the user for the whole component: the CHART_ROOT group. All internal primitives (task bars, progress fills, milestone diamonds, labels, connectors, markers, row bands, text notes, etc.) shall be children of this group and shall not be individually selectable via PowerPoint's native selection model. | The chart is one "app-component" from the presentation author's perspective. Fine-grained selection and editing must be governed exclusively by the add-in's overlay + ownSel model to deliver consistent think-cell-style UX and prevent visual conflicts with our chrome. | Harness trace + state seam (no child PP_KIND shape remains in active Selection after item clicks; Unselect or re-select-parent observed); manual verification in PowerPoint that clicking a task bar never shows native child handles on the bar itself. | GanttBuilder (grouping under CHART_ROOT); Overlay.cpp Tick + ApplyClickSelection + suppression logic; GanttHitTest |
| SR-SHP-02 | When the overlay hit-test identifies an internal element (TASK, ROW, etc.), any concurrent or subsequent native selection of a child shape with a PP_KIND other than CHART_ROOT shall be suppressed (Unselect or equivalent) within one tick (≤150 ms). The internal ownSel model shall take precedence for chrome, app bar, and commands. | Polling suppression must be reliable and not leak visible PowerPoint selection artifacts (grips, highlights) on internals. | E2E trace scenarios exercising click on TaskBody, Label, RowBand, Milestone; assert in dumps that g_selKind or native sel never stabilizes on a non-CHART_ROOT child; screenshots show only our chrome or none. | Overlay Tick (child suppression path); harness "no_child_shape_selected" invariant |
| SR-SHP-03 | Direct user actions that would normally select a child (lasso, keyboard arrows in some contexts, Alt+click drill, post-rebuild timing windows) shall still result in the CHART_ROOT (or nothing) being the PowerPoint-level selection while our overlay reflects the precise item selection. | Edge cases must not bypass the model. | Add targeted harness profiles; review of SelectionChange behavior and post-mutate selection state in traces. | Connect.cpp selection handling + Overlay |
| SR-SHP-04 | The CHART_ROOT group itself shall remain natively selectable and movable/resizable using PowerPoint's standard group handles. Moving or resizing it shall update the overlay rect and docked app bar without loss of content or selection model integrity. | The container is the user's "shape" for slide layout. | Existing overall-move/resize traces + new combined invariants; FitChartRoot* paths. | GanttBuilder Fit* ; Overlay grip handling |

## App Bar Sticky Docking to Component — DOCK

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-DOCK-01 | The app bar window shall be positioned immediately below the current screen rect of the CHART_ROOT (the "app-component"), using a small, token-defined vertical gap (e.g. 8 pt scaled). The combined visual forms a single logical unit with an "imaginary outline" around the chart + docked bar. | The bar is context for the chart; it must travel with the component and never appear detached or inside the chart area. | Harness trace: appbar top ≈ chartScreenRect.bottom + gap (within 1-2 px tolerance after DPI); position stable at pre/immed/+1/+3. Screenshot review of docking. | Overlay ShowAppBar + chart rect computation in Tick; grip/move paths that call RequestRepaint + ShowAppBar |
| SR-DOCK-02 | Any native move or resize of the CHART_ROOT group (via its handles or FitChartRoot*) shall cause the app bar to be repositioned to the new bottom of the component on the next overlay update (or synchronously where possible). No visible lag or stale position after the operation settles. | User expects the whole "app" to move as a unit. | Extend trace_overall_move and trace_overall_resize; add "appbar_docked_stable" invariant; verify with before/immed/+ticks. | Overlay overall grip code + Tick chartChanged handling |
| SR-DOCK-03 | The app bar shall never be shown when no CHART_ROOT is present, and shall be hidden together with the overlay on all gating conditions (host inactive, slideshow, out-of-bounds, etc.). | Coherent visibility. | Reuse and extend IsHostActiveForOverlayChrome + lifecycle SRS. | Overlay HideAppBar paired paths |

## Contextual App Bar Content — BAR

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-BAR-01 | When the overall document / component is selected (ownSelKind empty or CHART_ROOT or background), the app bar shall include the INSERT group and the global SCALE group (D/W/M/Q/Y + Labels + Grid). | Document-level operations (timescale, structure) belong at the chart root context. | appbar_matrix scenario + state assertions on groups present. | BuildAppBar (None path); RebuildAppBarModelFromSlide |
| SR-BAR-02 | When a specific item is selected (TASK, ROW, MILESTONE, etc.), the app bar shall contain *only* groups and items relevant to that item (e.g. Edit/Rename/Swatches/Nudge/Label/Progress/Delete for TASK; no SCALE controls). | Context pollution is confusing; matches per-selection mental model. | Trace profiles selecting task then overall and vice-versa; assert exact appBarGroups contents (presence/absence of "SCALE"). | AppBarAppend* conditional logic; harness dumps of appBarGroups |
| SR-BAR-03 | Changing selection (overall ↔ task/row) shall update the app bar content immediately (within the same or next tick) without requiring additional user action. | Reactive UI. | Selection change traces + visual matrix. | RebuildAppBarModelFromSlide called on sel change |

## Theme-Coherent All Custom Surfaces — THEME

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-THEME-01 | Every menu, popup, card editor, inline editor, and any other surfaced panel created by the add-in shall be implemented with custom drawing (GDI+/layered windows or owner-drawn) using exclusively the tokens from GanttTheme.h (sourced from docs/design-tokens.md). Default Win32 HMENU TrackPopupMenu visuals, unthemed dialog frames, or standard control chrome are forbidden. | Consistency with the approved mockup and the rest of the on-slide surface (app bar, overlay chrome). "Cheap right-click" or PowerPoint-default appearance breaks the presentation-grade contract. | Visual review of captured right-click and double-click-edit surfaces against mockup + tokens; pixel-level or coordinator sign-off. No default menu metrics allowed in code. | Overlay custom paint paths for appbar; new custom menu/card impl (replaces CreatePopupMenu + Track + standard CreateWindow children) |
| SR-THEME-02 | All future UI elements (tooltips, hints, pills, inspectors, etc.) added to the native surface shall cite this requirement in their SRS entry and implementation plan. Theme source shall be single (GanttTheme.h + tokens doc). | Prevent regression to ad-hoc styling. | Code review + new-SRS checklist. | design-tokens.md; GanttTheme.h |
| SR-THEME-03 | Right-click context menus (even when derived from the same AppBarModel as the bar) and the task edit overlay panel shall be theme-coherent custom surfaces. | User feedback explicitly called out inconsistency of current right-click and overlay edit panel. | E2E scenario covering right-click on task + double-click edit; PNG artifacts + review gate. | GanttHitTest BuildMenu + ShowContextMenuForHit; card editor window creation |

## Cross-Cutting

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-NUI-01 | All new or changed native UX behavior (selection, docking, context, menus, panels) shall have a matching entry in this SRS (or sibling native SRS) written in table format *before* or concurrent with the code change, plus a harness e2e scenario exercising it. | Systematic coverage from feedback. | PLAN.md + harness run logs + PR review. | This file; native/tools/harness_driver.py + scenarios/ |
| SR-NUI-02 | Changes to app bar content rules, docking math, or suppression logic must re-run the full appbar_matrix + relevant trace profiles and pass invariants before considered done. | Protect against re-introducing flashes, wrong context, or detached UI. | Harness execution in the loop. | v2.4.0+ trace infrastructure |

## Open / Related
- Convert and migrate legacy prose SRS files from docs/ (SRS_RowAndTaskSelection.md etc.) into proper tables here or shared.
- Implementation of fully custom menu and card surfaces may be staged (first make right-click custom, then card).
- Ties to v2.5.2 (creation), v2.5.1 (calm chrome), v2.5.0 (lifecycle), v2.5.4 (structure + theme parity).
