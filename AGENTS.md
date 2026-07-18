# Agent Instructions

These instructions are the shared source for coding agents working on
PowerPlanner. Codex reads `AGENTS.md` directly. Claude should read
`CLAUDE.md`, which delegates back here so the guidance stays in one place.

## Project Shape

- PowerPlanner is a React 19 + TypeScript + Vite app for presentation-grade
  Gantt chart authoring, plus a **native** PowerPoint COM add-in (think-cell
  style on-slide editor) under `native/`.
- The web chart surface is SVG. Do not replace it with Canvas/Konva unless a
  task explicitly changes that architecture.
- Zustand stores live in `src/stores/`; layout math lives in `src/layout/`;
  SVG rendering lives in `src/renderer/`; app chrome lives in `src/app/`.
- The portable single-file build is produced by `vite.export.config.ts` and
  writes `PowerPlanner.html` under `dist-template/`.
- The Office.js PowerPoint add-in entry is `taskpane.html` plus `src/taskpane/`.
- The **native** add-in is C++ ATL/COM under `native/PowerPlannerAddin/`
  (GanttLayout/Builder/Ops/Json, Overlay, ThemeMenu). Shared concepts live in
  `spec/`; native-only requirements live in `spec/srs-native/`. Active quality
  work is Phase 13 in `PLAN.md` (v2.8.x).

## Web Commands

Use the existing npm scripts:

```bash
npm run dev
npm run dev:addin
npm run build
npm run build:template
npm run lint
npm test
npm run test:e2e
npm run addin:validate
```

- `npm run dev` — HTTP on port 5180 (main app, Playwright, daily work)
- `npm run dev:addin` — HTTPS on port 5180 (PowerPoint sideload; requires `npm run addin:certs`)

The Claude launch targets in `.claude/launch.json` map to:

- `dev`: `npm run dev` on port 5180 (HTTP)
- `dev:addin`: `npm run dev:addin` on port 5180 (HTTPS)
- `portable`: serve `dist-template/` on port 5181
- `site`: serve `site/` on port 5190

When web commands change, update this file and `.claude/launch.json` together.

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

## Planning And Docs

- Treat `PLAN.md` as the current implementation plan and keep it current when
  completing planned work. **Quick Summary** is the cockpit (current version +
  next iteration); do not trust old closed sections for "what's next".
- **Active quality program:** Phase 13 / v2.8.x in `PLAN.md` (docs hygiene,
  continuous feel metrics, visual goldens, SR coverage, native CI, architecture
  hygiene). Product polish in parallel: v2.7.4. Native-only shalls:
  [`spec/srs-native/`](spec/srs-native/README.md). Shared shalls:
  [`spec/srs/`](spec/srs/README.md). Spec layering: [`spec/STRUCTURE.md`](spec/STRUCTURE.md).
- **CRITICAL PROCESS RULE**: Whenever new tasks arise — whether from user
  feedback, self-discovery during exploration, bug reports, feature analysis,
  or any other source — **immediately register them in PLAN.md**.
  - Extend the currently active iteration (add checklist items under the
    relevant vX.Y.Z section).
  - For a coherent new focused group of work, declare a new iteration or
    sub-iteration (following the vX.Y.Z **Goal:** + flat -[ ] list style at
    the bottom of the active phase).
  - This ensures PLAN.md always reflects open work, priorities, and future
    items without losing track. Do this *before* starting implementation.
- Keep `README.md` user-facing and concise.
- Use `docs/` for focused implementation notes such as the PowerPoint add-in
  guide, architecture maps, UX inventories, and plans. **Do not place primary
  normative requirements (SRS) here.** Legacy `docs/SRS_*.md` files are
  pointers only — tables live under `spec/srs-native/`.
- **SRS / Requirements (MANDATORY FORMAT AND LOCATION)**:
  - The single correct format is the ASPICE-style requirements *tables* as
    defined and used in `spec/srs/` (see spec/srs/README.md and examples like
    SRS-chart-elements.md, SRS-powerpoint.md).
  - **Layered architecture (enforced)**:
    - `spec/` = Foundation layer (common to web + native): `data-model.md`,
      `layout.md`, `visual-vocabulary.md`, `interaction.md`, `schema/`, `fixtures/`,
      + `spec/srs/` for shared cross-implementation requirements.
    - Platform-specific: `spec/srs-native/` for native-only concerns (overlay
      lifecycle, app bar, shape protection, theme coherence of chrome/menus/panels,
      on-slide selection, docking, time window, interaction conventions, etc.).
      Use identical table format. Index: `spec/srs-native/README.md`.
    - Web-specific (when created): future `spec/srs-web/`.
  - When adding or updating requirements from feedback or analysis: create/update
    in the correct layer using the table columns (ID | Requirement | Rationale |
    Verification | Trace). Never use prose-only or docs/SRS_*.md as the source
    of truth for "shall" statements.
  - Always trace new SRS entries to PLAN items, harness scenarios, and code.
- All agents and future work must follow the structure above. Update
  `spec/README.md` when the model evolves. References in AGENTS/PLAN must point
  to the canonical locations.

## Memory Files and Continuous Self-Improvement

- Regularly contribute to memory files taking the form of `.md` files in the
  `docs/` folder. These serve as persistent knowledge bases capturing learnings,
  processes, decisions, feedback, and project evolution.
- Constantly try to improve yourself and future agents in this project by:
  - Learning what we're doing from all available sources (conversations, code,
    plans, logs, artifacts, and user interactions).
  - Iterating independently often until completion on tasks, improvements, and
    refinements.
  - Challenging your own actions, assumptions, and outputs to drive higher
    quality.
  - Ensuring quality and completeness in every step of the work.
  - Most importantly, performing self-improving recursive updates to
    `AGENTS.md` and associated sub-memory files in `docs/`.
- After any significant work, discovery, or feedback cycle, proactively update
  relevant memory files to evolve the shared agent knowledge base for the
  project and all future agents.

## Implementation Rules

- Prefer the existing data model and layout pipeline over new parallel paths.
- Keep layout functions pure and covered by unit tests when behavior changes.
- Keep chart output presentation-grade: crisp SVG primitives, stable spacing,
  readable labels, and print-safe behavior.
- Preserve offline-first portable HTML behavior. Changes to persistence should
  keep `.html`, `.json`, and `.yaml` round-trips working.
- Keep Office.js code isolated in `src/taskpane/` unless shared app behavior is
  intentionally needed.

## Verification

- For data model, layout, persistence, or renderer changes, run `npm test`.
- For UI workflow changes, run the relevant Playwright coverage or at least
  smoke-test the app at `npm run dev`.
- For portable build changes, run `npm run build:template` and verify the
  generated standalone HTML still opens.
- For PowerPoint add-in manifest or taskpane changes, run
  `npm run addin:validate`; cert/sideload checks may require local Office
  tooling.
- For native on-slide UI/UX changes (overlay, app bar, row ops, rebuilds):
  use the trace harness: `python native/tools/harness_driver.py trace row-add-below --check-invariants`
  (and rename/scale/drag/window profiles as relevant). Captures pre/immed/+1/+3
  chrome JSON + PNGs and continuity invariants. Also run continuous-feel and
  golden scenarios when paint/chrome surfaces change (see Native Commands).
  Always update PLAN.md + docs/.
- **Native SRS + E2E discipline (from feedback 2026-07-11)**: Any native UX
  behavior change, new panel/menu, selection/docking/context rule MUST be
  accompanied by:
  1. An entry (or update) in the correct SRS (table format in spec/srs or
     spec/srs-native/).
  2. A corresponding harness e2e scenario or trace profile exercising the
     requirement (new or extension of existing).
  3. An explicit checklist item in the active PLAN.md iteration *before*
     implementation begins.
  4. Re-run of relevant traces + `--check-invariants` + artifact review.
- Theme coherence (SR-THEME family): any new or modified popup, context menu,
  card, panel, or chrome element must be implemented with custom drawing
  sourcing from GanttTheme.h + design-tokens.md and pass visual coherence
  review (PNG + mockup comparison). Default Win32 menus/dialogs are forbidden
  for PowerPlanner surfaces.
- **UX walkthrough gate + interaction conventions (from feedback 2026-07-11,
  v2.6.0)**: gates that only assert markers/state prove *correctness*, not
  *usability* — every UX-touching slice must ALSO exit through a task-based
  cold walkthrough: pick the user goals the slice affects ("change a date",
  "link two tasks", "add a milestone", "delete 3 rows", ...), execute them via
  the harness AS A FIRST-TIME USER WOULD (no test seams as shortcuts for the
  gesture itself), capture per-step PNGs/GIF, and review them against
  spec/srs-native/SRS-InteractionConventions.md and this checklist:
  1. Discoverable — is each affordance visible on hover/selection, or does the
     flow require documentation to find?
  2. Direct manipulation first — continuous values (dates, %, positions) are
     changed by dragging with a LIVE preview of the result; stepper buttons
     for continuous values are forbidden.
  3. Consistent verbs — the same label always does the same thing in every
     context; commit/cancel semantics (click-away, Esc, Enter) are identical
     across all editors.
  4. No traps — every mode/state has a visible exit; Esc/deselect always
     returns surfaces (app bar, cursors) to the default context.
  5. Constrained + snapped — drags cannot leave the component; movement snaps
     to the scale grid where dates are involved.
  6. Platform conventions — Ctrl/Shift multi-select, Del, F2/double-click,
     right-click behave as an Office user expects; never steal input the user
     directed elsewhere.
  Findings from a walkthrough are registered as UF-xx entries in PLAN.md
  Phase 11 (or the active program) before the slice is declared done. A slice
  that passes its trace invariants but fails the walkthrough is NOT done.

## Self-Verification & Process Awareness (MANDATORY)

- **Never trust or ask the user for runtime state.** Always verify yourself using
  available tools before and after any action that affects running processes:
  `run_terminal_command` for `Get-NetTCPConnection`, `Test-NetConnection`,
  `Get-CimInstance Win32_Process`, `netstat`, log tails, `Invoke-WebRequest`
  (with retries, explicit 127.0.0.1 vs localhost, SkipCertificateCheck for https),
  `npm run addin:stop`, etc.
- For long-running servers use `background: true` on `run_terminal_command` (or
  the `monitor` tool for streaming), then **immediately** call
  `get_command_or_subagent_output` with the returned task_id + fresh port/process/
  HTTP tests to confirm.
- After `npm run dev` (or equivalent), you must see in your tool results:
  - Vite "ready" message with the correct URL.
  - Port 5180 (or configured port) LISTENING owned by the vite process from this
    project.
  - Successful TCP test (`TcpTestSucceeded: True`).
  - Successful HTTP(S) fetch returning 200 + body containing PowerPlanner/ root
    / Gantt content (no connection refused/timeout).
  - No repeating "server is being restarted or closed" or dep-scan errors in
    polled output.
- When chrome-devtools MCP is available, use `navigate_page` + `take_snapshot`
  / `list_console_messages` / script evaluation on http://localhost:5180 (or the
  https URL) to confirm the actual UI loaded without critical browser errors.
- **Loop and iterate until verified**: inspect with tools → apply fix (edit
  config, clear node_modules/.vite, kill PIDs, relaunch) → re-inspect with the
  same tools. Do not stop or report success to the user until **your own tool
  outputs** conclusively prove the desired state.
- Before claiming completion for anything involving "the app launches", "dev
  server is up", or "PowerPoint plugin started", you must have performed the
  smoke test / reachability / content verification yourself via tools and seen
  clean results (per the existing smoke-test requirement in the project notes).
- Update this AGENTS.md whenever new persistent self-verification requirements
  are established by the user or project needs.

When commands or verification practices change, update this file and
`.claude/launch.json` together.
