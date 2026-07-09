# Native Harness Driver (for Agents & Coordinator)

This package lets coding agents and the onslide-coordinator drive the real on-slide harnesses and receive **structured feedback** (behavior + visuals) without having to parse raw console output or hard-code paths.

## Quick Start

```python
from native.tools.harness_driver import run_appbar_matrix, run_harness

# High-level convenience
report = run_appbar_matrix()
print(report.status)           # PASS / FAIL / FLAKE
print(report.artifacts)        # list of recent PNGs written
print(report.markers_found)

# Generic
r = run_harness("ppreflow")
```

From the command line (run from repo root):

```powershell
python native/tools/harness_driver.py scenario appbar_matrix
python native/tools/harness_driver.py run ppappbarshot --matrix --retries 1
```

## Output

Every run writes a sidecar report next to the build artifacts:

`native/build/ppappbarshot_report.json`

Example shape:
```json
{
  "exe": "ppappbarshot",
  "args": ["--matrix"],
  "returncode": 0,
  "duration_s": 4.2,
  "stdout_tail": "...",
  "markers_found": ["APPBAR-MATRIX NONE OK", "APPBAR-MATRIX TASK OK", ...],
  "artifacts": ["native/build/ab-task.png", "native/build/ab-row.png", ...],
  "status": "PASS",
  "timestamp": "2026-07-09T..."
}
```

## Flakiness Handling

COM harnesses can be flaky (especially KEYS / TEXTELEM stages). The driver:
- Treats runs that produce **neither** a clear success marker nor a failure marker as `FLAKE`.
- Supports `--retries` / `retries=` (total attempts = retries + 1).
- The caller (coordinator) decides whether to treat FLAKE as transient.

## Current Safe Scenarios

See `scenarios/`.

- `appbar_matrix` — exercises the new S2 app-bar matrix capture using the already-built `ppappbarshot.exe`.

## Integration Notes (from the plan)

- Only drives **pre-built** executables in `native/build/`.
- The driver taskkills PowerPoint before each attempt per the repo single-PowerPoint rule; never run it while another harness/gate is running.
- Reuses existing seams (`Overlay_SelectForTest`, `OverlayAppBarHwnd`, `CaptureRectToPng`, etc.).
- For S3 row-selection work (next focus), add scenarios that exercise row bands / hit zones once the current slice stabilizes.

## Adding a New Scenario

1. Add a JSON file under `scenarios/`.
2. Use the driver from Python or CLI.
3. The coordinator can call `run_scenario("your_new_one")` and feed the report back to the implementing agent.

See the top-level plan: `docs/native-agent-feedback-loop-plan.md`
