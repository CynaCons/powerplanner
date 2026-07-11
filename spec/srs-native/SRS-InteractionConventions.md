# SRS — Native Interaction Conventions (Direct Manipulation, Live Preview, Semantics, Affordances)

Native-specific requirements for the PowerPoint COM add-in on-slide Gantt editor implementing a think-cell-style interaction model. Feature tag: IXC (interaction conventions).

Traces up to: `AGENTS.md` (UX walkthrough gate + interaction conventions checklist), `PLAN.md` (Phase 11 slices v2.6.0–v2.6.7, B1/B2, UF-10/UF-11/UF-12, M1/M6, N4/N5), `../srs/SRS-editing.md`, `../interaction.md`, `docs/onslide-ux-inventory.md`, `native/PowerPlannerAddin/Overlay.cpp`, `GanttHitTest.*`, app bar and editor state machines.

Reference impl: `native/PowerPlannerAddin/` (drag gesture handling in Overlay, live preview paint, commit paths for inline/card/drag, affordance hit zones, keyboard routing, custom cursor/appbar updates).

## Direct Manipulation First

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-IXC-01 | Continuous scalar values (task start and end dates, progress percentage, element spatial positions and row order) shall be edited exclusively by direct manipulation: the user drags the object or its dedicated edge/handle; stepper buttons, +/- incrementers, or similar discrete controls for continuous values are forbidden. | Direct drag is the expected, efficient model for Gantt charts (industry standard); steppers were explicitly called out as anti-UX in feedback. | Cold UX walkthrough gate (AGENTS.md) for "change a date", "set progress", "reposition task" executed as first-time user with harness; progress-drag and drag-date-pill trace profiles. | v2.6.2, UF-12 |
| SR-IXC-02 | Dragging a task bar (body or ends) shall be the sole primary mechanism to adjust its date range; any numeric date fields in secondary editors (cards) are for precise entry only and do not replace the drag verb. | Prevents mode confusion and enforces the "drag first" contract. | UX walkthrough + trace "task-move-commit"; review that no persistent +/- controls exist for dates on bar. | v2.6.2, UF-12 |

## Live Preview

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-IXC-03 | While dragging a task bar to change its dates, a floating date pill displaying the live start and end dates (in the current scale format) shall be rendered and updated continuously under the pointer or near the bar. | User must see the exact numeric outcome of the gesture before committing; no "surprise" on release. | drag-date-pill trace profile (pre/immed/+1 frames show updating pill); UX walkthrough gate with date change goal. | v2.6.2 |
| SR-IXC-04 | While dragging a milestone or date marker, a date pill showing the target date shall follow the drag live. | Same live feedback principle for point dates. | marker-snap or equivalent trace; walkthrough "move milestone". | v2.6.2, v2.6.5 |
| SR-IXC-05 | During a drag-create gesture, the preview shall depict the actual final visual shape of the element being created (correct bar height, corner style, fill pattern, progress if applicable) rather than a placeholder rectangle or wireframe. | The preview must be predictive of the committed result to avoid post-create surprise edits. | create-preview-shape trace profile; cold UX walkthrough of creation flows. | v2.6.5, N2 |
| SR-IXC-06 | While dragging the progress fill edge on a task bar, a numeric percentage readout (e.g. "42%") shall be displayed adjacent to the handle and update live with the drag position. | Continuous feedback for the % value being set. | progress-drag trace + walkthrough "adjust progress". | v2.6.2, UF-12 |

## Commit / Cancel Semantics (Uniform Across Editors)

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-IXC-07 | Commit and cancel semantics shall be identical for all editor surfaces (inline label editor, card/panel property editor, drag gesture preview): click-away from the editor or pressing Enter commits the pending change; pressing Esc cancels and restores prior state. | Inconsistent commit behavior (B2) between inline and card was a source of data loss and user frustration. | UX walkthrough gate covering inline rename, card edit, drag commit/cancel for multiple element types; harness editor state traces. | B2, v2.6.2 |
| SR-IXC-08 | No editor shall silently discard or auto-commit a pending edit solely because the host window lost focus (e.g., WA_INACTIVE, click in Notes pane, or PowerPoint ribbon); explicit user gesture (click-away inside surface, Enter, or Esc) is required. | Silent discard on focus loss violates the uniform semantics and causes data loss (B2). | B1/B2 hotkey+focus scenarios in harness; walkthrough with pane switches; state assertions that pending edit survives external focus change until explicit action. | B2, v2.6.1 |

## Context Reset

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-IXC-09 | Pressing Esc when an item is selected, or explicitly deselecting (clicking background or CHART_ROOT), shall cause the app bar to revert to the default/component context and cursors/handles to the unselected state; no contextual mode shall remain sticky after the reset action. | Users expect Esc and deselect to be reliable "escape hatches" that return the entire surface (bar + overlay) to a known neutral state. | overlay_lifecycle and selection-change traces; UX walkthrough "deselect returns to insert/scale context". | v2.6.1, v2.6.3, M6 |
| SR-IXC-10 | After any drag commit, Esc press, or selection change that clears item selection, the cursor shall immediately reflect the new default or component-level affordances; transient drag cursors shall not persist. | Prevents "stuck in move mode" or invisible affordance states. | Trace invariants on cursor state post-op; walkthrough cold paths. | v2.6.1, v2.6.3 |

## Constraint + Snap

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-IXC-11 | All drag gestures (move, resize, progress, create, link) shall be clamped so that the pointer and resulting edit cannot leave the bounds of the active CHART_ROOT component rectangle. | Edits outside the chart area are meaningless and would corrupt layout or produce off-slide artifacts. | Harness drag boundary tests + component-rect invariants in traces; UX walkthrough that drags stop at edges. | v2.6.2 |
| SR-IXC-12 | Date-valued drag operations shall snap the computed date value to the smallest time unit currently visible on the scale (e.g., whole day when daily labels shown); snap shall be active both during live preview and at commit. | Provides predictable, grid-aligned results matching the visual scale without requiring modifier keys for the common case. | marker-snap, drag-date-pill traces asserting snapped values; walkthrough "drag snaps to days". | v2.6.2, v2.6.7 |

## Visible Affordances

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-IXC-13 | On hover and/or selection of a task bar, visible affordances shall appear for all primary gestures supported on that element: left/right link ports, progress edge drag handle (when % is editable), move grip, resize end handles. | Affordances must be self-evident; users discover operations by looking, not reading docs. | Affordance visibility matrix in harness (or visual trace); UX walkthrough gate checking discoverability checklist items 1 and 5. | v2.6.5, UF-11, N5 |
| SR-IXC-14 | Row-adder chips (or equivalent "+ New row" affordances) shall be visibly present on hover of row areas or the row header region; clicking or dragging them shall perform row insertion without requiring menu or ribbon action. | Row creation must be directly manipulable and obvious. | row-adder-boundaries trace/scenario; walkthrough "add a row via chip". | v2.6.5 |
| SR-IXC-15 | The move grip (or primary drag affordance) on selected elements shall be visually distinct and labeled or iconically obvious; Alt+click or grip escape hatches shall not be the sole discovery path. | Addresses undiscoverable Alt+click (M6). | M6-specific trace + walkthrough; visual review of grip vs. other handles. | v2.6.2, M6 |

## Consistent Verbs

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-IXC-16 | The command label "Rename" (or direct equivalent) shall, in every context and for every element type, initiate an inline in-place text edit on the primary label of the target; it shall never open a card or perform any other action. | Eliminates the three different behaviors observed for "Rename" (M1). | Verb-consistency matrix test (or harness dump of command registry per selKind); UX walkthrough "use Rename on row then task then milestone". | M1, v2.6.2 |
| SR-IXC-17 | The command label "Edit…" (or "Edit") shall, in every context, open the full properties card/panel for the selected element; it shall never perform an inline rename. | Consistent verb semantics across the surface. | Same matrix + walkthrough as above. | M1, v2.6.2 |

## Platform Conventions and Input Discipline

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-IXC-18 | Multi-selection shall obey standard Office / Windows platform conventions: Ctrl+click toggles membership of the clicked item; Shift+click selects the contiguous range from the anchor to the clicked item. | Users expect familiar multi-select gestures; custom rules increase cognitive load. | multi-select harness scenario exercising Ctrl/Shift combinations; UX walkthrough "select 3 tasks with platform keys". | v2.6.4 |
| SR-IXC-19 | The Delete (Del) key shall delete the current overlay selection when the chart component has input focus; Del and other editing keys shall have no effect on the chart when focus is in another pane or window. | Prevents accidental data loss from global hotkeys (B1) and respects host focus model. | hotkey-scope scenario (type in Notes with selection → no theft, Del only acts when overlay owns); walkthrough. | B1, v2.6.1 |
| SR-IXC-20 | F2 and double-click on a selected element (or its label) shall start an edit action on the primary label (inline where supported, else card); right-click shall open a context-sensitive menu whose actions match the current selection/hit context. | Standard platform editing and context gestures. | Keyboard + dblclick traces; right-click menu trace; walkthrough using F2, double-click, right-click. | v2.6.1, N4 |
| SR-IXC-21 | Overlay-registered keyboard accelerators and hotkeys shall be scoped: they shall only be active and consume input while the overlay reports an active selection and the slide view's input focus is directed at the chart component; input directed elsewhere (other task panes, other windows, ribbon) shall pass through to PowerPoint. | Stops global hotkey theft (B1) and "steals input directed at other windows/panes". | B1 hotkey-scope + focus-loss scenarios in harness + manual Office verification; walkthrough with mixed focus. | B1, v2.6.1 |

## Cross-Cutting Verification

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-IXC-22 | Every interaction convention in this document shall be covered by at least one harness scenario or trace profile exercising the live gesture and by execution of the cold UX walkthrough gate defined in AGENTS.md (task-based, first-time-user gestures via harness without seams, PNG/GIF capture, review against the discoverability / direct-manip / consistent-verbs / no-traps / constrained / platform checklist). | Trace invariants alone prove correctness of state; only the walkthrough proves usability and adherence to the nine conventions. | Explicit gate checklist item per slice in PLAN + harness run logs + walkthrough artifacts reviewed before slice close. | v2.6.0 (gate), all v2.6.x slices |

## Open / Related

- Implementation of the conventions is staged across v2.6.1 (selection & hotkeys), v2.6.2 (direct manip + B2 commit), v2.6.4 (multi), v2.6.5 (linking affordances), v2.6.7 (scale snap).
- Ties to theme coherence (SR-THEME): all menus, pills, and editors that participate in these interactions must be custom-drawn.
- Future: once conventions are stable, fold relevant items from docs/SRS_*.md into this or sibling tables and archive prose.
- M3 (selection visibility to PP) and N4 (keyboard path) have partial coverage here; full enforcement may require additional entries.
