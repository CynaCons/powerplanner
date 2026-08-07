# Lane-changing drag commit performance (U2 follow-up / Phase 13 v2.8.5)

## Symptom

Horizontal in-row date drags that stay within the projection window hit the
fast path (~≤200 ms after v2.5.3). **Lane-changing** commits (vertical row
retarget or union-bounds change) take the **full reconcile** path, measured
historically at **~2–4 s**.

## Bottleneck (investigation)

1. Fast path bails when the scene union bounds / structural classifiers say the
   group frame must change (`IsStructuralDocDelta` / scene-cache mismatch).
2. Full `UpdateGantt` / re-emit path does COM shape property volume (same class
   of cost as the pre-fast-path 10s single edits).
3. Write-only group frame re-pin can scale children (PPT group semantics) —
   unsafe shortcuts were rejected in v2.6.2 hardening.

## Decision (2026-07-17)

- **Not fixed in Phase 13.** Continuous paint cadence (v2.8.1) covers **preview**
  feel during the gesture; commit cost for lane-change remains a product perf
  iteration.
- Register future work: dedicated “lane-change fast path” iteration with budget
  ≤500 ms and scene-cache child reposition without group resize.

## Measure again

```text
python native/tools/harness_driver.py scenario trace_drag_row_retarget --check-invariants
```

Record `opLatencyMs` / TRACE timings when optimizing.
