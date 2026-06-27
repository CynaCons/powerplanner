---
name: visual-plan
description: Render the project roadmap as an interactive HTML board the user can steer from. Use when the user asks to see the plan/roadmap visually, "show me the plan", "visual plan", wants a steering board, or wants a plan view regenerated after status changes. Reads PLAN.md and emits a clickable board via the visualize show_widget tool.
---

# Visual Plan

Turn the current roadmap into one interactive board: status-colored iteration
nodes grouped by lane, click-to-inspect target + acceptance, and steer buttons
that feed instructions back via `sendPrompt`. This replaces walls of plan text
with a thing the user can look at and steer from.

## Steps

1. **Read the plan.** Parse `PLAN.md` (and `docs/` roadmaps if referenced). Pull
   the forward-looking iterations/sub-iterations. For each, derive:
   - `code` (e.g. `N2`, `S1`, `F0`), `title` (≤3 words)
   - `lane` — the phase/track it belongs to (e.g. `foundation`, `web`, `native`)
   - `status` — `done | active | next | later` (one `active`, at most one `next`
     per lane is ideal)
   - `target` — one sentence (the goal)
   - `acc` — 2–4 short acceptance bullets (how we know it's done)
2. **Render** with `mcp__visualize__show_widget` using the template below.
   Group nodes into lanes; default-select the `active` item.
3. **Keep prose out of the widget.** Any narrative goes in the response text, not
   inside the HTML (per the visualize design rules). First call
   `mcp__visualize__read_me` with `modules:["interactive"]` once per session.
4. **Offer to persist / refresh.** Ask if they want it saved as a standalone
   `roadmap.html`, and regenerate the board whenever statuses change.

## Conventions

- Statuses → tints: `done`=success, `active`=accent, `next`=warning,
  `later`=neutral/muted. Include a one-line legend.
- Lanes are horizontal grids (`repeat(N, minmax(0,1fr))`); ≤6 nodes per lane row.
- Every steer button ends with ` ↗` and calls `sendPrompt(...)`. Standard set:
  *Make this the focus*, *Tell me more*, *Reprioritize the roadmap*.
- Sentence case everywhere; two font weights (400/500); CSS variables for color
  (dark-mode safe); Tabler outline icons; no emoji.

## Template

Data lives in a `DATA` object keyed by code; `STYLE` maps status→tint+icon. The
widget renders one `<div>` grid per lane + a `#detail` panel, and a single
delegated click handler: node clicks re-select; `data-act` buttons call
`sendPrompt`. See the reference implementation that shipped this board — copy its
structure (lanes `lane-<name>`, `detail()` builder, `STYLE`/`DATA` maps, the
`document.addEventListener('click', …)` delegate) and just swap the `DATA`.

Steer button wiring (keep verbatim shape):

```js
if(b.dataset.act==='focus')  sendPrompt('Let\'s make '+it.code+' ('+it.title+') the immediate focus and start it.');
if(b.dataset.act==='more')   sendPrompt('Walk me through '+it.code+' ('+it.title+') in more detail — the approach and risks.');
if(b.dataset.act==='reorder')sendPrompt('I want to reprioritize the roadmap. Here is the order I want: ');
```

## Notes

- The board is a *view*, not the source of truth — `PLAN.md` is. After the user
  steers, update `PLAN.md`, then re-run this skill so the board reflects reality.
- If a lane has many items, prefer overview (one node per phase) + drill-in over
  cramming; the user can click a phase to expand.
