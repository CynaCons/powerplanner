# SRS - Selection Chrome & Chart Visual Quality (ASPICE-style)

## 1. Feature Overview / Purpose
The overlay chrome (frames, highlights, badge, app bar) and the emitted chart
must read as one calm, production-grade surface. Today the chrome fights the
content and PowerPoint's own selection UI.

Defects observed in trace screenshots (2026-07-10/11) + user feedback:
- Overall selection shows THREE chromes at once: PowerPoint's native grips,
  our thick saturated blue frame, and the "PowerPlanner" chip — visually
  noisy and amateur ("prominent blue rectangle" feedback, PLAN v2.4.3).
- Row selection highlight fill makes the row label unreadable/invisible.
- Task bar progress fill covers the start of bar label text ("eframes",
  "ws"); some bars show no readable label.
- Marker labels ("TODAY", "BOARD REVIEW") collide with axis month/day text.
- Dependency elbows route through bar labels and annotation text.
- App bar window clips its own content in row context (leading "INSE…" and
  trailing "…rid" cut off).

## 2. User Goals & Interactions
- Selection state is always obvious but never louder than the content.
- Exactly ONE selection language at a time; our chrome never stacks on
  PowerPoint's for the same object.
- Labels (row, bar, marker, axis) are always readable in every state:
  idle, hover, selected, during drag.
- The app bar always shows all of its buttons, correctly sized.

## 3. Software Requirements (Functional)

### Overall (CHART_ROOT) chrome
- **SR-CHR-01**: When CHART_ROOT is natively selected, PowerPoint's native
  grips are THE selection chrome. The overlay shall draw at most a hairline
  frame (1 px physical, low-emphasis token) and shall NOT draw the badge or
  any filled/saturated rectangle.
- **SR-CHR-02**: The "PowerPlanner" chip shall appear only as a hover
  affordance (pointer within the chart's top-edge grip zone, nothing
  selected) — small, token-styled, disappearing when the pointer leaves.
  It shall never appear on other monitors, during other apps' focus, or
  stacked over native selection (see SRS_OverlayLifecycle).
- **SR-CHR-03**: When nothing is selected and the pointer is outside the
  chart, the overlay draws NO chart-level chrome (zero rectangles).

### Row & item chrome
- **SR-CHR-04**: Row selection shall render as a left accent bar plus at
  most a low-alpha wash that keeps every label at readable contrast
  (label text drawn above any wash; wash alpha bounded by token). The row
  label shall remain pixel-visible in ALL selection states (verified by
  pixel comparison of the label region, not only shape counts).
- **SR-CHR-05**: Task/milestone/marker/text selection frames use one
  consistent visual family (thin frame + small handles from GanttTheme.h);
  no filled overlays over content.

### Chart emission (bars, labels, markers, deps)
- **SR-VIZ-01**: Bar label text shall always render above progress fills
  with sufficient contrast on both segments (progress fill may not cover
  or dim the label). If the label doesn't fit inside the bar, apply the
  documented fallback chain (inside → right → left, SRS-ELEM-006/NUI-08)
  instead of clipping.
- **SR-VIZ-02**: Marker labels shall not overlap axis header text: markers
  render their labels in a dedicated band or offset with collision
  avoidance (shift/stagger), and never on top of month/day labels.
- **SR-VIZ-03**: Dependency connectors route with a consistent elbow
  pattern, terminate with arrowheads pointing INTO the target edge, and
  render BELOW all text (z-order: gridlines < deps < bars < labels).
- **SR-VIZ-04**: Axis header, gridlines, today/marker lines use the token
  palette with clear hierarchy (axis text quieter than content, today line
  distinct from markers).

### App bar
- **SR-BAR-01**: The app bar window shall size itself to its measured
  content (min of content width vs slide-pane width) — buttons are never
  clipped. If content exceeds available width, groups collapse per a
  defined priority (context group stays, INSERT collapses to a "+" menu)
  rather than clipping.
- **SR-BAR-02**: The app bar shall be horizontally centered on the chart's
  pane, keep a stable position across ticks (no jitter), and its
  show/hide follows the single visibility authority (SRS_OverlayLifecycle
  SR-LIFE-01).

### One design source
- **SR-TOK-01**: Every color/size introduced or changed by this SRS lands
  in GanttTheme.h AND docs/design-tokens.md in the same change (native is
  authoritative for the add-in; the doc stays normative for review).

## 4. Verification Approach (E2E with Native Seams)
- Extend the screenshot matrix (appbar-shot/trace) to capture ALL states:
  idle-no-hover, chart hover, row hover, row selected, task selected,
  milestone/marker/text selected, overall natively selected, link mode,
  during-drag ghost — at 100% and 150% DPI.
- Pixel invariants in harness_driver:
  - Label-region stability: crop each row-label rect (from chrome dump) and
    require SSIM/diff vs unselected baseline above threshold in every
    selection state (catches SR-CHR-04 regressions structurally missed by
    shape counts).
  - "No giant fill": in overall-selected capture, no contiguous saturated
    accent region larger than N px² outside bar rects (catches SR-CHR-01).
  - App bar completeness: rendered content width ≤ window width; first/last
    button rects fully inside the window rect (SR-BAR-01) via
    OverlayAppBarButtonRectForTest.
- Marker/axis collision: assert marker-label rects (new dump field) don't
  intersect axis text band (SR-VIZ-02).
- Visual review AC: PNG set attached for user judgment at iteration close
  (the user remains final judge of "calm").

## 5. Non-Functional / Constraints
- Tokens only (G6); DPI via HtScalePx; no new .cpp without .bat updates
  (prefer header-only); harness stays input-neutral.
- Chart emission changes must keep conformance fixtures passing (geometry
  frozen — visual style changes only unless a fixture explicitly covers
  style) and FITPERSIST OK.
- Web↔native visual-vocabulary parity tracked (FND-05): where native style
  changes, note divergence vs web renderer in design-tokens.md.

## 6. Open / Related
- Exact token values decided in the design pass of Iteration 2 (update
  docs/design-tokens.md; mockup remains the reference).
- Suppressing PowerPoint's native grips entirely (owning ALL chrome) was
  considered and rejected for now — native grips are the platform-familiar
  resize affordance for the whole object.
- Related: SRS_OverlayLifecycle.md (when chrome may exist at all),
  spec/srs-native/SRS-RowAndTaskSelection.md (selection semantics).
