# SRS - Interaction Reactivity & Smoothness (ASPICE-style)

## 1. Feature Overview / Purpose
User verdict (2026-07-11): the on-slide editor now LOOKS right but FEELS
wrong — "not reactive, not smooth, not intuitive; looks good but not usable."
Root causes (code audit): every edit rebuilds native shapes via
Ungroup→Delete→re-add→Group (hundreds of ms of COM churn + visible reflow),
masked by a whole-window LockWindowUpdate freeze; chrome state syncs on a
150 ms poll; text editing is card-form-first instead of inline. This SRS
makes responsiveness a specified, measured property.

## 2. User Goals & Interactions
- Any single-element edit (move, resize, ±1d, %, color, rename, add/delete
  one element) feels instant: the visual result is stable well under a
  quarter second, with no flash, no whole-window freeze, no reflow jump.
- Hover feedback (cursor, wash, handles) tracks the mouse frame-by-frame.
- Double-click renames in place, on the element, with the text where it was.
- Selection made in PowerPoint (or by the overlay) is reflected immediately.

## 3. Software Requirements (Functional)
- **SR-SMO-01 (in-place reconcile)**: `UpdateGantt` shall update EXISTING
  shapes in place (position/size/text/format deltas via the reconcile path)
  and create/delete ONLY elements added/removed by the op. The
  Ungroup→Delete-all→re-add→Group cycle is prohibited for single-element
  edits. Frame preservation (FITPERSIST) and PP_PROJ semantics unchanged.
- **SR-SMO-02 (latency budget)**: command dispatch → stable final visual
  ≤ 200 ms for single-element ops on the sample document (measured, see §4).
- **SR-SMO-03 (no global freeze)**: single-element ops shall not
  LockWindowUpdate the PowerPoint window; masking is permitted only for
  structural rebuilds (row insert/delete, scale change) until SR-SMO-01
  covers them too.
- **SR-SMO-04 (immediate hover)**: hover cursor + wash react on the
  WM_MOUSEMOVE path (next overlay repaint), not the 150 ms tick.
- **SR-SMO-05 (event-driven selection)**: subscribe to the application's
  WindowSelectionChange event (COM sink) so native-selection changes update
  chrome within one repaint; the tick remains as watchdog/fallback only.
- **SR-SMO-06 (optimistic commit echo)**: on drag commit, the overlay keeps
  painting the committed geometry (ghost → applied echo) until the native
  shapes report the new state, so the user never sees the old position
  reappear or a blank gap.
- **SR-SMO-07 (inline rename)**: double-click on a bar label, row label,
  milestone/marker/note label opens the INLINE editor positioned on that
  label (existing inline editor infrastructure); the card editor remains for
  Edit (dates/percent/color).
- **SR-SMO-08 (undo integrity)**: all of the above preserve one undo entry
  per gesture.

## 4. Verification Approach (E2E with Native Seams)
- Trace steps gain wall-clock timestamps; new `opLatencyMs` per trace =
  dispatch→(post-rebuild stable state). New invariant `op_latency_budget`
  (≤ 200 ms) applied to single-element-op profiles (nudge, percent, color,
  rename, task-move-commit).
- New trace profiles: `task-nudge-latency`, `task-color-latency`,
  `drag-commit-echo` (assert no intermediate frame equals the OLD geometry
  after commit — echo invariant via screenshots).
- Reconcile assertions: after single-element ops, shape COUNT delta == the
  op's semantic delta AND untouched shapes' ids/z-order stable (extend the
  dump/PP round-trip checks) — proves in-place path.
- IDLESTABLE, CHROME CALM, APPBAR FIT, flash invariants remain green.
- Live feel check by the user at iteration close (final judge).

## 5. Non-Functional / Constraints
- Ground rules of docs/onslide-v4-plan.md §1 (fixtures frozen, PP_PROJ
  frozen, tokens, DPI, input-neutral harness, exceptions contained).
- Event sink must degrade gracefully (sink connect failure → poll-only).
- No new .cpp without .bat updates (prefer header-only).

## 6. Open / Related
- Builds on the shared BuildProjectedScene refactor (rows-only fallback
  window today..today+30d) landing with v2.5.2.
- Related: docs/SRS_OverlayLifecycle.md (paint gating),
  docs/SRS_CreationFlows.md, docs/improvements-backlog.md NAT-07/ARC-07.
