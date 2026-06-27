# SRS — PowerPoint Integration `[native]`

Requirements specific to the native PowerPoint add-in: emitting native shapes,
round-tripping, and on-slide editing. Feature tag: `PPT`. These refine the
generic features for the PowerPoint surface; they do not apply to the web
implementation.

Traces up to: `../interaction.md`, `../layout.md`, `../visual-vocabulary.md`.
Reference impl: `native/` (`docs/native-addin.md`).

## Loading & invocation

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-PPT-001 | The add-in shall load into PowerPoint desktop at startup and present a "PowerPlanner" ribbon tab. | The user needs a discoverable entry point. | Demo (verified, N1) | `native/PowerPlannerAddin/Connect.cpp`; docs/native-addin.md |
| SRS-PPT-002 | The add-in shall register per-user without administrator rights. | Lower install friction; matches think-cell-style deployment. | Demo (HKCU registry, verified N1) | `native/PowerPlannerAddin/dllmain.cpp` (`AtlSetPerUserRegistration`); `Connect.rgs` |

## Native emission

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-PPT-010 | "Insert Gantt" shall emit the chart as **native, editable** PowerPoint shapes (rectangles, diamonds, elbow connectors, text boxes) — no images or embedded SVG. | Native shapes remain editable after the add-in is uninstalled; that is the core value. | Demo | `native/PowerPlannerAddin/GanttBuilder.cpp`; visual-vocabulary mapping |
| SRS-PPT-011 | Shape positions shall be computed by a C++ layout that conforms to `../layout.md`, mapping abstract days/row-slots to slide points (`ptPerDay`). | The PowerPoint chart must match the web chart's geometry. | Test: native conformance over `spec/fixtures/*` at ptPerDay=1 (N2) | layout; SRS-LAY-001 |
| SRS-PPT-012 | Every emitted shape shall be tagged (`PP_KIND`, `PP_ID`) and grouped under a single chart root. | Enables hit-testing, identity, and round-trip. | Demo | interaction §identity |
| SRS-PPT-013 | Colour shall be converted from the document `#RRGGBB` to PowerPoint BGR by the add-in. | PowerPoint stores colour as BGR; conversion is the surface's job. | Test (hex→bgr unit) | visual-vocabulary |

## Round-trip

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-PPT-020 | On insert, the add-in shall store the serialized `GanttDocument` on the chart root (`PP_DOC` tag), validatable against `spec/schema`. | The slide carries its own source of truth. | Test (PP_DOC parses + validates) | SRS-PERS-001 |
| SRS-PPT-021 | "Pull from slide" shall reconstruct the exact `GanttDocument` from a chart group's `PP_DOC`. | Edit-from-slide and re-insert require lossless read-back. | Test (insert → pull deep-equals) | interaction §round-trip |

## On-slide editing (staged)

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-PPT-030 | The add-in shall draw selection handles and contextual controls over the slide, aligned to chart shapes and tracking PowerPoint zoom/scroll/window changes. | think-cell-style on-slide editing, not a task pane. | Demo (N4) | interaction §PowerPoint surface |
| SRS-PPT-031 | Editing a chart shape on the slide shall update the model and reflow dependent elements, keeping `PP_DOC` in sync. | Shapes and data must not diverge. | Demo (N5) | interaction §agents; SRS-EDIT-001 |

## Notes

- Verification "Demo" here means confirmed in PowerPoint desktop (no automated
  Office UI hook yet); record what was shown. N-phase tags map to PLAN.md Phase 9.
