# On-Slide Editor — Experience Specification (V4, normative)

> **The reference is executable.** `docs/mockup/onslide-mockup.html` (open it in
> a browser) is the normative look-and-feel: every behavior below can be
> exercised there, and every visual value traces to `docs/design-tokens.md`.
> User approved 2026-07-04 ("the look and feel is close to what I want…
> the menu is very good"). Where this document, the mockup, and the tokens
> disagree, precedence is: tokens → this document → mockup incidentals.
>
> Implementation plan + acceptance criteria: `docs/onslide-v4-plan.md`.

## R0 — Global invariants (apply to every requirement)

- **G1** PowerPoint shapes are a render target only; the overlay owns all input
  over the chart (shipped V2). Every mutation flows read `PP_DOC` → GanttOps →
  `RebuildChart` → synchronous `SetOwnSelection` only.
- **G2** One gesture / one command = exactly one undo entry.
- **G3** Rebuild-in-place preserves the CHART_ROOT frame exactly (shipped,
  `FITPERSIST`); a chart inserted fresh fills the slide content area below a
  ~15% title zone with uniform scale (shipped, `FIT OK`).
- **G4** All our windows (overlay, editors, app bar) are hidden within one tick
  (150 ms) whenever PowerPoint is not the foreground app or is minimized
  (shipped, `SCOPE`). Any NEW window must reuse `IsHostActiveForOverlayChrome`
  and its test override — never duplicate the check.
- **G5** Exceptions never escape WndProc/Tick (`_com_error`, `std::exception`,
  `...` all caught).
- **G6** Every color/dimension/type size comes from `GanttTheme.h` =
  `docs/design-tokens.md`. Hardcoded values in emitters or chrome are defects.
- **G7** `spec/fixtures/*.json` is a cross-implementation contract with the web
  TS engine. Never modified. Conformance compares abstract layout only.
- **G8** `PP_PROJ` stays a pure linear day↔point mapping (`minDay`, `pad`,
  `ptPerDay`, `originX`); new needs = new fields, never re-interpretation.

## R1 — Reserved title zone  *(shipped: f2d9796 + b7ca884)*

Top ~15% of the slide stays free for a native title placeholder. Chart fills
the rest (18pt margins), uniform scale, frame preserved across edits. No
further work in V4; regression-guarded by `FIT OK` / `FITPERSIST OK`.

## R2 — Rows are objects; the rail is a label column

**Model.** Every row is equal ("everything is a row"). A row with children
(one level, `PpRow.groupId`) reads as a *title row*; a row whose tasks carry
`labelPlacement="rail"` reads as a *task row*. No special types exist.

**Rail rendering.**
- Row names: `type.rowName`, left-padded 12pt; children indented +18pt; title
  rows with children get a collapse chevron (chevron = `ink3`).
- A task with `labelPlacement="rail"` renders NO on-bar label; instead the rail
  shows, at the task's vertical lane: an 8pt-square color dot (radius 3, the
  task's effective swatch) + the task label in `type.railTask`. Ellipsize at
  rail width − 12pt.
- Rail width `rail.width`; if any rail label would clip, grow the rail up to
  ×1.25 before ellipsizing. Charts with zero rail labels are byte-identical to
  today's output (conformance safety).

**Behavior.**
- **B2.1** Clicking a rail row (name area) selects the ROW: rail highlight per
  tokens §6 (no plot chrome). App bar switches to the Row context (R8).
- **B2.2** Clicking a rail task label selects the TASK (same selection object
  as clicking its bar): plot chrome on the bar + rail entry highlighted.
- **B2.3** Row operations (from app bar and context menu): Add above, Add
  below (same level as reference row), Move up, Move down (swap with adjacent
  row; children move with a collapsed parent is NOT required in V4 — flat swap
  of single rows is the specified behavior), Indent (child of nearest
  preceding top-level row; no-op if none or if row has children), Outdent
  (no-op if already top-level), Rename (floating card editor, label field),
  Delete (cascades: row's tasks, milestones, their anchored texts, their
  dependencies).
- **B2.4** After indent/outdent, rows re-normalize so children stay adjacent
  to their parent, preserving relative order.
- **B2.5** Hovering a rail row shows a small `+` quick-add chip (adds a task in
  that row at the visible-range center, 5 days long).
- **B2.6** Delete key deletes the selected row (same cascade as B2.3).

## R3 — Hierarchical date header (five scales)

**Structure.** The axis is TWO stacked bands (`axis.height` total; Y-scale uses
a single full-height band). Content per `doc.scale`:

| scale | top band | bottom band (separator ticks follow this) |
|---|---|---|
| `year` | years ("2026") | — (single band) |
| `quarter` | years | quarters ("Q2 2026", "Q3 2026") |
| `month` | years | months ("JUN"…) |
| `week` | months (+year on first: "JUN 2026") | week cells labeled with the Monday's day-of-month, tabular numerals |
| `day` | months | day cells labeled with day-of-month; labels auto-thin: every day if ≥13px/day @96dpi, else every 2nd, else Mondays only |

- Vertical separator ticks in the plot at every bottom-band boundary
  (`hairline`, `outline`); top-band boundaries additionally get
  `hairline.major` in `outline2`. Cap total separators at ~150: on overflow
  fall back one density level (day→week→month) and note it in a code comment.
- `gridDensity` (`auto|year|quarter|month|week|day|none`, default `auto` =
  table above) overrides the separator tier only (labels unchanged); `none`
  keeps band labels, no plot ticks. `gridStyle` (`solid|dotted`) applies to
  bottom-tier ticks.
- All separator/band shapes tagged with stable ISO ids (e.g. `2026-07`,
  `2026-W28`) so `UpdateGantt` diffs stay stable.
- **B3.1** Changing scale/density/style is a single undo entry, frame
  preserved, and re-labels the header per the table (`SetScale` op exists;
  `SetGridDensity`/`SetGridStyle` per plan S2).

## R4 — Material bars (move, resize, recolor, progress)

**Visual.** Bar = rounded rect (`bar.height`, `bar.radius`): track = swatch
pre-blended 40% on white; progress = solid swatch overlay from the left at
`percent`%; on-bar label `type.barLabel` (suppressed when `labelPlacement` =
`rail` or global rail mode); `type.pct` right-aligned when bar ≥ ~110pt.
Two tasks sharing a row offset by `bar.subLane`. Effective swatch: task.color
if set, else `swatch1`. **Milestones**: `ink` diamond `milestone.size` +
`type.msLabel` at x+13pt. **The stored per-element `color` fields must actually
drive the fill** (they are currently ignored by the emitter — known defect).

**Behavior (shipped in V2/V3, re-validated visually in V4):** body-drag moves
(vertical = row change), edge-drag resizes (ew-resize cursor ±band), drags snap
to days with ghost + date tooltip, double-click opens card editor.
- **B4.1 (new)** Swatch pick (app bar / card editor) recolors bar AND rail dot
  via `SetTaskColor` — visible immediately.
- **B4.2 (new)** `labelPlacement` cycles bar→rail per task from app bar/menu;
  a global "Labels" command sets ALL tasks to rail (and back) — one undo entry.

## R5 — Markers  *(drag shipped: d4eae41; management new)*

Today = `primary`, deadline = `deadline`, custom = `customMarker` (or explicit
color). Stem `marker.width`; pill per tokens (Today pill top, deadline pill
bottom). **New in V4:** markers are full selection citizens — select (chrome =
stem highlight), Delete key + app-bar Delete (**delete path shipped** —
app-bar Delete in the marker context, the Delete key, and `DeleteById` all
remove markers), Rename (card editor), ±1d nudge, "Add marker"
(background context) inserts a custom marker at visible-range center.

## R6 — Notes (text elements)  *(engine shipped: e4ddbaa/c1e74ff; entry points new)*

White box, `outline2` border, radius 5, `type.note`, anchored notes show an
8×1.5pt tick toward their anchor. **New in V4:** creation entry points — "Note"
on task/milestone context (anchored to selection, default offset +14/−30pt)
and "Note" on background context (free note at click/center). Behavior of
drag (anchored = offset change; free = re-home) and card editing is shipped.

## R7 — Dependencies

Elbow connectors `dep.weight` in `ink3`: out of source's right edge, half-gap
elbow, arrowhead at target's left edge. Re-route automatically on any
rebuild (they are emitted from the model — no work needed for re-route).
- **B7.1** Link mode: task selected → "Link" → crosshair over bars + hint pill
  ("Link: click a target task · Esc cancels") → click target task/milestone ⇒
  `AddDependency(from, target, finish-to-start)`, exit mode, one undo. Esc /
  background click / selection loss cancels. Self/duplicate rejected silently.
- **B7.2** "Unlink" on task context (enabled only when the task has deps)
  removes all deps touching it — one undo.

## R8 — Bottom app bar (the command surface)

**Window.** A second layered `WS_EX_NOACTIVATE` per-pixel-alpha window, docked
bottom-center of the slide-view area (8px above the bottom edge, max-width 94%
of slide view, `appbar.height`), white container `appbar.radius` with `outline`
border and soft shadow. Shows whenever the chart overlay shows (same
visibility rules, G4). Never steals focus. All px values DPI-scaled.

**Anatomy.** Horizontal groups separated by `outline` hairlines. Each group
optionally starts with an UPPERCASE 9px label. Buttons = 14px icon + 11.5px
text, hover per tokens §6. A 5-segment scale control `D W M Q Y` (active =
white chip + `primary`). Right side is ALWAYS the global group; left side
swaps with the selection:

| selection | left-side contents (in order) |
|---|---|
| none | INSERT · Row · Task · Milestone · Marker · Note |
| task | *label* · Edit · [8 swatches] · −1d · +1d · Label: bar/rail · −10% · +10% · Link · Unlink(iff deps) · Note · Delete — **plus the full Row group for the task's row** when that row renders rail labels (task row) |
| row | *name* · Rename — plus Row group: Above · Below · ↑ · ↓ · Indent · Outdent · Delete |
| milestone | *label* · Edit · −1d · +1d · Note · Delete |
| marker | *label* · Rename · −1d · +1d · Delete |
| note | Note · Edit · Re-anchor · Delete |
| (always, right) | SCALE segmented D/W/M/Q/Y · Labels (global rail toggle) · Grid (cycles density auto→month→week→none) |

- **B8.1** Every button dispatches through ONE shared command registry
  ((context, command) → GanttOps mutation) that the right-click menus also
  use. Two dispatch switches = defect.
- **B8.2** Clicking app-bar buttons never activates the window, never clears
  the on-slide selection (except commands that delete it), and every mutation
  obeys G1/G2.
- **B8.3** Right-click context menus mirror the same registry (subset per
  hit zone, incl. "Add milestone here" / "Add note here" on empty cells);
  menu items and app-bar buttons may not diverge in behavior.
- **B8.4** Disabled states render at 40% alpha and do not dispatch.
