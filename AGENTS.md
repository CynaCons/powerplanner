# Agent Instructions

These instructions are the shared source for coding agents working on
PowerPlanner. Codex reads `AGENTS.md` directly. Claude should read
`CLAUDE.md`, which delegates back here so the guidance stays in one place.

## Project Shape

- PowerPlanner is a React 19 + TypeScript + Vite app for presentation-grade
  Gantt chart authoring.
- The core chart surface is SVG. Do not replace it with Canvas/Konva unless a
  task explicitly changes that architecture.
- Zustand stores live in `src/stores/`; layout math lives in `src/layout/`;
  SVG rendering lives in `src/renderer/`; app chrome lives in `src/app/`.
- The portable single-file build is produced by `vite.export.config.ts` and
  writes `PowerPlanner.html` under `dist-template/`.
- The PowerPoint add-in entry is `taskpane.html` plus `src/taskpane/`.

## Commands

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

When commands change, update this file and `.claude/launch.json` together.

## Planning And Docs

- Treat `PLAN.md` as the current implementation plan and keep it current when
  completing planned work.
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
  guide.
- For requirements and user interaction specifications, prefer ASPICE-style
  SRS documents (e.g. `docs/SRS_FeatureName.md` or under `spec/srs/`) organized
  by feature with clear "what the user does" and "software requirements".

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
  use the v2.4.0 trace harness: `python native/tools/harness_driver.py trace row-add-below --check-invariants`
  (and rename/scale profiles). It captures before/immed/+1/+3 chrome state JSON + PNGs
  and runs continuity invariants. Re-run after edits to verify fixes for flashes,
  selection drops, wrong chrome context. Always update PLAN.md + docs/ memory files.

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
