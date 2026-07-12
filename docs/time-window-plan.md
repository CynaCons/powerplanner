# Time Window Editing — Feature Plan (v2.7.x)

**Registered:** 2026-07-12 (user feature request). **Status:** REVIEWED — adversarial subagent review 2026-07-12 found 3 critical / 5 major / 10 minor issues; ALL folded into the sections below (marked 'review-amended' / 'amended per review'). APPROVED by user 2026-07-12 (D1 = non-interactive arrow-port continuation glyph; D2-D4 defaults confirmed). W1 COMPLETE 2026-07-12 (model, pure scene clipping, C1/C3/cache hardening, and repair-lossless trace green); W2 remains next.
**One-line:** the chart gets an explicit, user-controlled time window, edited by dragging
arrow ports at the left/right ends of the timescale header; elements outside the window
clip or hide (losslessly — the document keeps everything), and the whole chart rescales
to the new window on drop.

---

## 1. Product behavior (what the user sees)

### 1.1 The window becomes a first-class setting
- Today the visible date range is **derived**: `[min(dates) − 5%pad .. max(dates) + 5%pad]`,
  recomputed from content. After this feature the document carries an explicit
  `windowStart` / `windowEnd` (ISO dates). When absent (all existing charts), behavior is
  **unchanged** (auto-fit) — full backward compatibility with existing PP_DOC tags,
  fixtures, and goldens.
- Once the user drags a window port, the window becomes explicit and persists in PP_DOC
  (round-trips through save / Import from slide / Repair layout, like every setting).

### 1.2 Editing gesture (the ports)
- **Hover over the timescale header rows** (the two axis bands at the top) → two
  **arrow-shaped ports** fade in at the header's left and right edges (◀ / ▶,
  theme-token painted, same affordance family as link ports).
- **Mouse-down on a port + drag horizontally**:
  - RIGHT port dragged right → `windowEnd` moves later (window **expands**);
    dragged left → moves earlier (window **shrinks** from the right).
  - LEFT port mirrored for `windowStart`.
  - Movement **snaps to the smallest visible scale unit** (day at day-scale, week at
    week-scale, …) — same rule as marker drags (SR-IXC-04/12).
  - A **live pill** shows the candidate window: `2026-06-01 → 2026-08-10  (+7d)` —
    live-preview convention SR-IXC-03.
- **Two-phase rendering (user's explicit design):**
  - **During the drag**: only the overlay repaints — a lightweight **axis preview**
    (recomputed tick/label strip painted over the header area) plus the pill. The
    PowerPoint shapes are NOT touched per mouse-move. Optional cheap extra: dim the
    chart body 10% to signal "preview mode".
  - **On release**: commit `SetTimeWindow(start,end)` → one rebuild rescales every
    element to the new `ptPerDay`. Budget: **≤ 2s** for a window commit (separate from
    the 200ms single-element budget — this is a whole-chart view operation; the live
    preview keeps the *feel* responsive). Esc mid-drag cancels (SR-IXC-07).
- **Degenerate-window guards:** minimum span = 1 visible scale unit; maximum span
  scale-dependent (review m5: cap so the AXIS_MAJOR tick count stays ≤ ~130 at the
  current scale — a flat 20-year cap would emit ~240 week ticks); a port can never
  cross the other edge.

### 1.3 Clipping and lossless hiding
- **Fully outside** the window → the element's shapes are simply **not emitted**
  (task bar+label+progress, milestone diamond+label, marker line+chip, anchored notes).
- **Partially inside** → the bar is **clipped at the window edge**: flat cut at the
  boundary + a small "continues" indicator (2px token-colored tick or chevron on the cut
  edge) so a truncated bar is distinguishable from one that genuinely starts/ends there.
  Progress fill clips with its bar (fill fraction still computed on TRUE task span).
- **Left rail is never filtered**: every row label renders regardless of whether the
  row currently has visible bars (user requirement).
- **Dependencies:** an elbow renders only when BOTH endpoint shapes exist; if one end is
  hidden, the elbow is hidden with it (revisit later: edge-pointing stub arrow).
- **Losslessness:** hiding is purely a scene-builder filter over the unchanged document.
  Dragging the window back over a hidden task re-emits it **bit-identical** (same dates,
  color, %, label placement, deps) — enforced by a dedicated e2e (see §4, W3).

### 1.4 Escape hatches & discoverability
- Settings popover gains **"Time window: Fit to tasks"** (clears windowStart/End →
  auto-fit) and shows the current window when explicit.
- The hover hint on the header teaches the gesture ("Drag ◀ ▶ to change the time window").
- Cold walkthrough "change-the-time-window" joins the walkthrough gate set.

### 1.5 Explicit non-goals (registered follow-ups, NOT this program)
- Panning (dragging the axis middle to shift both edges together) — natural follow-up,
  same machinery, registered as W-FUP-1.
- Mouse-wheel zoom on the axis — W-FUP-2.
- Auto-scale-switch (day→week when the window grows past N days) — W-FUP-3.
- Web renderer parity — W-FUP-4 (foundation spec written now so web can follow).

---

## 2. Technical design

### 2.1 Data model & foundation spec (shared layer — spec/, not native-only)
- `PpDocument.windowStart`, `PpDocument.windowEnd` (std::string ISO, default "" = auto).
  JSON round-trip omits empty (canonical-form rule, like axisNumbering). Schema +
  spec/data-model.md updated additively. `SetTimeWindow(doc, startISO, endISO)` +
  `ClearTimeWindow(doc)` pure ops in GanttOps (+ ops-test coverage: guards, snap-free
  storage — snapping is a GESTURE concern, the model stores exact dates).
- **Validation in the op**: end ≥ start + 1 unit; reject malformed dates; clamp span.

### 2.2 Scene builder (GanttScene.h — the heart of the change)
- `BuildProjectedScene`: when `windowStart/End` set → use them as the projection window
  **verbatim, no pad**; else legacy min/max + 5% pad. `ptPerDay = contentWidth / spanDays`
  exactly as today.
- New **clip stage** after prim emission (pure C++, unit-testable):
  - prim fully outside `[originX .. originX+contentW]` → drop;
  - prim straddling an edge → truncate x/w at the edge and tag `clippedL/clippedR`
    (new Prim flags) so the emitter can draw the "continues" tick; text prims whose
    anchor bar is dropped are dropped with it; label prims for clipped bars re-run the
    existing fit fallback within the clipped width.
  - ROW rail/band prims and axis prims are exempt from the filter.
- **PP_PROJ correctness (session-learned trap):** a window commit changes `ptPerDay` and
  `minDay` → **PP_PROJ and PP_ROWY MUST be rewritten** on every window commit. The
  scene-diff fast path only writes PP_DOC; therefore **window commits bypass the fast
  path by design** (see 2.4).
- **Projection invariant (review-amended, M5):** explicit window ⇒ the projection window
  IS `windowStart..windowEnd` verbatim; `DocDatesFitPaddedWindow` is **not consulted**
  (as written it returns false whenever any element sits outside — the NORMAL state of
  a windowed chart, which would force structural rebuilds on every nudge). Cache
  validity keys on window equality (`g_cacheWinStart/End`); both `SetTimeWindow` and
  `ClearTimeWindow` invalidate the scene cache; Clear re-enters auto-fit derivation.

### 2.3 Hit-testing & gesture (Overlay + GanttHitTest)
- New hit zone `HtZone::WindowPortL / WindowPortR`: a ~24px square at each end of the
  axis header band (header rect already known to BuildRowBands via the axis prims /
  chartRect top band). Ports are hover-gated: painted + hit-testable only while the
  cursor is over the header band or a drag is active (same pattern as row-adder chips).
- New `DragKind::WindowEdgeL / WindowEdgeR`:
  - `StartWindowEdgeDrag(right, downPt)` snapshots the current window (explicit or the
    derived auto window — first drag "materializes" the auto window as its baseline).
  - `UpdateDragGesture`: candidate edge day = anchor edge day + px→days via CURRENT
    `ptPerDay`, snapped to unit, clamped vs the other edge; sets `g_dragPillText`
    (window pill) and marks the axis-preview dirty; **RequestOverlayRepaint only**.
  - **Axis preview paint**: a new overlay paint pass that, while a window drag is
    active, covers the header band with a token-painted preview strip (background fill
    + recomputed tier labels/ticks for the CANDIDATE window, reusing the pure header
    layout math from GanttScene via a COM-free helper). This is the "only the timescale
    rows update during drag" behavior the user specified. The real header shapes stay
    frozen underneath (covered by the opaque preview).
  - **WM_LBUTTONUP**: snapshot locals (STALE-GLOBAL RULE — every drop commit reads only
    snapshotted values; two U5 bugs came from violating this), then
    `CommitWindowGesture(startISO, endISO)` → `SetTimeWindow` → `RebuildChart`.
  - Esc / capture-loss → cancel, repaint clears the preview (CLEAR-MUST-REPAINT rule —
    the echo-pixels lesson).
- Dump fields for the harness: `windowStart/windowEnd/windowDragActive/windowPillText`
  + `windowPortLRect/windowPortRRect` (ground-truth affordance rects — profiles must
  click exactly where the overlay hit-tests; U4/U5 lesson).

### 2.4 Commit path & performance
- Window commits go through `RebuildChart` with the fast path **explicitly ineligible**
  (add a cheap check: cached windowStart/End differ → skip TryApplySceneDiffFast). The
  full reconcile's in-place branch then rewrites geometry for ALL prims + PP_PROJ +
  PP_ROWY + PP_DOC and preserves the group frame (bounds are constant — the chart
  KEEPS its slide footprint; only the internal scale changes — so no group-resize trap).
  Elements entering/leaving the window make it structural — also handled by the
  reconcile (delete/create only the entering/leaving prims via the existing matcher).
- New latency trace `window-change-latency`: budget `window_commit_budget ≤ 2000ms`
  measured via OPLATENCY around the commit dispatch. If the in-place reconcile proves
  slower on the 6-task fixture, optimize AFTER measuring (per the latency-truth memory:
  measure first, the cost is always COM-call count).

### 2.5 Interactions with existing features (checked against current SRS/e2e)
- **Drag clamp (UF-08)**: task drags clamp to the *window*; already derived from
  ProjectionPx → picks up explicit windows automatically; e2e re-run proves it.
- **Marker/milestone snap, create-at-point, empty-cell math**: all read PP_PROJ →
  correct as long as PP_PROJ rewrite rule (2.2) holds.
- **Frozen-window nudge fast path**: unchanged for in-window edits; a nudge that pushes
  a date OUTSIDE an explicit window no longer grows the window (auto mode did) — the
  bar clips instead. This is a deliberate semantic change ONLY when a window is
  explicit; register in SRS.
- **Suppression/selection**: hidden task = no shape = not clickable; its rail row still
  selectable; if the currently-selected item becomes hidden by a window change, clear
  ownSel to component context (UF-07 semantics).
- **Undo**: one undo entry per window commit (StartNewUndoEntryIfPossible before the
  rebuild); Ctrl+Z restores the previous window + geometry (tag+shapes same entry).
- **Walkthrough/gallery**: new walkthrough + gallery-window.png contexts.

---

## 3. Requirements to author (W0 — before any code)
`spec/srs-native/SRS-TimeWindow.md` (ASPICE tables, SR-WIN-01..~14) covering: explicit
window persistence + auto-fit default (foundation rows go to spec/data-model.md);
port affordance visibility rules; drag semantics (snap, clamp, live pill, Esc,
two-phase render); clip/hide/lossless-rerender; rail exemption; dependency-hiding rule;
selected-item-hidden → context reset; window commit budget ≤2s; undo atomicity;
Fit-to-tasks reset. Each row's Verification column names a harness scenario below.

---

## 4. Implementation slices (each: SRS → e2e → impl → gates → walkthrough)

**W0 — Spec + registration (no code)**
SRS-TimeWindow.md; schema/data-model additions; scenario stubs
(trace_window_edge_drag.json, trace_window_clip_rerender.json,
trace_window_commit_latency.json); PLAN checklist. *Gate: spec review.*

**W1 — Model + scene clipping (pure layer, no UI)** *(amended per review)*
windowStart/End + JSON round-trip; SetTimeWindow/ClearTimeWindow + ops-test;
BuildProjectedScene explicit-window mode + clip stage + clipped flags + "continues"
tick emission; PP_PROJ/PP_ROWY rewrite rule + fast-path ineligibility on window change;
InvalidateSceneCache on Set AND Clear. **Review-critical additions:**
- **C1 — clipped shapes are NOT a geometry source of truth:** `ReflowFromSlide`
  back-projects task dates from shape Left/Width (GanttBuilder.cpp ~1448) and runs
  unconditionally from `FitChartRootToFrame` — with clipped bars it would REWRITE true
  dates to the clipped span (data corruption). Rule: under an explicit window, skip
  back-projection for elements whose doc dates extend beyond the window (or tag clipped
  shapes PP_CLIP and skip those). Dedicated e2e: Repair-layout + chart-resize on a
  clipped chart, PP_DOC dates byte-identical.
- **C3 — structural classification:** add windowStart/End comparison to
  `IsStructuralDocChange` (Overlay.cpp ~6240) AND `IsStructuralDocDelta`/doc patchers
  (GanttJson.cpp ~92) so window commits force the paint lock (no whole-chart flash) and
  the JSON patch path never silently drops the window fields.
- **M1 — label overhang clipping:** milestone/task/marker LABEL prims can legally
  overhang the window edge; the clip stage must clamp/drop them too or union bounds
  drift → frame re-fit → axis-independent rescale + ReflowFromSlide (compounds C1).
- **M3 — DEP unit-clip:** an elbow whose endpoint is clipped-but-visible must be
  dropped or clipped as a UNIT (all 3 segments + arrowhead), never per-segment.
- **m8 — brackets + anchored notes:** explicit clip rules + unit tests (bracket = 5
  prims clipped as a unit; a note anchored to a hidden bar hides; anchored to a clipped
  bar stays at the clipped anchor).
*Gates: build-ops (clip math incl. labels/deps/brackets, lossless re-emission equality),
conformance byte-identical, Repair-layout-lossless e2e, existing trace regression.*

**W2 — Ports + drag + axis preview (UI, no commit)** *(amended per review:
m1 — no header rect exists in the hit snapshot today [BuildRowBands filters out
HEADER_BAND/AXIS_* kinds]; derive it from PP_ROWY first-row top minus scaled AXIS_H or
admit HEADER_BAND to the walk. m2 — the axis tier layout is ~160 lines of local lambdas
inside BuildGanttScene; the COM-free preview helper is a real extraction refactor with
its own axis-golden regression gate. m3 — window ports hit-test BEFORE
HitTestClientPoint in WM_LBUTTONDOWN, same pattern as link ports — do NOT shrink marker
bands. m10 — port hover paint transition-only like UpdateHoverFromCursor so IDLESTABLE
stays paint-free.)*
Header hover ports (paint + hit zones + dump rects); DragKind::WindowEdgeL/R; snapped
candidate + window pill; axis-preview paint pass; Esc cancel. Commit temporarily wired
to a no-op behind the seam so W2 can gate on preview-only behavior.
*Gates: trace_window_edge_drag (mid-gesture dump: windowDragActive, pill text has two
ISO dates, preview painted — pixel-diff of the header band vs pre), IDLESTABLE stays
paint-free, hover ports don't regress link-port/marker hit-tests (full drag-suite
re-run).*

**W3 — Commit + clipping e2e + budget** *(amended per review)*
CommitWindowGesture; undo entry; **M4: the hidden-selection reset generalizes to ANY op
whose result de-emits the selected item** (a nudge can push the selected task outside an
explicit window — clear to component context, never invisible edits); **C2:**
`ComputeDragPxPerDay`/`ComputeEmptyCellPxPerDay`/`AnchorDayFromScreenX` derive px/day
from shape rect ÷ TRUE day span — falsified by clipped bars; under an explicit window
prefer the PP_PROJ path / skip clipped tasks (clipped-chart create/drag e2e); **M2:
undo e2e** (Ctrl+Z after an in-place window commit, then nudge — the scene cache must
not re-apply the undone window; add a PP_DOC drift probe when serving the cached doc);
**m4: tighten ChartWindowHiDay** to the window edge (currently overshoots by
~ROW_GUTTER/ptPerDay); **m6: harness driver owns the window_commit_budget rule wiring**;
**m7: commit echo suppressed/clipped for drags that end clipped**; **m9: Esc-cancel
e2e + first-drag zero-geometry-delta e2e + re-assert the header pixel-diff after the
real commit lands**; **D3 amended: measure BOTH in-place and delete+re-emit commit
paths unconditionally, record p50, 1s target / 2s ceiling**;
trace_window_clip_rerender: shrink window to hide 'discovery' → assert bar absent,
rail row present, PP_DOC unchanged except window; expand back → assert geometry/props
IDENTICAL to the pre capture (the lossless guarantee, compared via dump + pixel hash);
trace_window_commit_latency ≤2000ms; deps-hidden rule asserted.
*Gates: all new + full regression suite + walkthrough all.*

**W4 — Settings integration + polish + close**
"Fit to tasks" in the Settings popover (+ current window display); header hover hint
text; gallery-window captures + README mention; cold walkthrough
"change-the-time-window" added to the gate set; PLAN close + coordinator log.
*Gate: user visual/feel pass (final judge).*

---

## 5. Risks & mitigations (ranked)
1. **PP_PROJ staleness** — any path that skips the tag rewrite after a scale change
   corrupts every px↔day consumer. Mitigation: fast-path hard-ineligible on window
   change + a new invariant `pp_proj_matches_window` (dump PP_PROJ, driver checks
   ptPerDay consistency with window span) run in every window scenario.
2. **Commit latency on big charts** — all-prim geometry writes are O(prims)·~15ms COM.
   Mitigation: measure with the budget trace first; if >2s on realistic charts, batch
   into the delete+re-emit path (single group re-emission is sometimes FASTER than
   per-prim writes — measure both).
3. **Gesture/hit-zone collisions** — the header band overlaps marker hit bands (full-
   height) and the TODAY line. Mitigation: window ports take priority within the header
   band only; marker bands start below the header (verify + adjust HtSnapshot bands);
   e2e clicks derived from dump rects only.
4. **First-drag baseline** — materializing the auto window must not visibly shift
   anything (the explicit window = exactly the current derived window INCLUDING the 5%
   pad, so ptPerDay is unchanged at materialization). Assert zero-geometry-delta in e2e.
5. **Label fit on clipped bars** — clipped width can be tiny; existing inside→right
   fallback handles it; add the case to the clip unit tests.
6. **Session pathologies** — decl-order compile errors, stale-global commits,
   clear-without-repaint, harness exe staleness: all encoded as review checkpoints in
   each slice's dispatch prompt.

## 6. Open decisions for the user (defaults chosen, please veto)
- D1: DECIDED (user 2026-07-12): partially visible bars show a small NON-INTERACTIVE
  arrow-port glyph at the cut edge (same visual family as the window ports — a
  continuation cue, no hit zone).
- D2: first port-drag materializes the auto window including today's 5% pad (default;
  review-confirmed — the only zero-geometry-delta choice). Note: PP_PROJ carries no
  maxDay, so windowEnd is reconstructed from doc extents + pad (derivation specified in
  W1); the first pill shows padded dates (window start ≠ first task) — the walkthrough
  confirms this reads OK.
- D3: window commit budget 2s (default) vs stricter 1s (may force the re-emission
  optimization immediately).
- D4: when the selected task gets hidden by a window change: clear selection to
  component context (default) vs keep an "off-screen selection" state.
