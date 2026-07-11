# PowerPlanner Specification (the concept layer)

This directory is the **single source of truth** for what a PowerPlanner Gantt
chart *is* — independent of any rendering technology. It is the generic
concept/design layer. Everything else is an *implementation* of this spec:

```
spec/            ← the concept (this directory) — language-neutral
  srs/                   REQUIREMENTS (ASPICE-style): what the app shall do (the foundation)
    SRS-<feature>.md       requirement tables (shall-statements, rationale, verification, trace)
  data-model.md          DESIGN: entities, fields, invariants
  layout.md              DESIGN: the projection/layout algorithm in ABSTRACT coordinates
  visual-vocabulary.md   DESIGN: how each element is drawn (shape, size, color, labels)
  interaction.md         DESIGN: selection + editing model (incl. future on-slide UI)
  schema/document.schema.json   machine-readable data contract (JSON Schema)
  fixtures/              golden documents + expected layout (conformance tests)

src/             ← implementation A — WEB (React 19 + TypeScript + SVG). Reference impl.
native/          ← implementation B — POWERPOINT (C++ COM add-in, think-cell style)
```

## Two implementations, one concept

| Concern            | Web (`src/`)                          | PowerPoint (`native/`)                 |
|--------------------|---------------------------------------|----------------------------------------|
| Language           | TypeScript                            | C++ (ATL/COM)                          |
| Output             | SVG in a pan/zoom viewport            | Native shapes on a fixed slide canvas  |
| Data model         | `src/types/document.ts`               | `native/.../GanttModel.*`              |
| Layout algorithm   | `src/layout/engine.ts`                | `native/.../GanttLayout.*`             |
| Validation         | `src/persistence/schema.ts`           | (validates against the same JSON Schema)|

Both MUST implement `data-model.md` + `layout.md` identically, and both MUST
pass `fixtures/`. The web app is the **reference implementation**: when the spec
and the web app disagree, that is a bug in one of them, to be reconciled — the
spec is not free to drift from the proven reference, and the reference is not
free to drift from the spec.

## The key idea: abstract layout coordinates

The layout algorithm is defined in **device-neutral units**, never pixels or
points:

- **Time (x):** the unit is **one day**. `x = dayOffset(date) = days between the
  view start and the date`. An implementation multiplies by a device scale to
  get its own units: web uses `pxPerDay`, PowerPoint uses `ptPerDay`.
- **Rows (y):** the unit is **one row slot**. Layout produces a `rowIndex` and a
  `subRow` per item. An implementation multiplies the row slot by its own
  `rowHeight` (web px / PowerPoint pt) to get a device coordinate.

So the canonical algorithm emits `{ rowIndex, subRow, startDayOffset,
endDayOffset, widthDays }`-style results; **device mapping is a separate, final
step each implementation owns**. This is already how the web engine works
(`layoutDocument` takes `pxPerDay` as a parameter; passing `1` yields abstract
day units) and how the Office.js bridge worked (it reused the engine, then
scaled to points). The spec just makes the contract explicit so the C++ port
follows it rather than re-deriving it.

## Conformance (the teeth)

`fixtures/` pairs a canonical input document with its expected abstract layout
(computed at device scale = 1). Each implementation runs the same fixtures:

- Web: `tests/unit/spec-conformance.test.ts` (Vitest)
- PowerPoint: a small C++ conformance check over the same JSON fixtures

Same inputs → same expected outputs → the two implementations stay locked
together. A spec change is not done until the fixtures and both implementations
agree.

## Versioning

The data contract carries `schemaVersion` (currently `1`). A breaking change to
`data-model.md` or `schema/document.schema.json` bumps that version, and both
implementations migrate together.

## Requirements Layering (SRS)

`spec/srs/` holds **shared** ASPICE-style requirement tables that apply to both
implementations (web + native). These are the foundation "shall" statements.

Native-specific requirements (overlay, on-slide app bar, shape emission details,
selection suppression for child shapes, sticky docking, theme-coherent chrome
and menus, lifecycle, etc.) live in `spec/srs-native/` (table format identical
to `spec/srs/`). 

- Never author primary SRS "shall" statements in `docs/`.
- `docs/` holds supporting material: guides, architecture notes, UX inventories,
  mockups, and iteration plans.
- When user feedback or analysis yields a new requirement, register the work
  item in `PLAN.md` first, then add the table entry in the right SRS location,
  plus e2e harness coverage for native changes.

See `spec/srs/README.md` for the exact table format and ID rules. The same
format applies under `spec/srs-native/`. All agents must follow this structure.
