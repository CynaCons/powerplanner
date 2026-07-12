# Data Model

The canonical document. This mirrors `src/types/document.ts` and is enforced by
`src/persistence/schema.ts` and `schema/document.schema.json`. Dates are ISO
`YYYY-MM-DD` strings (`ISODate`). All `id` fields are opaque unique strings.

## GanttDocument (root)

| Field          | Type                 | Required | Notes                                    |
|----------------|----------------------|----------|------------------------------------------|
| `schemaVersion`| `1`                  | yes      | Exactly `1`. Bumped on breaking changes. |
| `title`        | string               | yes      | Chart title.                             |
| `calendar`     | CalendarSettings     | yes      | Scale + fiscal/working-day config.       |
| `rows`         | Row[]                | yes      | Ordered; render order = array order.     |
| `tasks`        | Task[]               | yes      |                                          |
| `milestones`   | Milestone[]          | yes      |                                          |
| `brackets`     | Bracket[]            | yes      |                                          |
| `dependencies` | Dependency[]         | yes      |                                          |
| `markers`      | Marker[]             | yes      |                                          |
| `style`        | StyleSettings        | yes      |                                          |
| `baseline`     | BaselineSnapshot     | no       | Optional captured schedule for drift.    |
| `axisNumbering`| `day` \| `cw`        | no       | Bottom-axis labels. Omitted means `day`; `cw` uses ISO calendar-week numbers for week/day scales. |
| `windowStart`  | ISODate              | no       | Explicit time-window start. Omitted (with `windowEnd`) means auto-fit: `[min(dates) − 5%pad .. max(dates) + 5%pad]` derived from content. Canonical JSON omits the empty value. |
| `windowEnd`    | ISODate              | no       | Explicit time-window end (inclusive; `windowEnd >= windowStart + 1 visible scale unit`). Set/cleared as a pair with `windowStart`. Elements outside the window are clipped/hidden at render time only — the document keeps everything (see `srs-native/SRS-TimeWindow.md`). |

## CalendarSettings

| Field            | Type      | Required | Constraints                                  |
|------------------|-----------|----------|----------------------------------------------|
| `scale`          | TimeScale | yes      | `day` \| `week` \| `month` \| `quarter` \| `year` |
| `fiscalYearStart`| number    | yes      | 1–12 (month). `1` = calendar year.           |
| `workingDays`    | number[]  | yes      | Each 0–6 (0 = Sunday).                        |
| `holidays`       | ISODate[] | yes      | Non-working dates.                           |

## Row

| Field      | Type             | Required | Notes                                        |
|------------|------------------|----------|----------------------------------------------|
| `id`       | string           | yes      |                                              |
| `label`    | string           | yes      |                                              |
| `groupId`  | string \| null   | yes      | Parent row id, or `null` for a top-level row.|
| `collapsed`| boolean          | no       | When a parent is collapsed, its child rows are hidden. |

A **summary/group row** is any row that is referenced as the `groupId` of one or
more other rows. Its bar is *derived* (see `layout.md`), not stored.

## Task

| Field            | Type           | Required | Constraints                         |
|------------------|----------------|----------|-------------------------------------|
| `id`             | string         | yes      |                                     |
| `rowId`          | string         | yes      | Must reference an existing Row.     |
| `label`          | string         | yes      |                                     |
| `start`          | ISODate        | yes      |                                     |
| `end`            | ISODate        | yes      | Inclusive; `end >= start`.          |
| `percentComplete`| number         | no       | 0–100.                              |
| `color`          | string (hex)   | no       | `#RRGGBB`. Implementation default if absent. |
| `labelPlacement` | LabelPlacement | no       | `on-bar`\|`left`\|`right`\|`above`\|`below`\|`hidden` |
| `notes`          | string         | no       |                                     |

A task's date span is **inclusive of `end`**: a task `start=end` occupies one
whole day. (See `layout.md` width rule.)

## Milestone

| Field           | Type           | Required | Notes                          |
|-----------------|----------------|----------|--------------------------------|
| `id`            | string         | yes      |                                |
| `rowId`         | string         | yes      | References a Row.              |
| `label`         | string         | yes      |                                |
| `date`          | ISODate        | yes      | Single anchor date.            |
| `labelPlacement`| LabelPlacement | no       |                                |
| `color`         | string (hex)   | no       |                                |

## Bracket

A labelled span across one or more rows.

| Field    | Type         | Required | Notes                              |
|----------|--------------|----------|------------------------------------|
| `id`     | string       | yes      |                                    |
| `label`  | string       | yes      |                                    |
| `start`  | ISODate      | yes      |                                    |
| `end`    | ISODate      | yes      | Inclusive.                         |
| `rowIds` | string[]     | yes      | Rows the bracket spans vertically. |
| `color`  | string (hex) | no       |                                    |

## Dependency

| Field  | Type           | Required | Notes                                          |
|--------|----------------|----------|------------------------------------------------|
| `id`   | string         | yes      |                                                |
| `from` | string         | yes      | Predecessor task id.                           |
| `to`   | string         | yes      | Successor task id.                             |
| `type` | DependencyType | yes      | `finish-to-start`\|`start-to-start`\|`finish-to-finish`\|`start-to-finish` |

The `type` selects which ends of the two task bars the link connects
(see `layout.md`).

## Marker

A free-standing vertical annotation at a date.

| Field   | Type       | Required | Notes                               |
|---------|------------|----------|-------------------------------------|
| `id`    | string     | yes      |                                     |
| `type`  | MarkerType | yes      | `today` \| `deadline` \| `note`     |
| `label` | string     | yes      |                                     |
| `date`  | ISODate    | yes      |                                     |
| `color` | string     | no       | Any CSS color string (markers may use named colors), unlike element `color` which is strict `#RRGGBB`. |

## StyleSettings

| Field    | Type      | Required | Notes                         |
|----------|-----------|----------|-------------------------------|
| `theme`  | ThemeName | yes      | `light` \| `dark` \| `print`  |
| `preset` | string    | yes      | Named style preset id.        |

## BaselineSnapshot (optional)

| Field        | Type                                  | Notes                          |
|--------------|---------------------------------------|--------------------------------|
| `capturedAt` | ISODate                               | When the snapshot was taken.   |
| `tasks`      | `{ id, start, end }[]`                 | Captured task spans for drift. |

## Referential invariants

- Every `task.rowId` / `milestone.rowId` references an existing `Row`.
- Every `bracket.rowIds[*]` references an existing `Row`.
- Every `dependency.from` / `.to` references an existing `Task`.
- `row.groupId`, when non-null, references an existing `Row`.
- Items whose row reference is missing/hidden are **omitted from layout**, not an
  error (see `layout.md`).
