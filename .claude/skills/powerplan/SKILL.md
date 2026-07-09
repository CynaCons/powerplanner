---
name: powerplan
description: Show the project plan as an ASCII gantt chart of step-by-step actions — what's done, what's next, what's later — followed by details of each remaining phase/task. Use when the user asks "powerplan", "what's next", "show me the next tasks", "where are we in the plan", "ascii gantt", or wants a status review after closing an iteration. Reads PLAN.md (or the project's plan file) and cross-checks it against git history.
---

# PowerPlan — ASCII gantt status review

Turn the project's plan file into one terminal-friendly picture: an ASCII gantt
of the current milestone's steps (done / active / planned / unscheduled), a
"now" line marking exactly where work stands, and below it a short detail
section per remaining phase or task. The chart is **ordinal, not calendar**:
horizontal position means sequence, bar width roughly means size in units — no
dates unless the plan actually has them.

## Steps

1. **Locate the plan.** Default `PLAN.md` at the repo root; otherwise the file
   the user names, or `ROADMAP.md` / `docs/*plan*.md`. If none exists, say so
   and offer to create one — don't invent a plan.
2. **Cross-check against reality.** Read recent `git log --oneline` (and the
   working-tree diff of the plan file). Commits referencing a task id that is
   still unchecked in the plan mean the plan is stale — update the checkbox in
   the plan file (plans are kept current in real time) and mention the fix.
   Never mark something done on the chart that isn't verifiably done.
3. **Scope the chart.** Chart the *current milestone/phase and its remaining
   steps*, not the whole plan history. Collapse fully-completed earlier phases
   into a single one-line row (or omit them). List post-milestone work as a
   one-line "After:" footer. If a phase has sub-units, indent them under it.
4. **Render the gantt** in a fenced code block using the format below. Keep it
   under ~100 columns so it never wraps.
5. **Details below the chart.** For each remaining phase/unit, 1–4 sentences:
   what it is, its acceptance criteria / gate, dependencies, and anything
   absorbed into it (e.g. a defect fix scheduled inside a later unit).
6. **Flag the top 1–3 risks or decisions** — unowned items, gates awaiting the
   user (e.g. a pending visual review), known issues no slice owns. End with
   the single concrete next action.

## Chart format

```
<Milestone code + title>                         now
                                                  │
P1  Phase one (n units)             ████████████──┤  done, <one-line note>
P2  Phase two                           ██████████┤  green; <last open gate>
P3  Phase three
      unit-a                              ████████┤  done (<evidence>)
      unit-b                                      ├▓▓▓▓▓▓  ◀ NEXT: <what & why>
P4  Phase four                                    │      ░░░░░░
      unit-c                                      │      ░░░   <hook>
      unit-d                                      │        ░░░ <hook>
──────────────────────────────────────────────────┼──────────────────────────
Parallel / unscheduled                            │
      <unowned item>                              ├─?─?─?  <needs an owner>
      <recurring gate, e.g. user review>          ●      ●      ●
──────────────────────────────────────────────────┴──────────────────────────
After <milestone>:  <one line listing the next milestones>
```

Legend (include it under the chart when symbols beyond █/░ are used):
`████` done · `▓▓▓▓` active/next · `░░░░` planned · `─?─` unscheduled/unowned ·
`●` gate or review point · `│` the now line · `◀ NEXT` the single next unit.

Rules:
- Exactly one `◀ NEXT`. Done bars end at (touch) the now line; the next unit
  starts just right of it; later work steps progressively rightward.
- One row per phase; indent sub-units two spaces under their phase row.
- Right-of-bar annotations are ≤ ~40 chars — details belong in the section
  below, not in the chart.
- The `Parallel / unscheduled` band is only for real cross-cutting items
  (unowned defects, recurring review gates); omit it when empty.

## Notes

- The chart is a *view*; the plan file is the source of truth. After the user
  reprioritizes, update the plan file first, then re-render.
- Lead the response with any status correction found in step 2 (e.g. "X is
  already committed but the plan hadn't caught up — updated"), then the chart,
  then details, then flags.
- This skill is portable: nothing above is specific to one project. Copy
  `.claude/skills/powerplan/` into any repo with a plan file and it works.
