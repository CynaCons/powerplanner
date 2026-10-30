# SRS — Layout

Requirements for projecting a document into positioned elements: visible rows,
sub-row stacking, row heights, and determinism. This is the behaviour of the
shared layout algorithm. Feature tag: `LAY`.

Traces up to: `../layout.md`. Reference impl: `src/layout/engine.ts`.

## Coordinates

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-LAY-001 | The system shall compute layout in abstract coordinates — time in days (`xDay = days from viewStart`), vertical in row slots — independent of any device unit. | One algorithm must serve both web (px) and PowerPoint (pt) without divergence. | Test: `spec/fixtures/basic-chart` at scale 1 | layout §coordinate model |
| SRS-LAY-002 | Each implementation shall map abstract coordinates to device units as a separate final step (web `× pxPerDay` / `ROW_HEIGHT`; PowerPoint `× ptPerDay` / its row height). | Keeps the concept device-neutral; device profile is per-implementation. | Analysis | layout §coordinate model |

## Visible rows

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-LAY-010 | The system shall render rows in document order, excluding child rows whose parent group is collapsed. | Collapse hides detail while preserving order. | Test (collapsed fixture, F2) | layout §1 |
| SRS-LAY-011 | The system shall assign each visible row a zero-based `rowIndex` in render order. | Stable vertical addressing for all elements. | Test: `basic-chart` (visibleRowIds order) | layout §1 |

## Sub-row stacking

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-LAY-020 | When tasks in a row overlap in time, the system shall place them on separate stacking tracks (sub-rows) using a deterministic greedy first-fit by ascending start date. | Overlapping work must not occlude; placement must be repeatable. | Test: `basic-chart` (t_d1 subRow 0, t_d2 subRow 1) | layout §2 |
| SRS-LAY-021 | A row's height shall be `max(1, trackCount)` row slots. | A row grows only as tall as its busiest moment. | Test: `basic-chart` (rowSlots [1,2,1,1]) | layout §3 |
| SRS-LAY-022 | When `allowOverlap` is set, the system shall skip stacking and place every task on sub-row 0. | An explicit "let them overlap" mode for dense exports. | Analysis | engine `allowOverlap` |
| SRS-LAY-024 | `allowOverlap` shall be a property of the document (`style.allowOverlap`), settable from the document context and carried through save and open; absent, a document stacks. | SRS-LAY-022 described a capability nothing could reach: the parameter was declared when the lane packer was written and no caller in the app ever passed it, so the one feature a planner would call think-cell overlap was dead code. Absent rather than false, so every document written before the setting existed still loads. | Test: `tests/unit/overlap.test.ts`, `tests/e2e/overlap.spec.ts` | `types/document` `StyleSettings.allowOverlap`; documentStore `setAllowOverlap`; CommandBar document context; engine |
| SRS-LAY-025 `[web]` | The control that chooses between stacking and overlapping shall name each choice by what happens to bars that cover the same dates, and state the collision case in the control itself rather than only in a tooltip. | The control read “Bars · Stack / Overlap”, which names the implementation and not the outcome, and the sentence explaining it was reachable only by resting the pointer on a 20px button. Owner report 2026-10-29: “I can change whether the bars are stacked or overlapped. I don’t really understand that.” | Test: `tests/unit/commandBarChrome.test.ts` | `CommandBar` `OVERLAP`; SRS-LAY-020, SRS-LAY-024 |
| SRS-LAY-023 | Date comparisons in stacking shall use ISO string ordering (lexicographic == chronological for `YYYY-MM-DD`). | Avoids timezone/parse pitfalls; deterministic. | Analysis | engine §step 2 |

## Row offsets

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-LAY-030 | Row offsets shall be the cumulative sum of preceding row slots; total chart height shall be the sum of all row slots. | Rows stack without gaps or overlap. | Test: `basic-chart` (rowOffsets [0,1,3,4], chartRows 5) | layout §3 |

## Robustness & determinism

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-LAY-040 | Elements whose row reference is missing or hidden shall be omitted from layout, not raise an error. | A partially-edited document must still render. | Analysis | engine (rowIndex.has guards) |
| SRS-LAY-041 | For identical inputs (`doc`, `viewStart`, `scale`, `allowOverlap`) layout shall be fully deterministic — no randomness, no wall-clock. | Reproducible output is required for conformance across implementations. | Test (repeat layout equals) | layout §determinism |
| SRS-LAY-042 | Layout shall not read the system clock; the `today` marker's position shall derive from a supplied date (document/marker), not `now`. | Cross-implementation parity and reproducible fixtures require a clock-free layout. | Analysis | layout §determinism |

## Row panel width

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-LAY-030 `[web]` | The editing controls shall live in a panel docked to one side of the chart, not in a strip across it, and the panel shall hold its sections side by side rather than taking turns in one row. | Measured at 1440x900 with one task selected, the bar's contents were 1210px wide inside 986px: the whole right-hand group sat outside it and the px/day readout overhung its own rounded edge by 99px. Owner report 2026-10-30: "I'm clicking on a task and it's like it's too much. It doesn't fit. The rounding is not good. The sliding bar escapes from the actual card." A strip grows only sideways while the selection's controls grow with what is selected, so the direction was the defect. Taking turns was the other half: tools, selection and document settings shared one row, which is why the gear had to clear the selection to take its place. | Test: `tests/e2e/smoke.spec.ts` | `app/CommandBar` `Section`; `stores/panelStore`; `styles/global.css` `.cmdbar` |
| SRS-LAY-031 `[web]` | Each section shall open and close from its own header, the state shall persist for that browser, and a closed section shall hold no focusable control. | A panel that reopens the way it was left is a tool; one that resets is a surprise. Unmounting rather than hiding keeps Tab from walking through controls nobody can see. | Test: `tests/e2e/smoke.spec.ts` | `app/CommandBar` `Section`; `stores/panelStore` |
| SRS-LAY-032 `[web]` | No control in the panel shall extend beyond it, and the panel shall never scroll sideways; where the window is too narrow for a side panel it shall take the foot of the window instead, still without overlapping the chart. | The bar it replaced both overflowed and floated over the chart, so its last row had to be scrolled clear of it. At phone and tablet widths 288px of side panel would leave no chart at all. | Test: `tests/e2e/smoke.spec.ts` | `styles/global.css` `.cmdbar`, `@media (max-width: 1000px)`, `.app-shell--compact` |

| SRS-LAY-050 `[web]` | Until a width is chosen, the row panel's width shall be measured from the names it actually holds, clamped to the usable range, and shall follow those names as they are renamed, added or revealed. | The width was the constant 200px: long package names were cut off mid-word while a plan of short names spent a quarter of the window on blank panel. Owner report 2026-10-29: "the left panel should auto-size to its text". | Test: `tests/unit/gutterWidth.test.ts` | `layout/gutterWidth` `gutterWidthFor`; `app/ChartArea` `autoGutterW` |
| SRS-LAY-051 `[web]` | A width the planner sets by dragging the divider, or one restored from a stored preference, shall override the measured width for the rest of the session and shall not be revised by later renames. | A panel that re-measured itself after the planner had already sized it would be arguing back, and the drag would appear not to hold. | Test: `tests/unit/gutterWidth.test.ts` | `app/ChartArea` `gutterW` (`number \| null`), `GUTTER_KEY` |
| SRS-LAY-052 `[web]` | The sheet shall reserve a band below the last row, inside the plot clip, for labels placed below their element; the band is where the marker chips sit while the whole plan fits the window, and SRS-ELEM-062 governs where they go once it does not. | Both were being drawn past the foot of the last row, where `chart-plot-clip` removed them: the below-label of SRS-EDIT-056 and the deadline chips of SRS-ELEM-05x. Taking the space out of the last row instead would have painted the band over that row's own bars. The reservation remains what a below-row label on the LAST row needs, since that label scrolls with its own row; the marker chips no longer rely on it, because on a plan taller than the window the foot of the plot is outside the clip entirely. | Test: `tests/unit/footBandReserved.test.ts` | `app/ChartArea` `FOOT_BAND_H`, `plotHeight`; `renderer/Markers` `footBandTop`; mockup §07 |
