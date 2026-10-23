# Agent Instructions

PowerPlanner is run the **PowerFlow** way. PowerFlow is the method — one
method, ten rules, two tools — and it lives in the `powerflow` skill, not in
this file. Read it there and follow it. This file holds only what is specific
to *this* project: the focus, the commands, the gates, and the rules PowerFlow
leaves to the project to decide.

Claude reads `CLAUDE.md`, which delegates here. Codex and other CLI agents read
this file directly.

## Product focus

**The web app is the product** (user directive 2026-10-23). It ships as a
single-file HTML application in the same format as PowerScroll: a GitHub
release carrying `PowerPlanner.html`, and a GitHub Pages site hosting a live
demo.

The native PowerPoint add-in under `native/` is **parked** — the owner's company
does not use PowerPoint. Do not schedule native work, do not "just fix" a
native defect you notice, and do not let a native concern shape a web design.
Command reference if it is ever unparked:
[`docs/native-commands.md`](docs/native-commands.md). Decision:
[DECISIONS.md](DECISIONS.md) D1.

## Project shape

- React 19 + TypeScript + Vite. Presentation-grade Gantt chart authoring.
- The chart surface is **SVG**. Do not replace it with Canvas/Konva unless a
  task explicitly changes that architecture.
- Zustand stores in `src/stores/`; layout math in `src/layout/`; SVG rendering
  in `src/renderer/`; app chrome in `src/app/`.
- The portable single-file build comes from `vite.export.config.ts` and writes
  `dist-template/PowerPlanner.html`.
- `src/version.ts` is the **single version source**; `package.json` mirrors it.
- The Office.js task pane (`taskpane.html`, `src/taskpane/`) is parked with the
  native add-in.

## Commands

```bash
npm run dev            # port 5180 - main app, Playwright, daily work
npm run build          # tsc -b && vite build
npm run build:template # single-file dist-template/PowerPlanner.html
npm run lint
npm test               # vitest unit
npm run test:e2e       # Playwright
npm run gen:visual     # regenerate spec/visual/*.svg from the fixtures
```

`.claude/launch.json` targets: `dev` (5180), `portable` (`dist-template/` on
5181), `site` (`site/` on 5190). When these change, update this file and
`.claude/launch.json` together.

## The plan

`PLAN.md` is written **only through the powerplan tools**. A hand edit is a
process violation, and this project has the receipts: it accumulated two ACTIVE
iterations, a duplicate version, and a header advertising native work as "Next
Up" three months after the focus moved to the web.

- First call of a session: `show_miniplan`. Last: ticks with evidence,
  discovered work registered, `check_plan` green, then the report.
- Every major turn ends with `show_current_iteration` pasted verbatim in a code
  block, after the report and before any question.
- **Register before doing.** Owner feedback, a bug, a discovery, a follow-up you
  would "just do" — it becomes a task first.
- **Tick with evidence.** Name the proof: a test count, a commit, an artifact
  path. "I implemented it" is not evidence, and a passing build is not a working
  feature.
- Task lines are one short sentence, 5-15 words, saying what the work is. No
  evidence, counts, SHAs or URLs in task text. Evidence goes in the commit and
  the showcase.
- One active iteration. Never `close_iteration --force`; move the open tasks
  forward and say so.

Known gaps, registered in the backlog rather than worked around with a hand
edit: powerplan cannot renumber a duplicate iteration version, and
`append_prose` appends near the backlog rather than at the header.

## Requirements — SRS rows before code

**This project keeps its SRS in `spec/`, not `docs/srs/`** (DECISIONS.md D2).
The ID scheme `SRS-<FEAT>-NNN` already matches PowerFlow canon; only the
directory differs, and `spec/` is the audited layer both implementations trace
to.

- `spec/srs/` — shared requirements. `spec/srs-web/` — web-only.
  `spec/srs-native/` — native-only (parked).
- Columns: ID | Requirement | Rationale | Verification | Trace.
- One testable *shall* per row. A number, never "fast". Verification is
  `Test | Analysis | Review | Demo`, and a `Test` row with no path in Trace is
  **not verified**.
- Numbers are never reused. A changed requirement is a new row; the old one is
  marked `(superseded by ...)`.
- A named feature does not ship without its rows, and the rows land in the same
  change as the code.
- `docs/SRS_*.md` are dead pointers from an old migration. Never add to them.

## Close the loop — before "done"

In order, one evidence line each. Stop at the first red and fix it.

1. `npm run lint` and `npm test` — report counts.
2. `npm run test:e2e` **once** on the dev server. Red → read the *first* error,
   fix, run once. No rerun loops, no retries "to be sure", never enable retries.
3. **Smoke**: `npm run dev` launches and the browser console is clean. Verify it
   from your own tool output — never ask the owner whether it worked.
4. **Look at it.** Any UI change needs one capture per changed state from the
   running app, reviewed by you.
5. **Showcase artifact** (below) for any iteration or batch worth a version
   bump.

## The showcase artifact — REQUIRED

Finishing a piece of work means handing over something that *shows* what
changed. A written summary is not delivery. This is the part of PowerScroll's
flow the owner asked for by name; treat it as the deliverable, not a bonus.
Skip it only for a single trivial fix.

**Screenshots come from the running app. Never mock them up.** A recreation of
the UI in HTML is not evidence and must not be presented as one.

```js
const page = await browser.newPage({ viewport: { width: 1400, height: 880 }, deviceScaleFactor: 2 });
await locator.screenshot({ path: ... });   // element-clipped, not full-window
```

This repo already has the capture machinery: `scripts/shot.mjs` (2x element
capture), `scripts/record-web.mjs`, `scripts/record-gif.mjs`, and the
`docs/media/gallery-*.png` set.

Capture gotchas already paid for:

- A capture script must live **inside the repo** — Node resolves
  `@playwright/test` from the script's own location, not the cwd. Write it to
  the repo root, run it, delete it. Never leave capture scripts behind.
- Set state up through the store or the agent bridge rather than clicking
  through the chart.
- Crop tight. A full-canvas shot of a mostly-empty chart shows nothing.
- Pick states that actually differ. Two themes that both render near-white
  prove nothing.

**Structure — one record per change:**

1. An area label (Command bar, Chart, Status bar, Document settings).
2. A title saying what is now true.
3. **The defect stated plainly** — what was broken, and why.
4. The screenshots, as evidence.
5. Any decision the owner might want to veto, called out.

**Be honest about what is shown.** A before/after needs the old build actually
captured — a git worktree at the previous commit, served on a second port. If
that was not done, *state* the before rather than illustrating it, and say on
the page that it is stated rather than shown.

**Include a verification section with the real numbers** — failures, flakes and
contention included. A green number with the failures omitted is a lie of
composition.

**Design it from the app's own system.** Theme from
[`docs/design-tokens.md`](docs/design-tokens.md). Frame each screenshot the way
the app frames a chart. Render **both light and dark** before publishing —
theme bugs hide in the gap between source and output.

A **design demo** is the other half of this. Before a UI direction is chosen,
build an interactive prototype — as `docs/mockup/web-command-bar-mockup.html`
did for the command bar — and let the owner pick from it. The demo settles the
design; the showcase proves it shipped.

## The cold walkthrough gate

Gates that assert markers and state prove *correctness*, not *usability*. This
project learned that the expensive way. Every UX-touching slice must also exit
through a task-based cold walkthrough: pick the user goals the slice affects
("change a date", "link two tasks", "add a milestone", "retitle a task"),
execute them **as a first-time user would** — no test seams as shortcuts for the
gesture itself — capture per-step images, and review them against:

1. **Discoverable** — is the affordance visible on hover or selection, or does
   the flow need documentation to find?
2. **Direct manipulation first** — continuous values (dates, %, positions) are
   changed by dragging with a live preview. Steppers for continuous values are
   forbidden.
3. **Consistent verbs** — the same label always does the same thing, and
   commit/cancel semantics (click-away, Esc, Enter) are identical across every
   editor. A product where one editor commits on click-away and another
   discards is broken even if both pass their tests.
4. **No traps** — every mode has a visible exit; Esc always returns to default.
5. **Constrained and snapped** — drags cannot leave the component, and snap to
   the scale grid where dates are involved.
6. **Platform conventions** — Ctrl/Shift multi-select, Del, F2/double-click.
   Never steal input the user directed elsewhere.

Findings are registered as tasks before the slice is called done. A slice that
passes its tests but fails the walkthrough is **not done**.

## Owner reports

The owner's sentence is the requirement. Register it verbatim
(`Owner report <date>: "..."`). The first commit is a **failing test that
reproduces the symptom as the owner describes it** — not your diagnosis of it.
Fix, run once, then `update_task` to `... - fixed <sha>, awaiting owner
verification` and **leave the box open**. The owner ticks it. The iteration
waits.

## Web agent bridge

An agent can author the chart in the running app through the `powerplanner-web`
MCP server (`mcp/agent-bridge/server.mjs`, registered in `.mcp.json`).

1. `npm run dev` (port 5180).
2. Click **Agent** in the app header — an explicit opt-in that doubles as the
   stop control. An "agent connected" chip appears in the status bar.
3. `get_status` first — it distinguishes "nothing attached" from "attached to an
   empty document".

Prefer **`apply_batch`** over many single calls: it applies an ordered list as
ONE undo frame, so the owner reverts your whole turn with a single Ctrl+Z, and
it is all-or-nothing, so an invalid operation changes nothing. Supply ids
yourself; later operations may reference entities created by earlier ones in the
same batch. Never move the viewport as a side effect of editing.

Dev-only by construction: dropped from production and portable builds, gated by
`tests/unit/bridge-build-isolation.test.ts`. Design:
[`docs/web-agent-bridge-plan.md`](docs/web-agent-bridge-plan.md). Requirements:
[`spec/srs-web/SRS-agent-bridge.md`](spec/srs-web/SRS-agent-bridge.md).

## Release

[`docs/RELEASING.md`](docs/RELEASING.md) is the runbook;
[`docs/DEPLOYMENTS.md`](docs/DEPLOYMENTS.md) is the observed-state ledger,
updated after every publication. Never release from an open iteration, never
with an owner report awaiting its tick, and never move a tag that published —
ship `+1`.

## Implementation rules

- Prefer the existing data model and layout pipeline over a new parallel path.
- Keep layout functions pure and unit-tested when behaviour changes.
- Keep chart output presentation-grade: crisp SVG primitives, stable spacing,
  readable labels, print-safe behaviour.
- Preserve offline-first portable HTML behaviour. Persistence changes must keep
  the `.html`, `.json` and `.yaml` round-trips working.
- Simplicity over completeness. Small, readable diffs. No new dependencies
  unless the task names them.
- `.mcp.json` is additive: when registering a server, preserve and smoke-test
  the existing `powerspawn`, `powerplan`, recorder and `powerplanner-web`
  entries. Never replace the `mcpServers` object wholesale.

## Memories and decisions

- [`docs/agents/memories/`](docs/agents/memories/) — one dated fact per heading,
  corrected in place, for anything the next agent would otherwise rediscover: a
  platform trap, a build quirk, a measurement that must match. Never task
  status, secrets, or SRS copies.
- [`DECISIONS.md`](DECISIONS.md) — a choice between alternatives with lasting
  consequences: context, decision, and what would reopen it.
- `docs/agents/context/<slice>.md` — the slice brief, deleted when the SRS and
  the code carry it.
