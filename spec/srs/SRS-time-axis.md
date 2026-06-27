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

## Working & non-working days

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-TAX-030 | The system shall accept a `workingDays` set (subset of 0–6) and `holidays` (dates) in the calendar. | Plans differ on which days count as working. | Test: `tests/unit/persistence` (schema) | data-model (CalendarSettings); schema.ts |
| SRS-TAX-031 | On day/week scales the system shall shade non-working days (weekend mask) as a background hint, without affecting element positions. | Visual context for working vs non-working time. | Demo (weekend stripes) | visual-vocabulary; `GanttRenderer` showWeekends |

## Open items

- Per-scale axis-level fixtures are added in F2 (conformance breadth); SRS-TAX-002
  and -021 cite them.
- Fiscal-aligned **year** boundaries (not just labels) are a possible future
  enhancement; today year ticks are calendar-aligned (SRS-TAX-011).
- Weekend shading (SRS-TAX-031) is implemented; distinct **holiday** shading is a
  separate future item — `holidays` are carried and validated (SRS-TAX-030) but
  not yet rendered distinctly from the weekend mask.
