# Conformance Fixtures

Each fixture is a pair:

- `<name>.json` — a canonical input: `{ name, view: { viewStart, scale }, document }`
  where `document` is a valid `GanttDocument` (see `../schema/document.schema.json`).
- `<name>.expected.json` — the expected **abstract** layout (device scale = 1; one
  day = one x-unit, one row slot = one y-unit), as defined in `../layout.md`.

Every implementation runs the fixtures and must reproduce the expected output:

- **Web** (`src/`): `tests/unit/spec-conformance.test.ts` (Vitest) runs the engine
  at `pxPerDay = 1`, normalizes row offsets by `ROW_HEIGHT`, and deep-equals the
  expected file. This also keeps the golden honest (it is verified against the
  reference engine, not hand-maintained).
- **PowerPoint** (`native/`): a small C++ check loads the same JSON fixtures, runs
  the layout port at `ptPerDay = 1`, and compares — added with the N2/N3 layout
  port.

To add a fixture: write the input, run the web conformance test once to obtain the
exact abstract output, record it as `<name>.expected.json`, then add the same
fixture to the native check. A spec change is "done" only when both implementations
pass all fixtures.
