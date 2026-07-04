# PowerPlanner Design Tokens (normative)

Single source of truth for every color, dimension, and type size in the native
on-slide editor. The interactive mockup at `docs/mockup/onslide-mockup.html`
uses these exact values in its CSS `:root` block; the native add-in consumes
them via `native/PowerPlannerAddin/GanttTheme.h`. **A change here must land in
both places in the same commit.** No other file may hardcode a color.

Values were authored in CSS px on a 16:9 mock; the pt column is the normative
PowerPoint value (px × 0.75 for type, proportional rules for layout).

## 1. Color — neutrals & structure

| Token | Hex | Used for |
|---|---|---|
| `ink` | `#1B1D26` | Primary text, milestone diamonds |
| `ink2` | `#5D6273` | Secondary text (axis top band, notes, milestone labels) |
| `ink3` | `#9A9FB0` | Tertiary text (rail header, ablabels, chevrons, dep connectors) |
| `surface` | `#FFFFFF` | Slide/chart/app-bar background |
| `railSurface` | `#F6F7FB` | Left rail fill |
| `headerBand` | `#F1F3F9` | Axis header bands, segmented-control track |
| `rowAlt` | `#FAFBFE` | Alternate row banding (odd row indices) |
| `outline` | `#E2E5EF` | Row/grid hairlines, chart border, app-bar border |
| `outline2` | `#D3D7E6` | Axis band cell separators, month/major ticks, note borders |

## 2. Color — accent & semantic

| Token | Hex | Used for |
|---|---|---|
| `primary` | `#4355E0` | Selection chrome, Today marker, rail selection, hover text |
| `primarySoft` | `#E8EBFC` | Hover/selected fills (rail row, app-bar button hover) |
| `primaryDim` | `#6B7DE8` | Brand accents on dark surfaces (not used on slide) |
| `deadline` | `#C6362B` | Deadline markers (stem + pill), destructive hover text |
| `dangerSoft` | `#FBEAE8` | Destructive button hover fill |
| `customMarker` | `#7A4FA3` | Default color for custom (non-today, non-deadline) markers |

## 3. Color — task-bar swatch palette (8)

Order is the order shown in swatch pickers. `PpTask.color` empty ⇒ `swatch1`.

| Token | Hex | | Token | Hex |
|---|---|---|---|---|
| `swatch1` (indigo) | `#4355E0` | | `swatch5` (rust) | `#B3552F` |
| `swatch2` (teal) | `#0E8D8A` | | `swatch6` (olive) | `#8B8E24` |
| `swatch3` (plum) | `#7A4FA3` | | `swatch7` (magenta) | `#B23A6B` |
| `swatch4` (slate) | `#5B6C8F` | | `swatch8` (pine) | `#2E7D6E` |

Bar rendering: track = swatch at **40% opacity** over white (pre-blend to a
solid: `blend(swatch, #FFFFFF, 0.40)`); progress portion = solid swatch; both
5pt corner radius. On-bar label text `#FFFFFF`.

## 4. Layout dimensions (pt unless noted)

| Token | Value | Notes |
|---|---|---|
| `chart.top` | 15% of slide height | Below reserved title zone |
| `chart.sideMargin` | 18 | Left/right/bottom margins (shipped, fit-to-slide) |
| `rail.width` | 150 | ≈ 200px/960px of the mock ⇒ 20.8% of a 720pt-wide chart ⇒ 150pt. Grows ×1.25 max when rail labels overflow |
| `axis.height` | 30 | Two stacked bands of 15 each; single-band scales (Y) use one 30pt band |
| `row.height` | existing `ROW_HEIGHT` | Rows always equal height (uniform rows) |
| `bar.height` | 0.50 × row.height | Vertically centered in its lane |
| `bar.radius` | 5 | Rounded-rectangle corner radius |
| `bar.subLane` | ±0.28 × row.height | Vertical offset when 2 bars share a row |
| `milestone.size` | 16 | Square rotated 45°, radius 3 |
| `marker.width` | 1.5 | Vertical line weight |
| `marker.pill.pad` | 2.5 × 7 | Pill padding; radius = full (stadium) |
| `hairline` | 0.75 | Row lines, week ticks |
| `hairline.major` | 1.0 | Month/quarter/year boundary ticks |
| `dep.weight` | 1.5 | Dependency elbow stroke |
| `note.pad` | 4 × 8 | Note box padding, radius 5, border `outline2` |
| `appbar.height` | 36 px @96dpi | Overlay window (device px, DPI-scaled via HtScalePx) |
| `appbar.radius` | 11 px @96dpi | Container corner radius |
| `appbar.btnRadius` | 7 px @96dpi | Button hover pill radius |

## 5. Typography (PowerPoint pt; family Segoe UI)

| Token | Size / weight | Used for |
|---|---|---|
| `type.title` | 22 light | Slide title placeholder (native, not ours) |
| `type.rowName` | 9.5 semibold | Rail row names |
| `type.railTask` | 8.5 regular | Rail task labels (task rows) |
| `type.barLabel` | 8 semibold, white | On-bar labels |
| `type.pct` | 7 semibold, white, tabular | Right-aligned % on bars ≥ ~110pt wide |
| `type.axisTop` | 7 semibold, tracking +0.10em, `ink3` | Axis top band |
| `type.axisBottom` | 7 semibold, `ink2`, tabular for numbers | Axis bottom band |
| `type.msLabel` | 7.5 semibold, `ink2` | Milestone labels |
| `type.note` | 8 regular, `ink2` | Note text |
| `type.markerPill` | 7 bold, tracking +0.08em, white, UPPERCASE | Marker pills |
| App-bar chrome | 11.5px semibold buttons · 9px bold +0.10em UPPERCASE group labels · 10.5px bold segments (@96dpi, DPI-scaled) | Overlay-drawn, not shapes |

## 6. Interaction states

| State | Treatment |
|---|---|
| Selection chrome (task/milestone/marker/note) | 1.5px `primary` frame, radius 7, 3px halo `primary` @14% alpha, two 7px side handles (white fill, `primary` border) |
| Rail row selected | `primarySoft` fill + inset 2.5px left edge in `primary`; name text `primary` |
| App-bar button hover | `primarySoft` fill, text+icon `primary`; destructive: `dangerSoft` + `deadline` |
| Segmented active | White chip, `primary` text, 1px shadow |
| Drag ghost | Element outline at 30% alpha `primary` + date tooltip (ink bg, white 8pt text) |
| Link mode | Crosshair cursor over bars; hint pill (ink bg, white text) above app bar |
