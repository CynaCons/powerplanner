# SRS - Native Interaction Reactivity & Smoothness

Native-specific requirements for making the PowerPoint on-slide Gantt editor
feel immediate and stable during edits. User feedback from 2026-07-11 stated
that the editor looked right but did not feel reactive, smooth, or intuitive.
This SRS makes responsiveness a specified, measured property.

This file reformats the legacy smoothness prose requirements for v2.6.8 and
renames the file to the native SRS hyphen convention.

Traces up to: `../srs/SRS-editing.md`, `../interaction.md`,
`docs/archive/onslide-v4-plan.md` (historical), `docs/improvements-backlog.md`
(NTS-07), `docs/phase13-quality-program.md`, `PLAN.md` Phase 13 v2.8.1,
`spec/srs-native/SRS-OverlayLifecycle.md` (IDLESTABLE),
`spec/srs-native/SRS-CreationFlows.md`,
`spec/srs-native/SRS-InteractionConventions.md` (live preview SR-IXC-03..06),
and `native/tools/harness_driver.py`.

Reference impl: `native/PowerPlannerAddin/` (Overlay.cpp, GanttBuilder.cpp,
GanttScene*, reconcile/update paths, event sink, inline/card editors, drag
commit paint, render counters / paint timestamp instrumentation).

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

## Continuous Paint Cadence (Phase 13 / v2.8.1)

SR-SMO-02 measures **single-op dispatch → stable visual** latency. These
requirements measure **paint cadence during continuous interaction** (drag /
hover), which is a separate feel axis. IDLESTABLE (overlay lifecycle) still
requires **zero** paints when idle and unhovered.

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-SMO-09 | While an overlay drag gesture is active (task move/resize, progress edge, marker move, window edge, create-drag, link rubber-band), the overlay shall repaint at a sustained cadence of **at least 30 paints per second** on the reference harness machine, measured over a window of at least 300 ms of continuous pointer motion. | Below ~30 Hz the ghost/pill lags the pointer and the surface feels sticky even if post-commit op latency is under 200 ms. | Harness continuous-feel profile samples paint timestamps during mid-gesture drag; driver invariant `paint_cadence_min_hz` asserts effective Hz ≥ 30 (or records intentional RED with measured Hz). | PLAN v2.8.1; Overlay paint path; render counters |
| SR-SMO-10 | Where the host and GPU allow, the implementation shall **target 60 paints per second** during active drag; failure to reach 60 Hz is not a gate failure if SR-SMO-09 holds, but measured p50/p95 paint intervals shall be reported. | 60 Hz matches common display refresh and live-preview expectations; the floor remains 30 Hz so CI stays realistic under COM cost. | Same continuous-feel profile reports p50/p95 interval and achieved Hz; aspirational only. | PLAN v2.8.1 |
| SR-SMO-11 | When no drag is active and the chart is idle (no hover transition requiring chrome update), overlay and app-bar paint counts shall not increase over a multi-second sample (IDLESTABLE). Continuous-cadence instrumentation must not force idle paints. | Continuous metrics must not regress the paint-free idle contract. | Existing IDLESTABLE stage + counters before/after idle sleep remain green after cadence instrumentation. | SRS-OverlayLifecycle; overlay_lifecycle |
| SR-SMO-12 | Continuous-cadence measurement shall be exposed to harnesses via paint timestamps or interval samples (extend `Overlay_GetRenderCountersForTest` and/or chrome-dump cadence fields) over a timed drag/hover window, without requiring manual stopwatch review. | Feel budgets must be as machine-checkable as `opLatencyMs`. | Continuous-feel scenario JSON + dump fields; first baseline recorded in PLAN (PASS or intentional RED). | harness_driver; Overlay test seams |
| SR-SMO-13 | SR-SMO-09/10 do **not** waive SR-SMO-02: a gesture that paints smoothly but commits slower than 200 ms for an in-scope single-element op still fails the latency budget. Conversely, a sub-200 ms commit that freezes the ghost mid-drag fails SR-SMO-09. | Op latency and continuous cadence are independent acceptance axes. | Both `op_latency_budget` and `paint_cadence_min_hz` apply where profiles overlap. | SR-SMO-02; v2.8.1 |

## Open / Related

- Structural rebuilds such as row insert/delete and scale changes may retain
  temporary masking until the in-place reconcile path covers them.
- Event sink failures must degrade gracefully to the existing polling path.
- Lane-changing drag that forces full reconcile (~2–4 s) is tracked under
  PLAN v2.8.5; it is out of scope for the continuous-cadence floor while the
  drag **preview** paints (preview cadence still applies during the gesture).
- First harness baseline for SR-SMO-09 may be intentional RED with measured Hz
  (same honesty rule as the original 10 s → 200 ms latency work).
