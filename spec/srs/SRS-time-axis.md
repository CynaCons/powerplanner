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

## Open items

- Per-scale axis-level fixtures are added in F2 (conformance breadth); SRS-TAX-002
  and -021 cite them.
- Fiscal-aligned **year** boundaries (not just labels) are a possible future
  enhancement; today year ticks are calendar-aligned (SRS-TAX-011).
- Week numbering is ISO-8601 (Monday-start, Thursday rule). A `weekNumbering`
  calendar setting for organisations on a US or fiscal week scheme is not
  implemented; SRS-TAX-023/-024 describe ISO only.
- Weekend shading (SRS-TAX-031) is implemented; distinct **holiday** shading is a
  separate future item — `holidays` are carried and validated (SRS-TAX-030) but
  not yet rendered distinctly from the weekend mask.
