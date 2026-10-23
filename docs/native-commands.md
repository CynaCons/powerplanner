# Native PowerPoint add-in — command reference (PARKED)

The on-slide C++ COM add-in under `native/` is **parked** by user directive
2026-10-23 (see [DECISIONS.md](../DECISIONS.md) D1). Nothing here is scheduled.
This file keeps the build / register / harness reference that used to live in
`AGENTS.md`, so unparking does not mean rediscovering it.

Before trusting any harness result in this file, read
[agents/memories/harness-blind-spots.md](agents/memories/harness-blind-spots.md):
this harness has missed five defects by construction and ships three invalid
gates. A green harness is not evidence for native behaviour.

## Native Commands

Windows-only. MSVC + ATL + 64-bit PowerPoint. Prefer running from the **repo
root** for the Python driver; build bats live under `native/`. Details:
`docs/native-addin.md`, `docs/native-addin-install.md`, `native/tools/README.md`.

### Build (x64 DLL and harnesses)

```bat
native\build.bat
native\build-ops.bat
native\build-conformance.bat
native\build-overlay.bat
native\build-appbar-shot.bat
native\build-reflow.bat
```

- `build.bat` — COM add-in DLL → `native/build/PowerPlannerAddin.dll`
- `build-ops.bat` → `ppops.exe` (pure ops / model tests)
- `build-conformance.bat` → `ppconf.exe` (layout fixtures vs `spec/fixtures/`)
- `build-overlay.bat` → `ppoverlay.exe` (lifecycle / multi-stage overlay gate)
- `build-appbar-shot.bat` → `ppappbarshot.exe` (traces, matrix, gallery, walkthroughs)
- `build-reflow.bat` → `ppreflow.exe`
- Optional probes: `build-keys-probe.bat`, `build-undo-probe.bat`,
  `build-window-probe.bat`, `build-showcase.bat`, `build-render.bat`
- `native\_run_all_builds.cmd` — rebuild everything when unsure
- Run native build bats sequentially. They share object names under
  `native/build/`; concurrent builds can delete or replace each other's `.obj`
  files and cause misleading link failures such as missing `GanttBuilder.obj`.

PowerShell alternative: `native\build.ps1` (optional `-Register` / `-Unregister`).

### Register / launch in PowerPoint

```bat
native\register.bat
native\unregister.bat
native\start.bat
```

- Registration is **per-user** (no admin). Close PowerPoint before rebuilding so
  the new DLL can load.
- `start.bat` always rebuilds the DLL, registers, then launches PowerPoint. Look for the
  **PowerPlanner** ribbon tab → Insert Gantt.

### Harness driver (primary agent entry)

From repo root (Python 3):

```bat
python native\tools\harness_driver.py scenario <name>
python native\tools\harness_driver.py scenario <name> --update-goldens
python native\tools\harness_driver.py trace <profile> --check-invariants
python native\tools\harness_driver.py walkthrough <name>
python native\tools\harness_driver.py walkthrough all
python native\tools\harness_driver.py run <exe> [args...]
python native\tools\harness_driver.py golden <artifact_rel> <golden_name> [--update]
```

- Scenario JSON: `native/tools/scenarios/` (e.g. `appbar_matrix`,
  `overlay_lifecycle`, `trace_drag_date_pill`, `trace_window_edge_drag`)
- Trace profiles are names like `row-add-below`, `drag-date-pill`,
  `task-nudge-latency` (see scenario `trace_profile` fields)
- Walkthroughs: `native/tools/walkthroughs/` (`change-a-date`,
  `add-a-milestone`, `link-two-tasks`, `delete-3-rows`, `rename-a-task`)
- Artifacts + reports: `native/build/` (`*_report.json`, `trace_*` PNGs)
- Goldens: `native/tools/goldens/` — only update with `--update-goldens` /
  `--update` when the visual change is **intentional**; record why in PLAN.md
- Live recorder sessions: gitignored `native/records/`. Use
  `python native/tools/session_manager.py list` or the project
  `powerplanner-recorder` MCP; legacy `%TEMP%\powerplanner-sessions` sessions
  remain discoverable. Never delete a session by raw recursive filesystem
  command; use the manager/MCP confirmation and active-session safeguards.
- Project MCP configuration is additive: when registering a server in
  `.mcp.json`, preserve and smoke-test the existing `powerspawn`, `powerplan`,
  and recorder entries. Do not replace the `mcpServers` object wholesale.
- Codex desktop/CLI uses `~/.codex/config.toml`, not project `.mcp.json`, for
  MCP registration. Mirror these three entries with `codex mcp add` (absolute
  script paths / explicit `PYTHONPATH`) and restart the Codex session after
  configuration changes; MCP tools are not hot-loaded into an active session.

**Single-PowerPoint rule:** the driver kills PowerPoint before runs. Do not run
harness work while the user is editing live decks in the same session without
warning. It also waits if a non-PowerPoint fullscreen app owns the foreground.

### Recommended gate order (after native code changes)

1. Rebuild what you touched (`build.bat` + relevant harness bat, or
   `_run_all_builds.cmd` after broad changes).
2. Pure layer: run `ppops` and `ppconf` (via `scenario` if wrapped, or
   `run ppops` / `run ppconf` after build).
3. Targeted: `trace <profile> --check-invariants` for the op you changed.
4. Broader: relevant `scenario` JSON (lifecycle, creation, matrix, window_*).
5. UX-touching: matching `walkthrough` + PNG review vs
   `spec/srs-native/SRS-InteractionConventions.md`.
6. Tick `PLAN.md`; update `docs/` memory when process or traps are learned.

### Native self-verify (do not skip)

- Confirm harness exit status / report JSON is PASS (or intentional RED with
  measured numbers recorded in PLAN — never soft-fail latency/feel budgets).
- For chrome mutations: pre/immed/+1/+3 dumps and invariants (selection stable,
  no full-component takeover, app bar context, flash heuristics).
- User milestone gates (UG-01…UG-05 in PLAN Phase 13) are **live PowerPoint**
  sign-off — agents must not mark them done without the user.
- Continuous feel: `python native/tools/harness_driver.py scenario
  trace_drag_paint_cadence` — invariant `paint_cadence_min_hz` (≥30 Hz,
  SR-SMO-09). Intentional RED with measured Hz is allowed; never soft-pass.
- Visual goldens: `appbar_matrix` maps `ab-task.png` / `ab-none.png` to
  `native/tools/goldens/`; update only with `--update-goldens` when intentional.
  Policy: `docs/native-visual-dpi-policy.md`. Token parity:
  `python native/tools/check_theme_tokens.py`.
- SR coverage map: `native/tools/coverage/srs-native-coverage.json`.
- Pure CI (no PPT): GitHub Actions `native-pure.yml` — ppconf, ppops, tokens,
  optional ppunit.
