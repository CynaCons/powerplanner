# Native Harness Driver (for Agents & Coordinator)

Python package that drives **pre-built** native harnesses under `native/build/`
and returns **structured feedback** (markers, artifacts, invariants, walkthrough
reports) for agents and the onslide-coordinator.

Canonical agent entry: see **Native Commands** in repo-root `AGENTS.md`.
Active quality program: Phase 13 / v2.8.x in `PLAN.md`.

## Prerequisites

1. Build harnesses first (from `native/`):
   - `build-appbar-shot.bat` → `ppappbarshot.exe` (traces, matrix, gallery, walkthroughs)
   - `build-overlay.bat` → `ppoverlay.exe`
   - `build-ops.bat` → `ppops.exe`
   - `build-conformance.bat` → `ppconf.exe`
   - `build-reflow.bat` → `ppreflow.exe`
2. 64-bit PowerPoint installed (COM harnesses launch it).
3. Run the driver from the **repo root**.

## CLI (from repo root)

```powershell
python native/tools/harness_driver.py scenario <name>
python native/tools/harness_driver.py scenario <name> --update-goldens
python native/tools/harness_driver.py scenario <name> --retries 1

python native/tools/harness_driver.py trace <profile> --check-invariants
python native/tools/harness_driver.py trace --profile <profile> --check-invariants

python native/tools/harness_driver.py walkthrough <name>
python native/tools/harness_driver.py walkthrough all

python native/tools/harness_driver.py run <exe> [args...]
# known exe stems: ppappbarshot, ppoverlay, ppops, ppreflow (also any *.exe in native/build/)

python native/tools/harness_driver.py golden <artifact_rel> <golden_name> [--update]
```

Exit codes: `0` PASS · `1` FAIL/FLAKE/ERROR · `3` VISUAL_DIFF (golden path).

## Python API

```python
from native.tools.harness_driver import (
    run_scenario,
    run_operation_trace,
    check_trace_invariants,
    run_walkthrough,
    run_all_walkthroughs,
    compare_to_golden,
    run_harness,
)

report = run_scenario("appbar_matrix")
print(report.status, report.markers_found, report.artifacts)

tr = run_operation_trace("row-add-below")
tr.invariants = check_trace_invariants(tr, "row-add-below")

wt = run_walkthrough("change-a-date")
```

## Output locations

| Kind | Path |
|------|------|
| Generic harness report | `native/build/<exe>_report.json` |
| Operation trace | `native/build/trace_<profile>_report.json` + step PNGs |
| Walkthrough | `native/build/walkthrough_<name>_report.json` (+ `walkthrough_all_report.json`) |
| Captures | `native/build/*.png` (trace_*, gallery-*, ab-*, etc.) |

Example harness report fields: `status` (PASS/FAIL/FLAKE/ERROR), `markers_found`,
`artifacts`, `duration_s`, `returncode`, optional `golden_comparisons`.

## Scenarios (`scenarios/`)

JSON files. Common fields: `name`, `description`, `exe`, `args` and/or
`trace_profile`, `timeout`, `retries`, `check_invariants`, `invariants`,
`expected_markers`, `expected_trace_steps`.

### Stage / matrix (non-trace)

| Scenario | Role |
|----------|------|
| `appbar_matrix` | App bar contexts matrix + fit/calm markers |
| `gallery_matrix` | 10-context gallery captures (U8 cohesion) |
| `overlay_lifecycle` | Multi-stage overlay gate (gating, idle, create, …) |
| `overlay_creation` | Creation flows on empty / rows-only charts |
| `row_selection` | Row selection stage |
| `task_marker_context` | Task/marker context stage |

### Operation traces (`trace_*` → profile via `trace_profile`)

| Scenario | Typical concern |
|----------|-----------------|
| `trace_row_add_below`, `trace_row_rename`, `trace_row_scale`, `trace_row_label_select`, `trace_row_then_overall` | Row ops / selection continuity |
| `trace_row_adder_boundaries` | UF-06 dual boundary chips |
| `trace_task_select_progress`, `trace_progress_drag` | Task selection + progress DM |
| `trace_task_nudge_latency`, `trace_task_color_latency` | SR-SMO-02 op latency ≤200 ms |
| `trace_task_scale_keep_sel` | Scale keeps selection |
| `trace_hover_quick_add_task`, `trace_create_preview_shape` | Creation preview / quick-add |
| `trace_drag_date_pill`, `trace_drag_row_retarget`, `trace_drag_commit_echo` | Direct date drag + echo |
| `trace_drag_paint_cadence` | Continuous paint Hz mid-drag (SR-SMO-09; floor 30) |
| `trace_marker_snap` | Marker snap + pill |
| `trace_inline_rename_task`, `trace_card_commit_clickaway` | Rename / card commit semantics |
| `trace_component_shape_protection` | Child shape suppress + hotkey scope |
| `trace_appbar_docked`, `trace_appbar_context_evolution` | Docking + context purity |
| `trace_multi_row_delete` | Multi-select + bulk delete |
| `trace_link_drag_port` | Port-based linking |
| `trace_theme_coherent_surfaces` | Custom menu/card theme |
| `trace_scale_settings` | Document scale settings popover |
| `trace_overall_move`, `trace_overall_resize` | CHART_ROOT overall ops |
| `trace_window_edge_drag`, `trace_window_clip_rerender`, `trace_window_commit_latency`, `trace_window_repair_lossless`, `trace_window_undo` | Time window (v2.7) |

List on disk: **39** scenario JSON files (2026-07-17).

## Walkthroughs (`walkthroughs/`)

Cold UX goals — real posted gestures (not test-seam shortcuts). Used for
SR-IXC-22 / AGENTS walkthrough gate.

| Name | Goal |
|------|------|
| `change-a-date` | Drag task bar to reschedule |
| `trace_task_bar_unit` | Task bar unit: label/progress/body select same TASK; label drag moves dates (SR-TASK-UNIT) |
| `add-a-milestone` | Create a milestone discoverably |
| `link-two-tasks` | Port-based link |
| `delete-3-rows` | Multi-select delete |
| `rename-a-task` | Inline rename |

```powershell
python native/tools/harness_driver.py walkthrough change-a-date
python native/tools/harness_driver.py walkthrough all
```

PASS requires `WALKTHROUGH COMPLETE` + enough step PNGs on disk. Per-step notes
still need **human PNG review** for discoverability — green ≠ UX signed off.

## Invariants (`check_trace_invariants`)

About **59** unique rules in `harness_driver.py`, profile-scoped. Categories:

- **Continuity:** `row_sel_stable_during_item_op`, `appbar_visible_stable`,
  `no_large_sel_drop_to_empty`, `no_content_flash_in_trace`, row-band stability
- **Latency:** `op_latency_budget` (≤200 ms; OPLATENCY or step_tMs fallback)
- **Continuous feel:** `paint_cadence_min_hz` (≥30 Hz mid-drag; may be intentional RED)
- **Selection / chrome:** `no_child_shape_selected`, `hotkey_scope_respected`,
  `context_reset_to_component`, scale-group context rules
- **App bar:** docked-below-chart, follows move/resize, context group purity,
  pixel change on context
- **Direct manip:** drag pill, progress commit, create preview height, marker snap
- **Window:** ports, pill, snap/clamp, header pixel-diff, lossless reemission,
  undo, commit budget, `pp_proj_matches_window`

Always pass `--check-invariants` for chrome/mutation work. Window profiles force
their scoped rules even without the flag when invoked via the trace CLI paths
that embed that policy.

## Goldens (`goldens/`)

MD5 + size comparison (stdlib only):

```powershell
python native/tools/harness_driver.py golden native/build/ab-task.png appbar-task
python native/tools/harness_driver.py golden native/build/ab-task.png appbar-task --update
python native/tools/harness_driver.py scenario gallery_matrix --update-goldens
```

**Discipline:** only update goldens when the visual change is intentional;
record why in `PLAN.md`. Phase 13 v2.8.2 expands committed golden coverage
(directory may start sparse — empty is not "all green", it is "not yet locked").

## Safety & environment

- **Single-PowerPoint rule:** driver `taskkill`s POWERPNT.EXE before attempts.
  Do not run while the user is editing live decks without warning.
- **Fullscreen wait:** if a non-PowerPoint fullscreen app owns the foreground,
  the driver waits (avoids painting chrome over games/other apps).
- **PP_HARNESS stand-down:** harness presentations tag `PP_HARNESS=1` so the
  registered add-in does not double-draw chrome over harness windows.
- **Harness override:** NOTOPMOST under harness so fullscreen apps stay on top.
- Only drives **pre-built** exes; rebuild after C++ edits.
- COM flakiness: status `FLAKE` when neither clear success nor failure markers;
  use `--retries`. Coordinator decides whether FLAKE is transient.

## Adding a scenario

1. Add `scenarios/<name>.json` (prefer `trace_profile` + `check_invariants` for
   chrome ops; `expected_markers` for stage harnesses).
2. Implement/extend profile or stage in the C++ harness if needed.
3. Run: `python native/tools/harness_driver.py scenario <name>`.
4. Register in `PLAN.md` + SRS Trace column when the scenario gates a requirement.

## Adding a walkthrough

1. Add `walkthroughs/<name>.json` with `goal` + ordered `steps` (hover/click/drag/key/capture).
2. Notes should cite SR-IXC IDs where applicable.
3. Run walkthrough CLI; review step PNGs against AGENTS UX checklist.

## Session reports

Turn a recorded live session (`events.jsonl` + `meta.json` + optional `frames/`)
into one agent-readable markdown artifact, with snapshot/entity diffs and
anomaly detection for the live-vs-harness failure signatures. Spec:
`docs/session-recorder-spec.md` (R2).

```powershell
# Write <session-dir>/report.md (or --out path)
python native/tools/session_report.py native/records/<session-id>
python native/tools/session_report.py path\to\session --out path\to\report.md

# Evaluate harness-style invariant names against the recording (exit 0/1 + JSON)
python native/tools/session_report.py path\to\session --assert path\to\scenario.json

# Synthetic fixture (all event types + three 2026-07-18 failure signatures)
python native/tools/session_report.py --selftest
```

- Report sections: meta header, chronological timeline (input→hit, selection
  transitions, gesture lifecycles with duration, ops with `dispatchMs`, errors
  highlighted), consecutive snapshot/entity diffs (changed fields only), frame
  references, anomalies (open gestures, nativeSel child without
  suppression/ownSel mirror, error events, paint gaps >500 ms during gestures,
  hairline/1px preview entities).
- `--assert` rules (stdlib, session-adapted): at least
  `task_label_selects_task_unit`, `task_body_selects_same_unit`,
  `gesture_commits_or_cancels`, `no_silent_errors` — same `{rule, passed, detail}`
  shape as `harness_driver` invariants.
- Fixture: `native/tools/testdata/session-fixture/` (hand-written JSONL; no DLL).
- Python **stdlib only**; does not import or modify `harness_driver.py`.

## Recording management and MCP

Development recordings are written to the gitignored `native/records/` folder.
`POWERPLANNER_RECORDS_DIR` overrides that location. The manager also discovers
legacy `%TEMP%\powerplanner-sessions` recordings without moving or deleting them.

```powershell
python native/tools/session_manager.py list
python native/tools/session_manager.py show <session-id>
python native/tools/session_manager.py events <session-id> --limit 100
python native/tools/session_manager.py report <session-id>  # generates when missing
python native/tools/session_manager.py delete <session-id> --confirm <session-id>

Report generation from the MCP (`generate_recording_report`) calls
`session_manager.generate_report`, which prefers in-process
`session_report.write_report`. If a subprocess CLI path is used, it must pass
`stdin=subprocess.DEVNULL` so the child does not inherit the MCP stdio pipe
(Windows deadlock / 60s timeout). See `docs/session-recorder-spec.md` R3.
```

The project `.mcp.json` registers `native/tools/session_mcp.py` as the
`powerplanner-recorder` stdio server. It exposes bounded list, metadata, event,
report-generation, and delete tools. Deletion requires an exact session-id
confirmation, rejects active sessions, and verifies the resolved target remains
inside a known recording root. Restart the MCP client after changing `.mcp.json`.

## Coverage map

`native/tools/coverage/srs-native-coverage.json` — SR-ID → scenario/walkthrough status.

## Related docs

- `AGENTS.md` — Native Commands + verification discipline
- `docs/phase13-quality-program.md` — quality program memory + feel baseline
- `docs/native-visual-dpi-policy.md` — goldens / DPI
- `docs/native-agent-feedback-loop-plan.md` — original loop design
- `docs/native-addin.md` — build/register overview
- `spec/srs-native/` — native requirements (Verification columns name scenarios)
- `PLAN.md` Phase 13 — quality program (closed 2026-07-17)
