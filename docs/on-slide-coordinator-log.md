# On-Slide UX — Coordinator Log

Durable, repo-tracked narrative for the `onslide-coordinator` skill. Re-read this
on every rehydrate. State of record: session SQL `todos` (status) + git (`[todo: <id>]`
markers). Intent of record: `docs/on-slide-ux-plan.md`.

## Backlog (seeded at bootstrap)

| id | depends on | gate |
|---|---|---|
| disco-ops-harness | — | `native\build-ops.bat` → `OPS HARNESS OK` |
| disco-slide-window | — | `native\build-window-probe.bat` → `window-classes.txt` exists |
| ops-model-mutations | disco-ops-harness | `native\build-ops.bat` → `OPS HARNESS OK` |
| ctx-menu-actions | ops-model-mutations | `native\build.bat` → `[build] OK` |
| overlay-material | — | `native\build.bat` → `[build] OK` (+ overlay png) |
| overlay-toolbar | overlay-material, ops-model-mutations, disco-slide-window | `native\build.bat` → `[build] OK` |
| hover-rowband | disco-slide-window, overlay-material | `native\build.bat` → `[build] OK` |
| inline-edit | disco-slide-window, ops-model-mutations | `native\build.bat` → `[build] OK` |

Every native unit also re-runs `build-conformance.bat` (`1/1 fixtures passed`) and
`build-reflow.bat` + `ppreflow.exe` (`REFLOW PASS`) as regression gates.

## Cycle log

- bootstrap — seeded 8 units + 9 dep edges; schema extended with structured columns;
  integrity clean (0 missing deps, 3 ready).
- self-review (rubber-duck) — folded fixes: GanttOps DLL-link edit moved into
  ops-model-mutations (owns native/build.bat for the link); ctx-menu no longer edits
  build.bat + must verify no dead onAction callbacks; disco-slide-window gate hardened
  (delete stale txt, print WINDOW PROBE OK, require class records or
  FALLBACK_POLLING_ONLY); ops gate also re-runs conformance+reflow. Overlay lane runs
  SERIALLY (shared Overlay.cpp) per skill default. COM-backed gates (reflow/probe) run
  with a timeout + PowerPoint cleanup to avoid hanging the loop; pure-visual overlay
  smoke screenshots are best-effort, not hard gates (user gives visual feedback).
- cycle 1 — dispatched in parallel (disjoint allowed_paths): disco-ops-harness,
  disco-slide-window, overlay-material. All three validated from clean builds + committed:
  - disco-ops-harness → `OPS HARNESS OK` → 007038d
  - overlay-material → `[build] OK` → ecb8130
  - disco-slide-window → `WINDOW PROBE OK` → 279c9c7
  KEY FINDING: window probe = FALLBACK_POLLING_ONLY (no per-slide child window to
  subclass). Architecture decision recorded: all on-slide interaction goes through OUR
  overlay window + the 150ms Tick() poller (GetCursorPos mapped via PP_PROJ), never by
  subclassing PowerPoint. hover-rowband / inline-edit / overlay-toolbar specs updated.
  Skipped reflow regression for these two (neither touched layout/json/builder logic).
- cycle 2 — next ready: ops-model-mutations (critical path → unblocks ctx-menu-actions).
