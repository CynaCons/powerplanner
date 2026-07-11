# Software Requirements Specification (SRS)

The requirements foundation, ASPICE-style. Each `SRS-<feature>.md` states what
PowerPlanner **shall** do for one feature, as a table of uniquely-identified,
verifiable, traceable requirements. Both implementations (web `src/`, native
`native/`) must satisfy these and trace back to them.

This layer sits **above** the design specs (`../data-model.md`, `../layout.md`,
`../visual-vocabulary.md`, `../interaction.md`): SRS = *what*, design specs =
*how*. Requirements trace down to design, to the JSON Schema, to conformance
fixtures, and to tests.

## Requirement ID scheme

`SRS-<FEAT>-<NNN>` — `FEAT` is the feature's short tag, `NNN` a zero-padded
sequence (never reused; deleted requirements leave a gap). Example:
`SRS-ELEM-010`.

## Requirement table format

| Column        | Meaning                                                                 |
|---------------|-------------------------------------------------------------------------|
| **ID**        | `SRS-<FEAT>-<NNN>`, stable forever.                                      |
| **Requirement** | A single "shall" statement — one testable behaviour.                  |
| **Rationale** | Why it exists (intent, not restatement).                                |
| **Verification** | How it's confirmed: `Test` (+ fixture/test ref), `Analysis`, `Review`, `Demo`. |
| **Trace**     | Down-links: design `§`, schema, fixture, impl file(s), test.            |

Rules: one shall per row; verifiable (no "fast", "nice"); implementation-neutral
unless the feature is platform-specific (then note `[web]` / `[native]`).
Priority is implied by phase ordering in `PLAN.md`, not a column.

## Verification methods

- **Test** — automated (Vitest for web, C++ conformance for native, Playwright
  for workflows). Prefer this; cite the fixture/test.
- **Analysis** — argued from the algorithm/spec where a test is impractical.
- **Review** — confirmed by inspection against the design spec.
- **Demo** — shown working in the host app (e.g. PowerPoint) when no automated
  hook exists yet (record what was demonstrated).

## Feature index

| SRS file                       | Feature tag(s)  | Status  |
|--------------------------------|-----------------|---------|
| `SRS-chart-elements.md`        | `ELEM`          | drafted |
| `SRS-time-axis.md`             | `TAX`           | drafted |
| `SRS-layout.md`                | `LAY`           | drafted |
| `SRS-editing.md`               | `EDIT`          | drafted |
| `SRS-persistence.md`           | `PERS`          | drafted |
| `SRS-critical-path.md`         | `CPM`           | drafted |
| `SRS-baseline.md`              | `BASE`          | drafted |
| `SRS-theming-export.md`        | `THEME`, `EXP`  | drafted |
| `SRS-powerpoint.md` `[native]` | `PPT`           | drafted |

The visual specification is generated (see below), not a separate SRS file.

Native-only requirements are maintained in the sibling directory `../srs-native/` using the identical table format. See `../srs-native/README.md`.

## Visual specification

The foundation includes a **visual** specification (`../visual/`) — reference
figures that show exactly how a chart should look. They are **generated from the
web reference implementation** (`scripts/gen-visual-spec.ts` renders each fixture
through `<GanttRenderer>` → `spec/visual/*.svg`; `npm run gen:visual`), so they
cannot drift from what the app produces. A fixture is the input, its
`*.expected.json` is the machine-checked output, and its `*.svg` is the
human-checkable visual. `../visual-vocabulary.md` holds the textual visual rules;
annotated callouts keyed to requirement IDs come later (F1/F2).

## Traceability

Bidirectional: every requirement has down-links in its **Trace** column; every
conformance fixture and test should name the requirement IDs it covers. A
requirement with no verification is not "done".

## Quality status

All 9 SRS files passed a two-round independent ASPICE audit (requirement
quality, ID integrity, format consistency, traceability, and accuracy vs. the
actual source). Code-accuracy mismatches were corrected so requirements describe
what the code does. Two items are stated but **not yet enforced**, and are
disclosed in the relevant open-items rather than misstated as done:

- `end >= start` is an invariant (SRS-ELEM-002) not yet enforced at load — to be
  enforced when the JSON Schema becomes the runtime validation source (Phase F2).
- Element `color` is canonically `#RRGGBB` in the JSON Schema, but the current
  runtime validator does not check it (same F2 deferral).
- Distinct **holiday** shading (vs. the weekend mask) is a future item; weekend
  shading (SRS-TAX-031) is implemented.
