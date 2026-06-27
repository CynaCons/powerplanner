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
