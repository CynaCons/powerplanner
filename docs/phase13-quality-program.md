# Phase 13 — Quality Infrastructure (agent memory)

Registered 2026-07-17 after a docs + native-test evaluation. **Live checklist:
`PLAN.md` Phase 13 (v2.8.0–v2.8.5).** Closing summary 2026-07-17.

## Why it exists

Native delivery had strong harness traces, latency budgets, and SRS tables, but:

1. PLAN header / open boxes lagged reality (agents re-scoped finished work).
2. AGENTS.md was web-first (no first-class native command surface).
3. Continuous interaction feel (paint Hz during drag) was unmeasured.
4. Visual work was capture-and-review, not fail-closed goldens.
5. No SR coverage matrix, weak pure-layer CI, architecture hygiene deferred.

## Delivered

| ID | Status | Notes |
|----|--------|-------|
| v2.8.0 | COMPLETE | AGENTS Native Commands, tools README, plan archive, STRUCTURE, backlog sync |
| v2.8.1 | COMPLETE (baseline RED) | SR-SMO-09..13, paint timestamp ring, `trace_drag_paint_cadence`, invariant; measured ~20–26 Hz mid-drag (p50 ~32 ms) — floor 30 Hz **intentional RED**; one denser-step attempt done |
| v2.8.2 | COMPLETE | Goldens for appbar task/none, appbar_matrix wired, DPI policy doc, token check PASS (31 keys), visual-vocabulary checklist |
| v2.8.3 | COMPLETE | `srs-native-coverage.json`, walkthrough stubs `change-the-time-window` + `insert-build-present`, undo audit |
| v2.8.4 | COMPLETE | GHA `native-pure.yml`, ppunit seed + build-unit.bat, headless design note, multi-fixture still 1/1 basic-chart expected |
| v2.8.5 | COMPLETE (Tier-B re-deferred) | CMakeLists IDE-only, .editorconfig, lane-drag perf note, multi-mon Demo decision; Overlay Tier-B split re-deferred (risk) |

User gates **UG-01…UG-05** remain open (live PowerPoint).

## Day-one agent entry

1. `PLAN.md` Quick Summary → Phase 13
2. `AGENTS.md` → Native Commands + Verification
3. `native/tools/README.md`
4. `spec/srs-native/`
5. Gates: ppops, ppconf, `check_theme_tokens.py`, traces, goldens

## Continuous feel baseline (honest RED)

```
paintHz≈20–26  budget>=30  paintCount≈39  windowMs≈900–1900  p50Ms≈32  p95Ms≈63–125
scenario: trace_drag_paint_cadence
```

Follow-up: reduce drag-path paint cost / coalesce p95 spikes (not fake green).

## Honesty rules

- Latency and feel budgets: intentional RED with measured numbers beats soft green.
- Update goldens only when intentional; record in PLAN.
- Archived plans under `docs/archive/` are not the live backlog.
