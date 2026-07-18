# Session Recorder ("Record" button) — design spec (v2.10.x program)

Status: IMPLEMENTATION IN PROGRESS — harness core verified; live meta-gate open — 2026-07-18
Author: coordinator session (live-verify evidence of the same day)

## Implementation status — 2026-07-18

R1a and the harness-testable recorder core are implemented. The entity dump
uses the shared pure serializer and cached scene primitives for semantic text,
style, clipping, parentage, and geometry. `trace_entity_dump` passes all six
named invariants. The real `session-recorder` profile writes and re-reads a
session directory; its dir/schema/event-type/selection/gesture/dedupe/error/REC
checks pass. The latest verification produced 13 snapshots (7 full, 6 deduped)
with no false empty entity arrays, and `session_report.py --selftest` passes.

R1 is not complete. The live PowerPoint three-failure meta-test has not been
run. Ribbon integration is implemented and accepted by PowerPoint customUI;
overlay-only chrome is not yet represented as
entities, and recorder error coverage is focused on user-action boundaries
rather than every swallowed defensive catch. SR-SMO-09 is also honestly RED:
the latest optimized recorder harness measured 23.10 Hz (30 Hz required).
Cadence is variable but consistently below budget; the recorder gate remains
RED. A rejected full-chart geometry refresh fell to 0.89 Hz; cached task-shape
bindings retain fresh child geometry without that regression.

### Live session findings — 2026-07-18 16:25

Session `20260718-162522-519-14476` successfully proved an independent native
mutation: only `TASK_LABEL/t1` geometry changed while its TASK and progress
entities stayed fixed. It also exposed observability gaps. All 42 input events
were overlay `WM_MOUSEMOVE`; the native clicks/drags that caused both mutations
had no down/up/gesture events. PowerPoint reported `CHART_ROOT` twice with
`hasChildShapeRange=false`. Saved overlay frames were transparent/black except
for chrome, and transition captures preceded the repaint, so selection borders
appeared in the following transition's frame. Native button causality remains
open. Post-paint chart+chrome capture is now implemented, and cached live shape
bindings proved that a 4 pt TASK_PROGRESS-only move is present in snapshots even
when the root geometry/count/tags remain unchanged.

## 1. Why — the live-vs-harness gap, proven today

Live verification of v2.9.0 (task-bar unit selection) in a real PowerPoint on
2026-07-18 found three failures, **all invisible to the current instruments and
all green in the harness**:

1. **Task-bar click → native label child selected.** Clicking the Discovery bar
   body put native PowerPoint grips + a rotation handle on the label sub-shape,
   flipped the ribbon to Shape Format, and left our chrome absent (app bar stayed
   in document context). The suppression path logged nothing — the sink/Tick
   resolved the selection as allowed CHART_ROOT while PowerPoint had click-entered
   the group and selected the child (`Selection.ShapeRange` vs
   **`ChildShapeRange`** — root-cause hypothesis for v2.9.1).
2. **Drag-to-create commits nothing, silently.** Real mouse drags in a row band
   painted a 1-px hairline preview (UF-05 requires a real-height bar), committed
   no shape (verified via COM enumeration: zero TASK children), and produced
   **zero log lines** — no gesture start, no commit attempt, no error.
   Double-click create: same silence.
3. **Stale preview paint.** The hairline remnants persisted on the overlay until
   an unrelated rebuild repainted it.

Why the harness can't see any of this: `ppappbarshot --trace` drives the overlay
in **its own process** through test seams (`Overlay_SelectForTest`, cursor
override + posted messages) — real `WM_LBUTTONDOWN` routing, real mouse capture,
click-into-group native selection, and focus semantics are never exercised.
The add-in's in-POWERPNT state cannot be queried from outside, and
`%TEMP%\powerplanner-addin.log` is unstructured, blind to input events, and
drowned by tick spam ("hotkeys unregistered" every 156 ms).

**Conclusion (user directive):** agents need first-class visibility into live
sessions. The user records a procedure, narrates what is wrong, and the agent
reads the recording as ground truth.

## 2. What — three deliverables

### R1a — Entity dump (PRIMARY channel; user directive 2026-07-18)

Screenshots/computer-use are too slow and token-heavy as the agent's main
window into the app. The primary observability channel is a **complete entity
dump**: every rendered primitive as a generic entity with uniform properties.

The substrate exists — the scene pipeline's `Prim` list (Scene.h:34,
`tagKind`/`tagId`) IS the entity list; the hit snapshot and PP_KIND/PP_ID tags
carry geometry. R1a formalizes it (no renderer rewrite):

```json
{"entities":[
  {"id":"discovery","kind":"TASK","parentId":"","rowId":"r1",
   "slideRect":[l,t,w,h], "screenRect":[l,t,w,h], "z":14,
   "style":{"fill":"#5B67E8","stroke":"#3A44B0","strokeW":1.0,"fontPx":0},
   "text":"", "flags":{"selectedOwn":true,"selectedNative":false,
   "hover":false,"clipped":false,"visible":true}},
  {"id":"discovery","kind":"TASK_LABEL","parentId":"discovery", ...},
  ...]}
```

- Emitted/cached after **every scene build/reconcile**; covers ALL prims
  (tasks, labels, progress, pct, milestones+labels, deps, markers, axis, rails,
  rows, title, chrome), not just hit-testable ones.
- Pure + ppops-testable (no COM); identical serializer in harness and live, so
  harness-vs-live diffs become machine-checkable.
- Exposed via: `Overlay_DumpEntitiesForTest` seam (harness), recorder
  `snapshot` events (live sessions), and later `ppsession.exe entities` (live
  query, R3).
- Recorder `frame` PNGs become optional corroboration, throttled harder;
  agents reason from entities first.

### R1b — in-DLL flight recorder (event core)

A **Record** toggle (app bar Settings group + ribbon button). While recording,
the add-in writes a session directory:

```
native\records\<session-id>\
  events.jsonl        # one JSON event per line, monotonically increasing t/seq
  frames/NNNN-<trigger>.png
  meta.json           # dll version, pptx name, screen/DPI, chart id, start time
```

`POWERPLANNER_RECORDS_DIR` overrides the root. Installed builds fall back to a
per-user records directory, and management tooling continues to discover the
legacy `%TEMP%\powerplanner-sessions` root without silently migrating it.

Event schema (`type` + payload; `t` = ms since session start, `seq` monotonic):

| type | payload | source tap (from observability inventory) |
|------|---------|------------------------------------------|
| `input` | `{surface: overlay/appbar/card/menu, msg, pt, client, mods, hit:{zone,kind,id}}` | top of `OverlayWndProc` (Overlay.cpp:3910), `AppBarWndProc` (6198), card/menu procs; `WM_MOUSEMOVE` throttled ~20 Hz but always emitted on hit-result change |
| `nativeSel` | `{kind, id, hasChildShapeRange, childKind, childId, resolution}` | selection sink (Connect.cpp:195) + Tick watchdog + `Overlay_OnNativeSelectionChanged` (8106) — **must capture the raw resolution details, this is bug #1's blind spot** |
| `ownSel` | `{kind, id, reason}` | `SetOwnSelection` / `ClearOwnSelection` / `ApplyClickSelection` |
| `gesture` | `{phase: start/update/commit/cancel, kind, id, payload, result, hr}` | `StartDragGesture` / `StartCreateGesture` / commit helpers — **commit-absent after start is bug #2's signature** |
| `op` | `{cmd, phases, dispatchMs, hr}` | `HandleAppBarCommand` + `Gantt_GetLastOpPhasesForTest` phases |
| `paint` | `{surface, count, tMs}` | `RenderOverlay` post-ULW (5521) — reuse the paint-ts ring, always-on while recording |
| `snapshot` | full chrome-state JSON **+ entity dump (R1a)** | shared serializers; on every ownSel/nativeSel/gesture transition + 1 Hz idle; entity dump deduped by scene signature |
| `frame` | `{file, surface: composited, trigger, screenRect}` | Post-paint screen-composited chart + PowerPlanner chrome capture; on transitions + pre/post commits, throttled ≤2/s |
| `doc` | `{taskCount, rowCount, docDatesSignature, ...}` | after mutations/reconcile |
| `error` | `{where, hr, msg}` | **every swallowed catch emits one — no more silent failures** |
| `note` | `{text}` | user marker (small "flag" press while recording, optional R1.1) |

Constraints:
- Off by default; zero overhead when off (existing paint sampler pattern).
- With recording ON, paint cadence gate SR-SMO-09 (≥30 Hz drag paint) must stay
  green — recorded evidence itself proves it (`paint` events).
- New structured sink — do NOT parse/extend `powerplanner-addin.log` (three
  writers, incompatible prefixes).

### R2 — session report tooling (agent consumption)

`native/tools/session_report.py <session-dir>`:
- Reconstructs a markdown timeline: inputs → hit results → selection
  transitions → gestures/ops → doc changes, with frame thumbnails referenced.
- Diffs consecutive `snapshot` events (only changed fields).
- Re-runs applicable harness invariants against recorded snapshots (e.g.
  `task_label_selects_task_unit`) so a live recording can PASS/FAIL the same
  rules the harness gates — closing the live-vs-harness loop.
- `--assert <scenario.json>` mode so PLAN acceptance items can gate on a
  recorded live session, not only on harness traces.

### R3 — recording management MCP

User feedback established a concrete MCP need: make recordings discoverable,
readable, reportable, and safely removable without AppData/Temp spelunking or
arbitrary filesystem access. `native/tools/session_manager.py` owns root
discovery and deletion policy. `native/tools/session_mcp.py` exposes bounded
stdio tools for list/get-events/get-report/generate-report/delete and is
registered by the project `.mcp.json`.

Deletion accepts only an exact discovered session id, requires the same id as
explicit confirmation, rejects a session containing `.active`, and verifies
the resolved target remains below a known recording root. This MCP manages
completed artifacts; live PowerPoint query/injection remains a separate future
bridge decision.

**Subprocess / MCP stdio trap (2026-07-18):** `generate_recording_report` hung
for the full 60s timeout on large sessions while the same
`session_report.py` argv finished in &lt;1s from a shell. Confirmed cause:
`session_manager.generate_report` used `subprocess.run(..., capture_output=True)`
**without** `stdin=DEVNULL`, so the child inherited the MCP host's stdin pipe
(which FastMCP/anyio continuously reads). On Windows that deadlocks the child
until timeout. Not a stdout pipe-buffer deadlock — the CLI writes `report.md`
to disk and only prints the path. Fix: prefer in-process
`session_report.write_report`; any CLI subprocess path must use
`sys.executable`, `stdin=subprocess.DEVNULL`, and `capture_output=True` (drains
both pipes via `communicate`). Restart the MCP server process after code
changes — tools are not hot-reloaded.

## 3. Acceptance gates for R1 (meta-test: record today's failures)

1. Record a live session reproducing failures #1–#3. The recording MUST make
   each visible:
   - `nativeSel` event for the label click carrying `hasChildShapeRange` truth;
   - `gesture start` with no `commit` (or `error`) for drag-create;
   - `frame` PNGs showing the hairline preview.
   If the recorder cannot surface them, R1 is not done.
2. SR-SMO-09 stays green while recording.
3. Session dir + "REC" indicator visible to the user; toggle works from app bar.
4. Pure unit tests for the event serializer (ppops), E2E harness scenario
   `trace_session_recorder` (record during a scripted harness run, assert
   events.jsonl structure), and a **live recorded session committed as the
   ground-truth artifact** for the slice.

## 4. Rollout

- **R0** — SRS `spec/srs-native/SRS-SessionRecorder.md` (SR-REC-01..) + scenario
  stubs + this spec folded in. (worker slice, small)
- **R1** — capture core in Overlay/Connect + toggle UI. (worker slice, largest)
- **R2** — `session_report.py` + invariant replay. (worker slice, medium)
- **R1.5** — use a recording to root-cause and fix **v2.9.1 live task-unit
  selection** (ChildShapeRange) and **v2.9.2 live create-commit** — the first
  real customers of the recorder.
- **R3** — bridge decision after R1.5 experience.

Coordination protocol (applies to every slice): worker implements → coordinator
gates with ground-truth artifacts (report JSON / recorded session, not
checkbox claims) → commit with `[todo:]` tag → PLAN.md updated → a "what you
should see live" note for the user in the commit/iteration close.
