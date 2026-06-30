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
- cycle 2 — ops-model-mutations validated (OPS HARNESS OK, 1/1 fixtures passed,
  [build] OK, REFLOW PASS) → bfecf63.
- self-review #2 (cycle1-review, code-review) — Overlay.cpp CLEAN (no per-paint GDI
  leak, transparency + Tick/auto-reflow preserved); build scripts do not fake markers;
  scale/conformance round-trip safe. Found 1 latent bug in the discovery harness
  window-probe.cpp (COM pointers Released after CoUninitialize; Close gate too wide) →
  logged as low-priority fix-window-probe-com-teardown (no dependents).
- cycle 3 — ctx-menu-actions validated ([build] OK; all 12 onAction callbacks resolve
  in GetIDsOfNames; MutateChart->GanttOps->re-emit verified real, not hollow) → dbed209.
  This delivers on-slide right-click editing: Add Task/Row, Delete, Nudge +/-1, % +/-10,
  Change Scale. (ctx-menu edits only Connect.*, outside conformance/reflow link sets.)
- cycle 4 — overlay lane + fix, validated serially:
  - overlay-toolbar → [build] OK + overlay harness links; inspected real (WM_NCHITTEST
    gates buttons, MA_NOACTIVATE keeps selection, g_mutating re-entrancy guard vs Tick,
    no throw out of WndProc) → 7f87aa9
  - fix-window-probe-com-teardown → WINDOW PROBE OK exit 0 → 54376c4
  Status: 7 units done. Remaining: hover-rowband, inline-edit (both serial on Overlay.cpp).
- self-review #3 (cycle4-review) dispatched over the overlay-toolbar diff + the two
  remaining specs (held next dispatch until findings land, since both build on the
  freshly-rewritten interactive Overlay.cpp). Known design risks to confirm: (a) overlay
  window currently covers only the selected shape's bounds, so a whole-chart row-hover
  band may need a larger/second window; (b) a child EDIT on a WS_EX_NOACTIVATE window may
  not get keyboard focus — inline-edit needs a focusable approach.
