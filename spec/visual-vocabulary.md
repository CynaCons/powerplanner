# Visual Vocabulary

How each laid-out element is drawn. The spec fixes the *vocabulary* (shapes,
proportions, label rules, semantic colors); each implementation realizes it in
its medium — SVG primitives (web) or native PowerPoint shapes (PPT). Both must
stay **presentation-grade**: crisp edges, stable spacing, readable labels.

## Elements

| Element        | Shape                              | Web (`src/renderer`)        | PowerPoint (`native/`)              |
|----------------|------------------------------------|-----------------------------|-------------------------------------|
| Task bar       | rounded rectangle                  | `<rect rx>`                 | `msoShapeRoundedRectangle`          |
| % complete     | darker inset fill from bar left    | inset `<rect>`/clip         | inset rectangle or 2-tone fill      |
| Milestone      | diamond centered on its date       | rotated square / polygon    | `msoShapeDiamond`                   |
| Summary bar    | thin bar with end caps             | `<path>`/`<rect>`           | rectangle (thin) + caps             |
| Bracket        | square U-bracket over a span/rows  | `<path>`                    | connectors / freeform               |
| Dependency     | 3-segment orthogonal elbow, arrow at `to` | `<path>` + marker    | `msoConnectorElbow` + arrowhead     |
| Marker (today) | dashed vertical line + pill label  | `<line>` dashed + text      | line + text box                     |
| Marker (deadline/note) | vertical line + label      | `<line>` + text             | line + text box                     |
| Row label      | left-gutter text, indented for children | text                   | text box in the gutter              |
| Axis ticks     | stacked tick labels (major/minor/micro) | text + rules           | text boxes / rules                  |

## Proportions (web device profile, for reference)

- Row slot height `ROW_HEIGHT = 36`; bar thickness `BAR_HEIGHT = 22` (bar is
  vertically centered in its slot). Milestone diamond `MILESTONE_SIZE = 14`.
- A bar narrower than its label hides the on-bar label and falls back to a
  right-side (then left) placement; see `labelPlacement` below.

Other implementations choose their own absolute sizes but MUST preserve the
*ratios* (bar noticeably thinner than the row; milestone ~ bar height; readable
gutter) so charts look like the same product.

## Color semantics

- Task/milestone/bracket `color` is `#RRGGBB`. Absent → the implementation's
  default accent (web task default ≈ `#818CF8`; milestone default ≈ `#FBBF24`).
- `% complete` inset is a darkened shade of the bar color.
- Dependency connectors are a neutral grey (≈ `#94A3B8`).
- PowerPoint stores color as BGR (`0x00BBGGRR`); converting from `#RRGGBB` is the
  PPT implementation's responsibility.

## Labels and collision

`labelPlacement` ∈ `on-bar | left | right | above | below | hidden`.

- Default for tasks is `on-bar`; if the label does not fit on the bar, fall back
  to `right`, then `left`. `hidden` suppresses the label.
- Milestones default to a placement beside/below the diamond.
- Axis labels are thinned by minimum spacing (see `layout.md` Step 9) so they
  never overlap.

## Themes

`style.theme` ∈ `light | dark | print`. Themes are expressed as token sets
(background, surface, text, grid, accent). The web impl uses CSS variables; the
PPT impl maps tokens to shape fills/lines. `print` is a high-contrast,
chrome-free profile.

## Grouping & identity (implementation contract)

Emitted output should be **inspectable and round-trippable**:

- Web: elements carry their source `id` for hit-testing/selection.
- PowerPoint: every emitted shape is tagged (`PP_KIND`, `PP_ID`), and the whole
  chart is grouped under a root carrying the serialized document
  (`PP_DOC`) — see `native/` and `interaction.md`. This is what lets the add-in
  read a chart back off a slide.
