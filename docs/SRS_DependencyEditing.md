# SRS - Dependency Creation & Editing (ASPICE-style)

## 1. Feature Overview / Purpose
Dependencies (links between tasks/milestones) exist in the model with four
types and render as elbow connectors, but on-slide authoring is minimal:
link mode is button-only with no cursor feedback (spec B7.1 crosshair gap),
there is no drag-between-bars affordance, dependency lines are not
selectable, and Unlink removes ALL edges touching a bar. This SRS specifies
a complete, discoverable dependency workflow.

## 2. User Goals & Interactions
- The user selects a bar and can SEE how to link it (visible affordance),
  then clicks/drags to a target to create the link.
- During link mode the cursor over a valid target is a crosshair and a hint
  pill explains the mode; Esc/background click cancels.
- The user can select an individual dependency line, see it highlighted,
  and delete just that edge.
- Invalid targets (self, duplicate edge) are rejected with visible feedback,
  not silently.

## 3. Software Requirements (Functional)

### Link mode completion (spec B7.1)
- **SR-DEP-01**: While link mode is active, the cursor over any valid target
  bar/milestone shall be the crosshair; over invalid targets (source itself,
  background) the standard cursor + the hint pill remains (per
  docs/design-tokens.md §6 and mockup `.plot.linking`).
- **SR-DEP-02**: Source and candidate-target shall be visually
  distinguished during link mode (source keeps selection chrome; hovered
  valid target gets a subtle highlight ring) using GanttTheme.h tokens.
- **SR-DEP-03**: Completing a link creates exactly one `finish-to-start`
  dependency, rejects duplicates (same from/to/type) and self-links with a
  brief hint pill ("Already linked" / "Cannot link to itself"), exits link
  mode, and re-selects the source.

### Drag-to-link affordance
- **SR-DEP-04**: Hovering a selected task/milestone shall reveal a small
  link handle at the bar's right end (token-styled dot/anchor). Dragging
  from the handle to another bar shall create the dependency on release
  (rubber-band elbow/straight preview line while dragging, crosshair over
  valid targets, Esc cancels). Dropping on background cancels silently.
- **SR-DEP-05**: The link handle shall not interfere with resize edge zones
  (handle zone sits outside/above the right edge grip; hit priority
  documented in GanttHitTest).

### Dependency lines as first-class citizens
- **SR-DEP-06**: Dependency connectors shall be hit-testable (new HtZone;
  tolerance a few px around the polyline). Clicking one selects it
  (ownSelKind=DEP, id=from:to:type), shows a highlight along the line, and
  populates a minimal app bar context: label "Link A → B", `Delete`
  (danger). Right-click on a line offers "Remove this link".
- **SR-DEP-07**: `Delete` (key, button, or menu) on a selected dependency
  removes ONLY that edge. `Unlink` on a bar keeps its current remove-all
  semantics but its label shall communicate scope ("Unlink all").
- **SR-DEP-08**: Selecting a dependency and pressing Esc / clicking
  background clears the selection without other side effects.

## 4. Verification Approach (E2E with Native Seams)
- Extend DumpChromeStateForTest with linkMode flag, depCount, and (when
  ownSelKind=DEP) the selected edge id.
- Scenarios/traces: `link-mode-click-target` (depCount +1, mode exits,
  source re-selected), `link-duplicate-rejected` (depCount stable, pill
  shown), `link-drag-handle` (posted WM_MOUSE drag from handle to target;
  depCount +1), `dep-select-delete` (select line → Delete → only that edge
  removed; others intact), `link-esc-cancel`.
- Cursor assertions via the cursor-override seam (crosshair over target in
  link mode — SR-DEP-01).
- Pixel checks: rubber-band preview visible in mid-drag capture; no-flash
  invariants on all dep mutations.

## 5. Non-Functional / Constraints
- Only `finish-to-start` is creatable on-slide for now (other 3 types stay
  JSON/model-level; type picker is future work — keep `AddDependency`'s
  4-type capability intact).
- One undo entry per created/removed link.
- Shared registry for new commands; tokens/DPI/ground rules per
  docs/onslide-v4-plan.md §1.
- Hit priority: item zones > dep lines > row bands > background.

## 6. Open / Related
- Dependency type picker + per-edge re-anchor (future).
- Auto-scheduling on dependency violation (WEB-06/07 parity) — out of scope.
- Related: SRS_CreationFlows.md, docs/onslide-experience-spec.md B7.
