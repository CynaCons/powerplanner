# SRS - Native Interaction Reactivity & Smoothness

Native-specific requirements for making the PowerPoint on-slide Gantt editor
feel immediate and stable during edits. User feedback from 2026-07-11 stated
that the editor looked right but did not feel reactive, smooth, or intuitive.
This SRS makes responsiveness a specified, measured property.

This file reformats the legacy smoothness prose requirements for v2.6.8 and
renames the file to the native SRS hyphen convention.

Traces up to: `../srs/SRS-editing.md`, `../interaction.md`,
`docs/onslide-v4-plan.md`, `docs/improvements-backlog.md`,
`spec/srs-native/SRS-OverlayLifecycle.md`,
`spec/srs-native/SRS-CreationFlows.md`,
`spec/srs-native/SRS-InteractionConventions.md`, and
`native/tools/harness_driver.py`.

Reference impl: `native/PowerPlannerAddin/` (Overlay.cpp, GanttBuilder.cpp,
GanttScene*, reconcile/update paths, event sink, inline/card editors, drag
commit paint).

## In-Place Mutation And Latency

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-SMO-01 | `UpdateGantt` shall update existing shapes in place and create or delete only elements added or removed by the operation, while preserving FITPERSIST and PP_PROJ semantics. | Delete-all rebuilds create COM churn, visible reflow, and flicker for simple edits. | Reconcile assertions compare shape count deltas, untouched shape ids, and z-order stability after single-element operations. | Legacy SR-SMO-01; UpdateGantt; reconcile path |
| SR-SMO-02 | Command dispatch to stable final visual state for single-element operations on the sample document shall complete within 200 ms. | Single edits should feel instant and stay below a quarter-second perception threshold. | Trace timestamps compute `opLatencyMs`; invariant `op_latency_budget` applies to nudge, percent, color, rename, and drag commit profiles. | Legacy SR-SMO-02; task-nudge-latency; task-color-latency |
| SR-SMO-03 | Single-element operations shall not call `LockWindowUpdate` on the PowerPoint window. | Whole-window freezes hide churn but make the host feel unresponsive. | Code review and latency traces assert no global freeze for single-element ops; structural rebuild exceptions are documented. | Legacy SR-SMO-03; mutation paths |

## Immediate Feedback

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-SMO-04 | Hover cursor and wash feedback shall update on the `WM_MOUSEMOVE` path rather than waiting for the 150 ms tick. | Hover must track the pointer frame-by-frame for the surface to feel direct. | Mouse-move traces capture immediate cursor/wash change before next polling tick. | Legacy SR-SMO-04; Overlay WndProc |
| SR-SMO-05 | The add-in shall subscribe to `WindowSelectionChange` so native selection changes update chrome within one repaint, with the tick retained only as a watchdog/fallback. | Native selection made through PowerPoint should be reflected without polling delay. | Event-sink trace asserts selection chrome updates on event; failure path degrades to poll-only. | Legacy SR-SMO-05; COM event sink; Overlay Tick fallback |
| SR-SMO-06 | On drag commit, the overlay shall keep painting the committed geometry until native shapes report the new state. | Users must not see the old position reappear or a blank gap after release. | `drag-commit-echo` screenshot invariant rejects intermediate frames matching old geometry after commit. | Legacy SR-SMO-06; drag ghost/commit echo |

## Editing Semantics

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-SMO-07 | Double-clicking a bar label, row label, milestone label, marker label, or note label shall open an inline editor positioned on that label. | Rename should occur in place on the element instead of through a detached card-first flow. | Inline rename traces and UX walkthrough; card editor remains available for properties. | Legacy SR-SMO-07; SRS-InteractionConventions SR-IXC-16 |
| SR-SMO-08 | Smoothness changes shall preserve one undo entry per gesture. | Responsiveness improvements must not fragment the user's undo stack. | Undo-stack tests or manual Office verification after nudge, color, rename, and drag commit. | Legacy SR-SMO-08; command dispatch/undo integration |

## Open / Related

- Structural rebuilds such as row insert/delete and scale changes may retain
  temporary masking until the in-place reconcile path covers them.
- Event sink failures must degrade gracefully to the existing polling path.
