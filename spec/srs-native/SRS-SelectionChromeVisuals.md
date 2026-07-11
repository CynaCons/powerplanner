# SRS - Native Selection Chrome & Chart Visual Quality

Native-specific requirements for overlay chrome, emitted chart visuals, and
app bar fit. The on-slide editor must read as one calm, production-grade
surface where selection is obvious, labels remain readable, and PowerPoint's
native selection UI is not visually stacked with conflicting custom chrome.

This file migrates the legacy prose selection chrome and visual quality
requirements for v2.6.8.

Traces up to: `../srs/SRS-chart-elements.md`, `../srs/SRS-layout.md`,
`../visual-vocabulary.md`, `docs/design-tokens.md`,
`docs/onslide-experience-spec.md`, `spec/srs-native/SRS-OverlayLifecycle.md`,
`spec/srs-native/SRS-RowAndTaskSelection.md`, and
`native/tools/harness_driver.py`.

Reference impl: `native/PowerPlannerAddin/` (Overlay.cpp, GanttScene*,
GanttBuilder.cpp, GanttTheme.h, GanttAppBar.h, GanttHitTest.*).

## Overall Chart Chrome

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-CHR-01 | When CHART_ROOT is natively selected, the overlay shall draw at most a low-emphasis one-physical-pixel hairline frame without a badge or any filled/saturated rectangle. | PowerPoint's native grips are the platform selection chrome for the whole component; additional saturated chrome is visually noisy. | Overall-selected screenshot matrix; pixel invariant rejects large saturated accent regions outside bar rects. | Legacy SR-CHR-01; Overlay overall chrome paint |
| SR-CHR-02 | The `PowerPlanner` chip shall appear only as a small token-styled hover affordance when the pointer is within the chart top-edge grip zone and nothing is selected. | The chip is an affordance, not persistent selection state. | Chart-hover and overall-selected screenshots; lifecycle traces verify the chip never appears outside valid host visibility. | Legacy SR-CHR-02; SRS-OverlayLifecycle SR-LIFE-01..06 |
| SR-CHR-03 | When nothing is selected and the pointer is outside the chart, the overlay shall draw no chart-level chrome. | Idle charts should look like clean PowerPoint content. | Idle-no-hover screenshot matrix; paint dump shows no chart-level rectangles. | Legacy SR-CHR-03; idle chrome state |

## Row And Item Chrome

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-CHR-04 | Row selection shall render as a left accent bar plus at most a token-bounded low-alpha wash with row label text drawn above the wash. | Row selection must be visible without making labels unreadable. | Row-selected pixel comparison of label crop against unselected baseline; screenshot review at 100% and 150% DPI. | Legacy SR-CHR-04; row-label-hover-click trace |
| SR-CHR-05 | Task, milestone, marker, and text selection frames shall use one consistent visual family of thin frame and small handles sourced from `GanttTheme.h` without filled overlays over content. | A single item-selection language reduces visual clutter and preserves content readability. | Selection visual matrix for task/milestone/marker/text states; token review against design-tokens.md. | Legacy SR-CHR-05; Overlay item chrome paint |

## Chart Emission Visuals

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-VIZ-01 | Bar label text shall render above progress fills with sufficient contrast on both fill segments and documented fallback placement rather than clipping when labels do not fit inside the bar. | Task names and progress must both remain readable. | Progress and task-selected screenshots; label/fill z-order review; fallback checks against shared SRS-ELEM/NUI layout requirements. | Legacy SR-VIZ-01; SRS-chart-elements; GanttScene/GanttBuilder |
| SR-VIZ-02 | Marker labels shall avoid overlap with axis header text by rendering in a dedicated band or applying collision-avoidance offset/staggering. | Marker annotations should not obscure the time axis. | Marker/axis collision assertions from dumped label rects; screenshot review. | Legacy SR-VIZ-02; marker-label rect dump |
| SR-VIZ-03 | Dependency connectors shall route with a consistent elbow pattern, terminate with arrowheads pointing into the target edge, and render below all text. | Dependencies must be legible without crossing through labels. | Dependency screenshot matrix and z-order review; connector geometry assertions where available. | Legacy SR-VIZ-03; GanttScene dependency routing |
| SR-VIZ-04 | Axis header, gridlines, today lines, and marker lines shall use the token palette with clear hierarchy. | Timeline scaffolding should support content rather than compete with it. | Visual review against design tokens and native mockup; token source check in GanttTheme.h. | Legacy SR-VIZ-04; docs/design-tokens.md |

## App Bar Fit

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-BARV-01 | The app bar window shall size itself to measured content up to the slide-pane width, with overflow groups collapsed by defined priority rather than clipped. | Context commands must be reachable and readable in every selection state. | Appbar-shot matrix; `OverlayAppBarButtonRectForTest` asserts first/last button rects are inside the window. | Legacy SR-BAR-01; GanttAppBar layout |
| SR-BARV-02 | The app bar shall remain horizontally centered on the chart pane with stable position across ticks while following the single overlay visibility authority. | The app bar should feel docked to the component and must not jitter or outlive valid host visibility. | Docking/position traces and lifecycle gate traces; compare pre/immed/+1/+3 appbar rects. | Legacy SR-BAR-02; SRS-OverlayLifecycle SR-LIFE-01; SRS-NativeUXFoundations SR-DOCK |

## Design Token Source

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-TOK-01 | Every color or size introduced or changed by selection chrome and native visual quality work shall land in `GanttTheme.h` and `docs/design-tokens.md` in the same change. | Reviewable, tokenized styling prevents one-off visuals from diverging between surfaces. | Code/doc review for token additions; screenshot review against mockup and design-tokens.md. | Legacy SR-TOK-01; GanttTheme.h; docs/design-tokens.md |

## Open / Related

- Suppressing PowerPoint native grips entirely was considered and rejected for
  now; native grips remain the familiar whole-component resize affordance.
- Exact token values continue to be decided in design-token work and verified
  through PNG review.
