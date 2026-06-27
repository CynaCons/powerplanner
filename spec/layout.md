# Layout Algorithm

The projection from a `GanttDocument` to positioned elements, in **abstract
coordinates** (days and row slots — never pixels or points). This is the exact
algorithm in `src/layout/engine.ts` and `src/layout/timeAxis.ts`, lifted to
device-neutral terms.

## Coordinate model

- **x — time.** Unit: one day. `xDay(date) = diffDays(viewStart, date)`, the
  signed count of days from the view start to the date. `viewStart` is an input
  (the left edge of the visible window).
- **y — rows.** Unit: one row slot. Layout assigns each item a `rowIndex` (which
  visible row) and a `subRow` (which stacking track within that row).

A device maps abstract → device coordinates as the final step it owns:

| Abstract            | Web (`src/`)                       | PowerPoint (`native/`)            |
|---------------------|------------------------------------|-----------------------------------|
| `xDay` (days)       | `xDay * pxPerDay`                  | `xDay * ptPerDay`                 |
| one row slot        | `ROW_HEIGHT = 36` px               | `ROW_HEIGHT_PT` (slide pt)        |
| bar thickness       | `BAR_HEIGHT = 22` px               | proportional in pt                |
| milestone size      | `MILESTONE_SIZE = 14` px           | proportional in pt                |

The numbers above are the **web device profile**. They are NOT part of the
abstract algorithm; each implementation picks its own device profile. Conformance
fixtures are expressed at scale = 1 (one day = one x-unit, one row slot = one
y-unit) so they are device-independent.

## Inputs

```
layout(doc, viewStart, scale=1, allowOverlap=false)
```

- `doc` — the GanttDocument
- `viewStart` — ISO date at x = 0
- `scale` — device scale (days→units); `1` for abstract/conformance
- `allowOverlap` — when true, skip sub-row stacking (all tasks on subRow 0)

## Step 1 — Visible rows

```
collapsedGroups = { row.id : row.collapsed == true }
visibleRows     = rows where row.groupId is null OR row.groupId not in collapsedGroups
rowIndex(id)    = position of the row in visibleRows (0-based)
```

Render order is the document's `rows` array order, minus hidden children of
collapsed groups.

## Step 2 — Sub-row stacking (overlap → tracks)

For each visible row, lay its tasks onto stacking tracks (greedy, first-fit):

```
tasksInRow = doc.tasks where rowId == row.id, sorted by start ascending
tracks = []                       # each track holds the end date occupying it
for t in tasksInRow:
    placed = first track i where tracks[i] <= t.start   # free by t's start
    if found: tracks[i] = t.end ; subRow(t) = i
    else:     tracks.push(t.end) ; subRow(t) = len(tracks)-1
subRowCount(row) = max(1, len(tracks))
```

If `allowOverlap`, every task gets `subRow = 0` and `subRowCount = 1`.

Comparison of dates uses ISO string ordering (lexicographic == chronological for
`YYYY-MM-DD`).

## Step 3 — Row heights and offsets

```
rowSlots(i)  = max(1, subRowCount(i))         # in row-slot units
rowOffset(0) = 0
rowOffset(i) = rowOffset(i-1) + rowSlots(i-1)  # cumulative, in row-slot units
chartRows    = sum of rowSlots                  # total height in row-slot units
```

(The web impl multiplies row slots by `ROW_HEIGHT`; abstract stays in slots.)

## Step 4 — Tasks

For each task whose `rowId` is a visible row:

```
x          = xDay(task.start)
endX       = xDay(task.end)
widthDays  = max(2/scale, endX - x + 1)        # inclusive end → +1 day
rowIndex   = rowIndex(task.rowId)
subRow     = subRow(task.id)
yTop(slot) = rowOffset(rowIndex) + subRow      # top of the bar's slot, in slots
```

The `+1` (one day) makes `start == end` a one-day-wide bar. The web impl writes
this as `endX - x + pxPerDay` and centers the bar of `BAR_HEIGHT` within the
`ROW_HEIGHT` slot: `y = rowOffset + subRow*ROW_HEIGHT + (ROW_HEIGHT-BAR_HEIGHT)/2`.

Tasks whose `rowId` is not visible are omitted.

## Step 5 — Milestones

For each milestone whose `rowId` is visible:

```
x        = xDay(milestone.date)
rowIndex = rowIndex(milestone.rowId)
yCenter  = rowOffset(rowIndex) + 0.5           # vertical center of the row, in slots
```

Rendered as a diamond centered on `(x, yCenter)`.

## Step 6 — Summary (group) bars

A summary bar is derived for every row that is a parent (i.e., appears as some
other row's `groupId`) AND is itself visible AND has at least one descendant
task:

```
children   = rows whose groupId == parent.id
childTasks = tasks whose rowId in children
if childTasks empty: no summary bar
start = min(childTasks.start)        # ISO min
end   = max(childTasks.end)          # ISO max
x        = xDay(start)
widthDays= max(2/scale, xDay(end) - xDay(start) + 1)
rowIndex = rowIndex(parent.id)
yCenter  = rowOffset(rowIndex) + 0.5     # web nudges up by 4px: ... - 4
```

## Step 7 — Brackets

For each bracket:

```
x         = xDay(bracket.start)
widthDays = max(2/scale, xDay(bracket.end) - xDay(bracket.start) + 1)
rowIdxs   = bracket.rowIds mapped through rowIndex (missing → 0)
topRow    = min(rowIdxs)
bottomRow = max(rowIdxs)
```

The bracket spans vertically from `topRow` to `bottomRow`.

## Step 8 — Dependencies

For each dependency whose `from` and `to` tasks are both laid out:

```
fromX = (type is finish-to-start OR finish-to-finish) ? from.x + from.widthDays : from.x
toX   = (type is finish-to-start OR start-to-start)   ? to.x                     : to.x + to.widthDays
fromY = vertical center of the from bar
toY   = vertical center of the to bar
midX  = (fromX + toX) / 2
route = orthogonal: (fromX,fromY) → (midX,fromY) → (midX,toY) → (toX,toY)
```

The elbow routes horizontally to the midpoint x, then vertically, then
horizontally to the target — a 3-segment orthogonal connector. Web emits this as
an SVG path; PowerPoint emits an elbow connector with an arrowhead at `to`.

## Step 9 — Time axis ticks

Two or three stacked tick levels depending on `scale`
(`src/layout/timeAxis.ts`):

| scale   | major level | minor level | micro level |
|---------|-------------|-------------|-------------|
| day     | month       | week        | day         |
| week    | month       | week        | —           |
| month   | quarter     | month       | —           |
| quarter | year        | quarter     | —           |
| year    | year        | year        | —           |

- Week ticks start on `startOfWeek`; month/quarter/year on their period starts.
- Quarter and year labels honor `fiscalYearStart` (`Q{q} {fy}`, `FY{year}` when
  `fiscalYearStart != 1`).
- **Tick thinning:** after positioning, drop any tick closer than the minimum
  device spacing to the previously kept tick — major `60`, minor `36`, micro
  `16` (web px). Thinning is device-dependent and applied after device mapping.

## Determinism

Given the same `doc`, `viewStart`, `scale`, and `allowOverlap`, layout is fully
deterministic. No randomness, no wall-clock (the `today` marker takes its date
from the document/marker, not the system clock, for layout purposes).
