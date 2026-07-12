# On-Slide UX Action Inventory (audit 2026-07-11)

What a user can do TODAY in the native on-slide editor, per selection
context, with the gaps that drive iterations v2.5.2/v2.5.3. Source of truth
for commands: `BuildAppBar()` (GanttAppBar.h) → menus derive via
`AppBarModelToMenuItems()` (GanttCommandRegistry.h). Undo = PowerPoint native
Ctrl+Z (one undo entry per op via StartNewUndoEntry in RebuildChart).

## Per-context surface
- **None/overall**: app bar INSERT (Row/Task/Milestone/Marker/Note) + SCALE
  (D W M Q Y, Labels, Grid — None context ONLY). Right-click background =
  same as menu; right-click EMPTY CELL = "Add task/milestone/note here".
  Drag on empty cell = create task. Hover row = band wash + left-gutter "+"
  chip (currently adds a ROW — spec B2.5 says TASK). Overall chart has NO
  distinct context (grip click → native CHART_ROOT selection → None bar).
- **Task**: label, Edit, 8 swatches, −1d/+1d, Label bar/rail/both, −10%/+10%,
  Link, Unlink (enabled if deps), Note, Delete. Hotkeys Del, ←/→, Shift+←/→.
  Body drag = move (+ vertical row reassign); edge drag = resize;
  double-click = card editor. No Rename button (Edit/card only).
- **Row**: name, Rename, Above, Below, ↑, ↓, Indent, Outdent, Delete.
  Double-click label = inline rename. No drag-to-reorder.
- **Milestone**: label, Edit, −1d/+1d, Note, Delete. Drag = move date.
- **Marker**: label, Rename, −1d/+1d, Delete. Drag = move date.
- **Text/Note**: label, Edit, Re-anchor, Delete. Drag = offset/re-home.
- **Bracket**: NOT implemented at any layer (model-only).
- **Dependency lines**: not hit-testable, not selectable.

## Creation routes & the empty-chart dead end
Task: app-bar INSERT▸Task; drag empty cell; right-click cell "Add task
here"; menu Insert▸Task. **Drag + "here" routes fail SILENTLY with zero
tasks** — px/day comes from existing task shapes (`ComputeDragPxPerDay`→0),
not from PP_PROJ. Milestone: INSERT▸Milestone; cell menu (same dead end).
Row: INSERT▸Row; Above/Below; hover "+" chip. Dependency: select bar →
`Link` button/menu → click target (finish-to-start only; Esc/background
cancels; hint pill shows; **no crosshair cursor — spec B7.1 gap**; no
drag-between-bars; Unlink removes ALL edges of the bar).

## Discoverability gaps (drive v2.5.2/v2.5.3)
Hidden knowledge: drag-to-create (cross cursor only), double-click editors,
row rename dbl-click, keyboard (Del/arrows), body-drag row reassign, link
mode state, ±1d buttons both labeled "1d". Dead ends: empty-chart creation
silently no-ops; SCALE/Labels/Grid unreachable while any item selected;
brackets unreachable; task Rename hunt.

## Spec divergences (docs/onslide-experience-spec.md)
- B2.5 hover "+" should add a TASK at visible-range center (adds ROW today,
  and sits left-gutter vs mockup's right-edge adder).
- B7.1 link-mode crosshair cursor missing (GanttCursorForZone has no
  link-mode branch).
- R4 per-element color: swatch UI + ops store it; emitter honoring it
  unconfirmed (likely still ignored — NAT-14).
- R2 collapse chevron: `PpRow.collapsed` exists; NO toggle interaction.
- R5 marker delete: spec says missing — actually CLOSED (spec doc stale).
- spec/srs-native/SRS-RowAndTaskSelection.md "Progress Editing" no percent controls in TASK bar — CLOSED
  (−10%/+10% exist; spec doc stale).
- Dependencies: only FS creatable; no per-edge delete; no type picker
  (AddDependency supports 4 types in GanttOps).

## v2.6.x closing review (2026-07-12)

Evidence: `walkthrough all` passed all five cold flows with every expected
capture; the U8 gallery captured document, task, row, milestone, marker,
multi-row, dependency, settings, context-menu, and card states. The host was
200% DPI and offered no headless 100%/150% monitor override; each gallery app
bar/menu/card companion capture was a successful `PrintWindow` capture.

- **Change a date:** click a task, drag its body, read the live date pill, and press Esc to return to the document bar; the operation is direct and the final nudge trace measured 156 ms. **Rough edge / UF candidate:** the selected bar has no explicit on-object “drag to move / drag ends to resize” cue, so the action is still inferred from a thin frame and cursor.
- **Link two tasks:** select the source, use **Link**, then pick the target; the prompt explains the state and Esc exits it. **Rough edge / UF candidate:** the direct port is close to the resize end, and the link capture can show PowerPoint's native CHART_ROOT handles/overlapping chrome, which competes with the custom selection model.
- **Add a milestone:** choose the visible **Milestone** insert, drag the newly selected diamond to a snapped date, then Esc to leave the context. **Rough edge / UF candidate:** the creation hint teaches Alt+double-click, but the default bar does not make the alternative creation routes equally obvious once the chart is dense.
- **Delete three rows:** Ctrl+click accumulates rows, the compact “2 selected” bar exposes Delete, and one Delete removes the selected rows. **Rough edge / UF candidate:** the pale row washes and small count are easy to miss, and the capture gives no advance summary of the tasks that a cascading row delete will remove.
- **Rename a task:** select it, choose the explicit **Rename** command, type inline, and press Enter; Esc cancels/deselects. **Rough edge / UF candidate:** double-click still opens the properties card while Rename opens inline editing, so two natural entry points still mean different things.

Additional gallery finding / UF candidate: the dependency bar reads
`Discovery ? Wireframes` rather than using a proper arrow glyph on this
code-page build (matching the existing C4566 warning). Fix the string encoding
before treating dependency selection as presentation-grade.
