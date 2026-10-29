# SRS — Time Axis

Requirements for the calendar header: scales, fiscal calendar, tick generation,
and label thinning. Feature tag: `TAX`.

Traces up to: `../layout.md` §9, `../data-model.md` (CalendarSettings).
Reference impl: `src/layout/timeAxis.ts`.

## Scales

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-TAX-001 | The system shall support five time scales: day, week, month, quarter, year. | Plans span hours to years; one scale cannot serve all. | Review | data-model (TimeScale); timeAxis §buildAxis |
| SRS-TAX-002 | The system shall render the axis as stacked levels per scale: day = month/week/day; week = month/week; month = quarter/month; quarter = year/quarter; year = year/year. | Two-to-three levels give context (period) + detail (subdivision). | Test (axis levels per scale) | layout §9; timeAxis `buildAxis` |
| SRS-TAX-003 | The system shall position every tick at `xDay(tick.date)` in abstract day units, identical to element positioning. | Ticks and bars must align to the same time axis. | Analysis | layout §coordinate model; timeAxis `dateToX` |

## Fiscal calendar

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-TAX-010 | The system shall accept a `fiscalYearStart` month (1–12) and compute **quarter** boundaries and labels relative to it. | Many organisations report on a non-January fiscal year. | Test (quarter ticks honor fiscalYearStart) | data-model; timeAxis `quarterTicks` |
| SRS-TAX-011 | Year ticks shall be placed on calendar-year boundaries (Jan 1); when `fiscalYearStart` is 1 the label shall be the calendar year (e.g. `2026`), otherwise it shall be prefixed `FY` (e.g. `FY2026`). | Matches the implementation: year *gridlines* stay calendar-aligned while the label signals a fiscal calendar is in use. | Test | timeAxis `yearTicks` |
| SRS-TAX-012 | The system shall label quarters as `Q{q} {fy}` using the fiscal quarter and fiscal year of each period. | Quarter labels must reflect the fiscal calendar. | Test | timeAxis `quarterTicks`; utils `fiscalQuarterOf` |

## Tick generation & labels

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-TAX-020 | Week ticks shall start on the week boundary (`startOfWeek`); month/quarter/year ticks shall start on their period boundary. | Subdivisions must begin on natural period starts, not the view edge. | Analysis | timeAxis `*Ticks` |
| SRS-TAX-021 | The system shall thin each level after positioning, dropping any tick closer than the level's minimum spacing to the previously kept tick: major 60, minor 36, micro 16 device units. | Prevents overlapping, unreadable labels at any zoom. | Test (`thinTicks`) | layout §9; timeAxis `thinTicks` |
| SRS-TAX-022 | Tick thinning shall be applied **after** device mapping (it depends on `pxPerDay`/`ptPerDay`), not in abstract units. | Overlap is a device-pixel concern, not an abstract one. | Analysis | layout §9 |
| SRS-TAX-023 | On the week scale the minor level shall label each tick with its ISO-8601 week number as `W{n}`, not with a month-and-day pair. | Planners schedule, report and talk in week numbers ("W42"); a date restates the month band above it and cannot be matched to a calendar or a status report. | Test (`weekTicks` labels) | timeAxis `weekTicks`, `isoWeekNumber` |
| SRS-TAX-024 | ISO week numbering shall place a week in the year that owns its Thursday, so the turn of the year yields W52, W53 or W01 as ISO-8601 requires rather than a 53rd week that does not exist. | A week-number axis that disagrees with the organisation's calendar at year end is worse than no week numbers. | Test (year-boundary cases) | utils/dates `isoWeekNumber` |
| SRS-TAX-025 | The month band shall carry the year on the leading tick and on every January, as `Jan 2027`, and the month alone elsewhere. | The year on every month set the same four digits three times across a quarter, competing with the month name for the one strong band in the chart; January and the leading tick are the two places it actually resolves an ambiguity. | Test (`monthTicks` labels) | timeAxis `monthTicks` |

## Working & non-working days

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-TAX-030 | The system shall accept a `workingDays` set (subset of 0–6) and `holidays` (dates) in the calendar. | Plans differ on which days count as working. | Test: `tests/unit/persistence` (schema) | data-model (CalendarSettings); schema.ts |
| SRS-TAX-031 | On day/week scales the system shall shade non-working days (weekend mask) as a background hint, without affecting element positions. | Visual context for working vs non-working time. | Demo (weekend stripes) | visual-vocabulary; `GanttRenderer` showWeekends |

## Header integrity

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-TAX-040 | The chart body shall be clipped to the plot area itself — from the foot of the calendar header to the foot of the sheet — by a clip written in absolute coordinates and referenced from an element carrying no transform, so that the window cannot move with the scroll offset. | A `clip-path` resolves in the referencing element's own post-transform user space. Hung on the scrolled body group, a `y=0` window rode the scroll: bars painted over the calendar band, the foot of the plan went blank once the scroll passed the viewport height, and anything at negative local y was outside the window at every offset. | Test: `tests/e2e/chart-clipping.spec.ts`, `tests/unit/embedClip.test.ts` | `ChartArea` §clip layers; `embed/GanttRenderer`; SRS-TAX-002 |
| SRS-TAX-041 | The calendar header's height shall be the sum of the bands the axis actually built, and the same value shall feed the clip, the body transform, the dividing rule and the pointer arithmetic. | A constant 56 stood for a band sum that runs 44 (week) to 64 (day): the day-number band overflowed into the first row, every other scale left a dead strip, and the clip and the drag arithmetic could disagree about where the chart began. | Test: `tests/e2e/chart-clipping.spec.ts` (the band is read from the rendered rule) | `ChartArea` `AXIS_TOTAL_H`; `timeAxis` `AxisLevel.height` |

## Zoom

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-TAX-050 `[web]` | The chart's `wheel` handler shall be registered on the plot element as a non-passive native listener, so that Ctrl/Cmd + wheel zooms the chart and the browser's own page zoom does not also fire. | React registers `wheel` at the root with `passive: true`, which makes `preventDefault()` a no-op. The app read Ctrl + wheel and zoomed the chart while the browser page-zoomed the whole window at the same instant; the two fought and the owner saw the app's chrome grow instead of the plot. Shift + wheel only felt correct because its default — horizontal scroll on a surface with no horizontal overflow — had nothing left to do. | Test: `tests/unit/viewportZoom.test.ts` (listener contract), `tests/e2e/axis-zoom.spec.ts` | `useChartInteractions` §wheel; `ChartArea` |
| SRS-TAX-051 `[web]` | Every zoom path shall state the point it holds still: a wheel zoom shall hold the date under the pointer, and a zoom invoked without a pointer — the command bar's +/− buttons, the keyboard, the palette — shall hold the date at the middle of the plot. | "With the mouse scroll zoom, I expect to zoom where my mouse pointer is. That's the expectation." Only the wheel ever passed a real anchor; the button, key and palette paths all passed the literal `400`, which is the middle of the plot only on a 800px-wide chart and walks the view sideways on any other. | Test: `tests/unit/viewportZoom.test.ts` | `stores/viewportStore` `zoom`/`setChartWidth`; `useChartInteractions` §wheel |
| SRS-TAX-052 | An anchored zoom shall round the view's start date exactly once, from the closed form `start + anchor/pxPerDay − anchor/newPxPerDay`, and shall compute it from the clamped `pxPerDay` it actually applied. | The old arithmetic rounded twice — once to name the anchor date, once to place the new start — so each step lost up to a day at each end. Held against the 120 px/day ceiling that is 120px of drift per notch, and ten notches walked the anchor off the screen the owner was pointing at. | Test: `tests/unit/viewportZoom.test.ts` (the date under the anchor is unchanged across a zoom round trip) | `stores/viewportStore` `zoom` |

## Today marker

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-TAX-053 | The today marker shall cross the calendar bands as a 1px rule only, and its pill shall be drawn below the foot of the bands, riding the top of the plot area, pinned against the vertical scroll and inert to the pointer. | The pill was painted into the calendar band at every scale, so it buried whichever label it landed on — in the owner's capture it ate "Nov", leaving "ov". The bands are the one part of the chart that carries the calendar-true claim (SRS-TAX-002), so nothing may be allowed to paint over them. | Test: `tests/e2e/axis-zoom.spec.ts` (the pill's box is below the axis rule) | `ChartArea` §today; `docs/mockup/web-selection-and-axis-mockup.html` §06 |

## Delimiters and non-working days

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-TAX-054 | The calendar shall carry a `gridDelimiter` setting of `none`, `day`, `week`, `month` or `quarter`, and the plot shall draw a vertical rule on each boundary of the chosen unit, independent of the current time scale and zoom. | "I zoomed to a weekly level. There are no delimiters. I can see the scale, that is good, but I also need to be able to define the delimiters between the scales." The plot drew no vertical rule at all except the today line, which is also why a dragged milestone felt like it was floating: there was nothing under it to land on. The granularity is the planner's to pick and not inferred from the zoom, so month boundaries can be asked for while reading days (owner decision 2026-10-29). | Test: `tests/unit/axisGrid.test.ts` | `renderer/TimeAxis` `delimiterXs`; `ChartArea` §grid; `types/document` `GridDelimiter` |
| SRS-TAX-055 | Delimiters shall be suppressed entirely when the chosen unit would fall closer together than 4 device px, and the quarter unit shall take its boundaries from `fiscalYearStart`. | `pxPerDay` reaches 0.05, where day delimiters stand 1/20 px apart: drawing them is a solid block of ink over the whole plot and thousands of SVG nodes per frame. Suppressing is the honest answer — silently substituting a coarser unit would make the setting zoom-derived, which is the behaviour the owner rejected. | Test: `tests/unit/axisGrid.test.ts` (nothing at 0.05 px/day; quarter honours a July fiscal year) | `renderer/TimeAxis` `delimiterXs`; SRS-TAX-010 |
| SRS-TAX-056 | The calendar shall carry a `weekendDisplay` setting of `off` or `shaded`; when shaded, every run of consecutive non-working days shall be shaded as one band at every time scale where a day is at least 1.5 device px wide, at a contrast legible against the sheet. | The shading existed and could not be seen: `--color-weekend` is 2.5% black drawn at 0.4 opacity, which lands at one percent against the sheet, and it was gated to the day and week scales so at month scale or coarser it was simply absent. The `workingDays` control in the gear panel feeds nothing else in the web app, so until now toggling a day changed nothing a user could perceive. | Test: `tests/unit/axisGrid.test.ts` | `renderer/TimeAxis` `nonWorkingBands`; `ChartArea` §weekends; `CommandBar` `DocumentGroup`; SRS-TAX-031 |
| SRS-TAX-057 | `gridDelimiter` and `weekendDisplay` shall be optional in the schema, validated against their value sets when present, and read through one pair of defaults — `week` and `shaded` — when absent. | Every roadmap written before today has no such field, and the whole point of the setting is that it saves with the document. A required field would reject every existing file; a default read at each of the four call sites would drift. | Test: `tests/unit/axisGrid.test.ts`, `tests/unit/persistence.test.ts` | `persistence/schema` §calendar; `types/document` `DEFAULT_GRID_DELIMITER`/`DEFAULT_WEEKEND_DISPLAY` |
| SRS-TAX-059 | The standalone embed renderer shall derive weekend shading and delimiters from the same functions and the same document settings as the app, and shall gate them on px/day rather than on the scale name. | The embed walked the window itself, emitted one rect per weekend *day* — the seam SRS-TAX-056 exists to remove — at the old ~1% contrast, drew no delimiters at all, and switched on `scale === 'day' \|\| 'week'`, so a chart exported from a month view lost its weekends entirely. An exported chart that does not match the one on screen is a different chart. | Test: `tests/unit/embedGrid.test.ts` | `layout/grid` `delimiterXs`, `nonWorkingBands`; `embed/GanttRenderer`; SRS-TAX-054, SRS-TAX-056 |

## Live date feedback

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-TAX-058 `[web]` | While a bar, bracket or milestone is being dragged, a floating pill shall follow the pointer showing the date the release will commit — the start and end for a range, the single date for a point — formatted in the current scale's idiom, and shall never be clipped by the plot or intercept the pointer. | SR-IXC-03 and SR-IXC-04 require live numeric feedback on every date drag and the web surface has never had any: the bar moved and the planner learned the committed date only after letting go, which is the "surprise on release" those rows exist to forbid. The pill shows the snapped value, not the raw pointer delta, so what it reads is what gets written (SRS-EDIT-002). | Test: `tests/unit/axisGrid.test.ts` (`formatForScale`), `tests/e2e/drag-date-pill.spec.ts` | `renderer/TimeAxis` `DragDatePill`/`formatForScale`; `useChartInteractions` `dragPill`; SR-IXC-03, SR-IXC-04, SR-IXC-12 |

## Open items

- Per-scale axis-level fixtures are added in F2 (conformance breadth); SRS-TAX-002
  and -021 cite them.
- SRS-TAX-058's pill is wired to the drag state the chart already publishes.
  Bar-resize and progress drags gain their own readouts with the drag work
  (SR-IXC-06).
- Distinct holiday shading remains open: `holidays` are carried and validated
  (SRS-TAX-030) but SRS-TAX-056 shades only the `workingDays` mask.
- Fiscal-aligned **year** boundaries (not just labels) are a possible future
  enhancement; today year ticks are calendar-aligned (SRS-TAX-011).
- Week numbering is ISO-8601 (Monday-start, Thursday rule). A `weekNumbering`
  calendar setting for organisations on a US or fiscal week scheme is not
  implemented; SRS-TAX-023/-024 describe ISO only.
