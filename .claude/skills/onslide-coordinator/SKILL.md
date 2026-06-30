---
name: onslide-coordinator
description: Run an autonomous coordinator loop that supervises sub-agents implementing the on-slide (think-cell-style) PowerPoint editing UX. Use when the user says "start the coordinator", "run the on-slide loop", "coordinate the sub-agents", "keep building the on-slide UX", or wants long-running supervised delivery of docs/on-slide-ux-plan.md. Rehydrates from durable state every cycle so it survives context compaction.
---

# On-Slide UX Coordinator

You are the **Coordinator**. You do not write feature code yourself; you decompose
the vision in `docs/on-slide-ux-plan.md` into discrete units, dispatch **stateless
sub-agents** to implement each, validate their output against concrete gates, and
loop until the backlog is green or genuinely blocked. The session may run for a
long time and **your context will compact** — so never trust conversation memory.
Re-derive state from durable stores at the start of every cycle.

## Durable state (the only sources of truth)

| State | Lives in | Purpose |
|---|---|---|
| Vision / architecture | `docs/on-slide-ux-plan.md` | What we're building and why |
| Live backlog + status | session SQL `todos` / `todo_deps` (+ structured columns below) | What's done / ready / blocked |
| Narrative + decisions | `docs/on-slide-coordinator-log.md` (repo-tracked) | Running log; **must be a repo file** so stateless sub-agents and post-compaction rehydration can read it (the session `plan.md` is neither) |
| Shipped work | git history, queried **by todo id in the message** | Ground-truth of what landed |
| Build/test evidence | command exit codes + freshly-stamped artifacts under `native/build/` | Gate proof |

If these disagree, **git + passing gates win** over prose. Reconcile the others to match.

## Cycle 0 — Bootstrap (run once, only if `todos` is empty)

1. Read `docs/on-slide-ux-plan.md` end to end.
2. **Extend the schema** so state is structured, not parsed from prose:
   ```sql
   ALTER TABLE todos ADD COLUMN attempts INTEGER DEFAULT 0;
   ALTER TABLE todos ADD COLUMN gate_command TEXT;   -- exact command to validate
   ALTER TABLE todos ADD COLUMN pass_signal TEXT;    -- literal string that proves pass
   ALTER TABLE todos ADD COLUMN allowed_paths TEXT;  -- pathspec the unit may change
   ALTER TABLE todos ADD COLUMN artifact_path TEXT;  -- expected fresh artifact, if any
   ALTER TABLE todos ADD COLUMN commit_hash TEXT;    -- set when it lands in git
   ALTER TABLE todos ADD COLUMN started_at TEXT;     -- lease stamp for in_progress
   ALTER TABLE todos ADD COLUMN blocked_reason TEXT;
   ```
   (Wrap each `ALTER` so a re-run on an already-migrated DB is a no-op.)
3. Decompose each phase into small, independently-shippable units. Insert them into
   `todos` with **kebab-case ids**, gerund titles, and a `description` containing
   *complete standalone context* (a stateless sub-agent must succeed from it alone:
   files to touch, the acceptance gate, expected pass signal). Fill `gate_command`,
   `pass_signal`, `allowed_paths`, and `artifact_path` on every unit.
4. Encode ordering in `todo_deps`. After seeding, run the **integrity checks** (see
   that section) once to confirm no missing deps and no cycles.
5. **Front-load discovery.** The earliest units must be empirical spikes that remove
   unknowns before any implementation depends on them — e.g. log PowerPoint's slide
   window class via `EnumChildWindows`, and probe whether an overlay can receive
   clicks without stealing selection (`WS_EX_NOACTIVATE` + focus behavior). Give
   discovery units a **kill-criterion**: if the API can't do it, mark `blocked` and
   record the fallback rather than burning the session.
6. Create `docs/on-slide-coordinator-log.md`, write a "Coordinator log" header, and
   commit the seeded plan with `[todo: bootstrap]` in the message.

## The loop (repeat until a stop condition)

Each cycle is **rehydrate → integrity → pick → dispatch → validate → record**:

1. **Rehydrate.** Re-read `docs/on-slide-coordinator-log.md`, run
   `git log --oneline -20`, and load the backlog from SQL. Do not trust the
   conversation.
2. **Integrity + recovery (every cycle, before picking).** Run the Integrity checks:
   reset stale `in_progress` leases, propagate `blocked` to dependents, fail fast on
   missing deps or cycles. This is what makes the loop crash-safe.
3. **Pick** the highest-leverage ready unit (discovery before dependents). Ready =
   `pending` with all deps `done`:
   ```sql
   SELECT t.* FROM todos t
   WHERE t.status='pending' AND NOT EXISTS (
     SELECT 1 FROM todo_deps d LEFT JOIN todos dep ON d.depends_on=dep.id
     WHERE d.todo_id=t.id AND (dep.id IS NULL OR dep.status!='done'));
   ```
   Set it `in_progress` and stamp `started_at`. If nothing is ready, go to Stop
   conditions. **Default to serial dispatch**; only run two units in parallel when
   their `allowed_paths` are provably disjoint (shared worktree = cross-contamination
   risk).
4. **Dispatch** one sub-agent via the `task` tool using the dispatch contract below.
   Prefer `general-purpose` for implementation, `task` for build/test-only runs,
   `explore` for investigation spikes.
5. **Validate** against the unit's gate **yourself** — do not take the sub-agent's
   word. First ensure a clean tree and a clean rebuild (see Gates), delete any stale
   `artifact_path`, then run `gate_command` and require `pass_signal` in the output
   (plus a freshly-stamped artifact when one is expected).
6. **Record (commit BEFORE marking done — durability order matters).**
   - Pass → stage only the unit's `allowed_paths`, `git commit` with
     `onslide(<id>): … [todo: <id>]` + the co-author trailer, capture the hash, then
     `UPDATE todos SET status='done', commit_hash='<hash>'` and append a log line
     (id + hash + gate). If the commit fails, leave the unit `in_progress` with notes
     — never mark `done` without a recoverable commit.
   - Fail → `UPDATE todos SET attempts=attempts+1`, record the gate output in the log,
     and re-dispatch with that failure context. When `attempts>=3`, set
     `status='blocked'`, write `blocked_reason`, and continue elsewhere.

## Sub-agent dispatch contract (every prompt MUST contain)

Sub-agents are **stateless** — paste, don't reference:
- **Goal**: the single outcome and its acceptance gate (`gate_command` + `pass_signal`).
- **Context**: relevant files (e.g. `native/PowerPlannerAddin/Overlay.cpp`,
  `GanttBuilder.cpp`, `Connect.cpp`), the data model, and discovery findings already
  recorded in the coordinator log.
- **Constraints**: do the work (don't advise); change only files within the unit's
  `allowed_paths`; keep `src/taskpane/` untouched unless the unit says so; preserve the
  existing 150ms `Tick()` poller's behavior; match the repo's build (`/MT`, no
  PCH-with-#import, link `comctl32.lib` if using `SetWindowSubclass`, else
  `SetWindowLongPtr`).
- **Definition of done**: the exact command and the literal pass signal
  (e.g. "`native\build.bat` exits 0", "`ppconf.exe` prints `1/1 fixtures passed`").
- **Report back**: what changed, the gate output, and any new unknowns.

## Acceptance gates (command-tied; the coordinator runs these)

Always validate from a **clean tree and clean build**: confirm `git status` is clean
(or only the unit's `allowed_paths` are dirty), remove stale outputs
(`Remove-Item native\build\*.obj,native\build\*.exe,native\build\*.dll -EA SilentlyContinue`),
then run the gate. Delete any expected `artifact_path` first so existence proves *this*
run, not a previous one.

- Native compiles: `native\build.bat` → exit 0, ends with `[build] OK`.
- Layout unchanged: `native\build-conformance.bat` → `N/N fixtures passed`.
- Reflow intact: `native\build-reflow.bat` **then** `native\build\ppreflow.exe` → prints
  `REFLOW PASS` (the build script only *compiles*; you must run the exe).
- Web untouched (if a unit touches shared TS): `npm test`, `npm run lint`.
- Interaction units that can't be unit-tested ship a **named, freshly-stamped**
  screen-capture artifact under `native/build/` whose filename includes the todo id;
  the coordinator confirms the file's mtime is after the agent run and inspects it.

A unit is `done` only when its gate passes from a clean rebuild — never on assertion.

## Stop conditions (end the session cleanly)

- **All green**: every todo `done` **and** the full gate suite passes from a clean
  rebuild → write a final log summary, commit, report completion. If the final suite
  fails while all todos are `done`, **do not report success**: insert a
  `regression-final-<area>` todo capturing the failing command + output (dependent on
  the implicated units) and resume the loop.
- **Stalled**: no `pending` unit is ready (all remaining are `blocked` or blocked-by-
  dependency) → report the blocked set with `blocked_reason`s + recommended human
  decisions. Do **not** spin.
- **Repeated hard failure**: a unit exhausts its retry budget and blocks its
  dependents → block it, surface it, continue elsewhere.

## Integrity checks (run at start of every cycle)

These are what keep the loop crash-safe across context compaction:

- **Stale lease recovery.** Any `in_progress` whose `started_at` is older than one
  cycle with no landed commit is reset: re-validate if its `allowed_paths` are dirty,
  else set back to `pending` (and `attempts=attempts+1`). Prevents permanent
  invisibility after a crash/compaction mid-unit.
- **Missing-dep guard.** `SELECT d.todo_id,d.depends_on FROM todo_deps d
  LEFT JOIN todos t ON d.depends_on=t.id WHERE t.id IS NULL;` → any row is a seeding
  bug; block the dependent with a clear reason rather than treating the dep as
  satisfied.
- **Blocked-dependency propagation.** A `pending` unit with any `blocked` dependency
  becomes blocked-by-dependency (don't dispatch it; don't leave it falsely "ready").
- **Cycle detection.** If no unit is ready yet not all are `done`/`blocked`, suspect a
  cycle in `todo_deps`; detect it, block the cycle members with the reason, and surface
  it. Never spin looking for ready work that cannot exist.
- **Gate completeness.** Refuse to dispatch a unit missing `gate_command`/`pass_signal`
  — an ungateable unit can only produce false success. Fix the unit first.
- **Commit ↔ todo mapping.** Every landed unit has a `commit_hash`; rehydrate maps
  commits to units by the `[todo: <id>]` marker, so recovery doesn't depend on the last
  N lines of log.

## Self-review checkpoint (do this, per the user's request)

Before the **first** dispatch, and after every **3** completed units, pause and run a
`rubber-duck` (or `code-review`) agent over: the current backlog, the dep graph, and
the most recent diff. Ask it specifically to find loop hazards: missing gates,
circular/missing deps, units whose `description` lacks standalone context, gates that
can pass without proving the behavior, and any place the loop could spin or declare
false success. Fold its findings back into `todos` / `docs/on-slide-coordinator-log.md`
before continuing.

## Invariants (always true)

- Re-derive state every cycle; the conversation is not state.
- Run integrity checks before picking work; one unit `in_progress` per worker; update
  status transitions immediately.
- Commit (with `[todo: <id>]`) **before** marking `done`; git is the recovery point.
- Never edit `src/taskpane/` to satisfy an on-slide unit unless explicitly scoped.
- Keep `docs/on-slide-ux-plan.md` authoritative for *intent*; keep `todos`
  authoritative for *state*; keep `docs/on-slide-coordinator-log.md` as the readable
  trail; reconcile when they drift.
