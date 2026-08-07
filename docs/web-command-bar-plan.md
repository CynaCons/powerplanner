# Web Command Bar — Design Plan

Replace the permanent left inspector column with a single **contextual command
bar**, matching the model the native add-in already specifies in
[`onslide-experience-spec.md`](onslide-experience-spec.md) §R8.

Status: **design — awaiting sign-off**
Plan: `PLAN.md` → Phase 16 / v2.13.x
Scope: web only (`src/app/`, `src/styles/`). No `native/`, no `src/taskpane/`.

---

## 1. Why

The user's report was "I don't really like the panel on the left even though it
is collapsible". Investigating it turned up something more specific than a taste
objection: **the panel is largely duplicate chrome.**

| Control | Rendered in | Simultaneously visible? |
|---|---|---|
| Scale `D W M Q Y` | `Inspector.tsx`, `ToolPalette.tsx`, `CommandPalette.tsx` | **Yes** — inspector + tool palette both on screen |
| Fit to data | `Header.tsx`, `ToolPalette.tsx` | **Yes** |
| Task / milestone counts | `Inspector.tsx` (Statistics), `StatusBar.tsx` | **Yes** |

Two identical segmented scale controls are painted at the same time, both wired
to `setScale`. The 320px column was charging rent for a duplicate.

The deeper diagnosis: **the inspector is a contextual property editor that was
given a permanent column.** `SelectionInspector` — the genuinely useful part —
renders only when something is selected. With an empty selection the column had
nothing to say, so it was padded with document settings and read-only stats that
already live in other chrome. The filler is the defect, not the panel.

Collapsing it (v2.12.1) treated the symptom. This removes the cause.

## 2. The model

One bar, docked bottom, selection-driven. Directly mirrors native R8:

> left side swaps with the selection; right side is ALWAYS the global group

```
nothing selected
┌──────────────────────────────────────────────────────────────────────┐
│ INSERT  Row · Task · Milestone · Marker   │  D W M Q Y · − 2d/px + · Fit │
└──────────────────────────────────────────────────────────────────────┘

task selected
┌──────────────────────────────────────────────────────────────────────┐
│ "Rev A bring-up"  Edit · ▪▪▪▪▪▪▪▪ · −1d +1d · −10% +10% · Link · Delete │ D W M Q Y … │
└──────────────────────────────────────────────────────────────────────┘
```

Anatomy: horizontal groups separated by hairlines, each optionally led by a
9px uppercase label. The right group never moves, so the scale control is always
in the same place regardless of what is selected — the thing that makes a
contextual bar learnable rather than jumpy.

## 3. Where everything goes

| Today | Tomorrow | Why |
|---|---|---|
| Inspector → Statistics | **Deleted.** Counts already in status bar; add span + avg complete there | Read-only telemetry is status-bar work |
| Inspector → Document → Scale | **Deleted.** The tool palette's copy becomes the only one | Was a duplicate |
| Inspector → Document → Fiscal year start | File menu → *Document settings…* | Set once per document |
| Inspector → Appearance → Theme | File menu → *Document settings…* | Set once per document |
| Inspector → SelectionInspector | **Command bar, left group** | This was always the real content |
| Tool palette | **Becomes the command bar** — tools stay in the left group when nothing is selected | Same dock, richer job |
| Header → Fit | **Deleted.** Bar keeps the canonical one | Was a duplicate |
| Header → insert actions | Move to the bar's INSERT group | Insert is a canvas action, not a document action |
| Status bar | Gains span + avg complete | Absorbs Statistics |

Net: four chrome bands → three (header, bar, status). Chart goes from 580px to
the full window width at 900px — **+55%**, which is also the largest remaining
lever on the legibility problem from v2.12.0.

## 4. Per-selection contents

Derived from what `SelectionInspector` already edits, so no capability is lost.

| Selection | Left group |
|---|---|
| none | INSERT · Row · Task · Milestone · Marker · Bracket |
| task | *label* · Edit · 8 colour swatches · −1d +1d · −10% +10% · Link · Unlink (iff deps) · Delete |
| milestone | *label* · Edit · −1d +1d · Delete |
| bracket | *label* · Edit · Delete |
| marker | *label* · Rename · −1d +1d · Delete |
| multi | *n selected* · colour swatches · −1d +1d · Delete |
| (always, right) | SCALE `D W M Q Y` · zoom −/+ with px/day readout · Fit |

**Overflow rule.** Fields that do not reduce to a button — exact start/end date
pickers, percent slider, notes — open in a small popover anchored to the bar's
*Edit* button. The bar holds the frequent verbs; the popover holds precision.
This is what stops the bar becoming a horizontal re-run of the panel.

## 5. What could go wrong

- **Bar height on narrow windows.** Groups wrap or the left group scrolls
  horizontally; the right group is pinned and never wraps.
- **The compact/task-pane layout** (`app-shell--compact`) already hides the
  inspector, so it inherits this for free — but it must be re-checked, not
  assumed.
- **Regression risk to editing.** Everything `SelectionInspector` can do must
  remain reachable, via bar or popover. Gate: an e2e test that edits label,
  dates, colour, percent and notes through the new surface.
- **Losing the notes field.** It is the least button-shaped control; it belongs
  in the popover, not dropped.

## 6. Migration order

Each step leaves the app working.

1. **B0** — status bar absorbs Statistics (span, avg complete). Delete the
   Statistics section.
2. **B1** — File menu gains *Document settings…* (theme, fiscal year start).
   Delete the Appearance and Document sections. The inspector is now
   selection-only.
3. **B2** — tool palette becomes the command bar shell: groups, hairlines,
   pinned right group. Behaviour unchanged.
4. **B3** — port `SelectionInspector` contents into the bar's left group plus
   the Edit popover.
5. **B4** — delete the inspector column and its collapse machinery from
   v2.12.1. Chart goes full width.
6. **B5** — remove the duplicate Fit from the header; move insert actions into
   the INSERT group.

Steps 1–2 are worth doing regardless of what happens to the column, which is why
they come first.

## 7. Open questions

1. Should the bar **float over** the chart (native docks it 8px above the slide
   edge, over content) or **occupy its own grid row** as the tool palette does
   today? Floating matches native and gives the chart more height; docking is
   simpler and never occludes a bar at the bottom of a long chart.
   Proposal: own grid row for v1, revisit.
2. Keep the **tools** (select / marquee / pan) in the bar's empty-selection
   state, or move them to a persistent mini-group so they are reachable while a
   task is selected? Proposal: persistent left-most micro-group, since Pan and
   Select are needed *during* a selection.
3. Does the **header** still earn 48px once insert actions leave it? It would
   hold title, undo/redo, agent toggle, File. Possibly merges into the bar.
