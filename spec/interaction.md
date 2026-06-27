# Interaction Model

How a user creates and edits a chart. The web app implements the full model
today; the PowerPoint add-in reaches it in stages (its on-slide model is the
think-cell-style end goal). This spec defines the shared *intent*; each surface
implements what its medium allows.

## Core operations (medium-independent)

| Operation        | Meaning                                                        |
|------------------|----------------------------------------------------------------|
| Select           | Pick one element; multi-select extends the set.                |
| Move task        | Shift `start`/`end` together by a whole number of days.        |
| Resize task      | Change `start` or `end` independently (`end >= start`).        |
| Move milestone   | Change its `date`.                                             |
| Create task/milestone | Add at a target row + date.                              |
| Connect          | Drag from one task to another to create a Dependency.          |
| Bracket          | Create a bracket spanning the selected rows/dates.             |
| Edit fields      | Label, color, % complete, notes, dates via an inspector form.  |
| Delete           | Remove the selected element(s).                                |

All edits operate on the **document**, then re-run layout — never on device
coordinates directly. Snapping is to whole days (the abstract x unit).

## Web surface (`src/`) — implemented

Direct manipulation on the SVG canvas: drag bars/edges, drag-to-connect,
marquee select, context menus, inline label editing, an inspector panel, a
command palette, and keyboard shortcuts. This is the reference for *what* each
operation does.

## PowerPoint surface (`native/`) — staged toward on-slide editing

The end goal is think-cell-style: the chart is native shapes on the slide, and
editing happens **contextually on the slide**, not in a task pane.

- **N2–N3 (now):** one-shot emit (ribbon → native shapes) and round-trip
  (read a chart back from the slide via the `PP_DOC` tag on the group).
- **N4:** an on-slide overlay — a layered window aligned to the slide-edit pane —
  draws selection handles and contextual controls over the chart shapes, hooking
  PowerPoint's `WindowSelectionChange` and mouse events.
- **N5:** "agents" watch shape edits and reflow the chart (move a bar on the
  slide → the model updates → dependent layout re-emits), keeping shapes and
  data in sync.

## Identity & round-trip contract

For an implementation to support editing, emitted output must be re-readable:

- Each element keeps its document `id`.
- PowerPoint additionally tags every shape (`PP_KIND`, `PP_ID`) and stores the
  serialized document on the chart group (`PP_DOC`), so "pull from slide"
  reconstructs the exact `GanttDocument`. The serialized form is the same JSON
  validated by `schema/document.schema.json`.

## Undo / history

Edits are document mutations, so each surface integrates with its host's history
(web: in-app undo stack; PowerPoint: native undo where the object model allows).
