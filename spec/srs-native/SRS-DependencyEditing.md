# SRS - Native Dependency Creation & Editing

Port-based linking (UF-11, v2.6.5) supersedes click-click link mode as the primary creation gesture — requirements below to be extended in v2.6.5

Native-specific requirements for creating, selecting, editing, and deleting
dependencies between tasks and milestones in the PowerPoint on-slide Gantt
editor. The data model supports dependency edges, and the native surface must
make links visible, guided, reversible, and individually editable.

This file migrates the legacy prose dependency editing requirements for
v2.6.8.

Traces up to: `../srs/SRS-editing.md`, `../interaction.md`,
`docs/onslide-experience-spec.md`, `docs/design-tokens.md`,
`spec/srs-native/SRS-CreationFlows.md`,
`spec/srs-native/SRS-InteractionConventions.md`, and
`native/tools/harness_driver.py`.

Reference impl: `native/PowerPlannerAddin/` (Overlay.cpp, GanttHitTest.*,
GanttAppBar.h, GanttBuilder.cpp, GanttScene dependency routing, PP_DOC
dependency model).

## Link Mode Completion

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-DEP-01 | While legacy link mode is active, the cursor over any valid target bar or milestone shall be a crosshair, with invalid targets using the standard cursor while a hint pill explains the mode. | Users need feedback about where a click can create a valid dependency. | `link-mode-click-target` cursor seam assertions and screenshot review. | Legacy SR-DEP-01; onslide-experience-spec B7.1 |
| SR-DEP-02 | During link mode, the source and candidate target shall be visually distinguished with source selection chrome and a subtle token-styled target highlight. | Linking requires clear source/target roles before commit. | Link-mode screenshot matrix and token review. | Legacy SR-DEP-02; Overlay link-mode paint |
| SR-DEP-03 | Completing a link in legacy link mode shall create exactly one finish-to-start dependency, reject duplicate or self-links with a brief hint pill, exit link mode, and re-select the source. | Link creation must be deterministic and visibly reject invalid operations. | `link-mode-click-target` and `link-duplicate-rejected` assert depCount changes, hint state, mode exit, and source reselection. | Legacy SR-DEP-03; AddDependency path |

## Drag And Port-Based Linking

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-DEP-04 | Hovering a selected task or milestone shall reveal a token-styled link handle or port that previews and creates the dependency when dragged to another bar and released. | Direct port-based linking is the primary creation gesture for dependencies. | `link-drag-handle` or port-link trace asserts rubber-band preview, valid-target cursor, depCount +1, and Esc/background cancel behavior. | Legacy SR-DEP-04; UF-11; SRS-InteractionConventions SR-IXC-13 |
| SR-DEP-05 | The link handle or port hit zone shall avoid interference with resize edge zones and use documented priority in hit testing. | Link creation and date resizing must both remain usable on task edges. | Hit-test matrix for resize edge versus link port; code review of GanttHitTest priority order. | Legacy SR-DEP-05; GanttHitTest |

## Dependency Lines As First-Class Selections

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-DEP-06 | Dependency connectors shall be hit-testable with click selection of the individual edge, line highlighting, and a minimal dependency app bar context with label and delete action. | Users must be able to inspect and operate on one link without selecting a task. | `dep-select-delete` first stage asserts ownSelKind=DEP, selected edge id, highlight, and appBarGroups. | Legacy SR-DEP-06; dependency hit zone |
| SR-DEP-07 | Delete on a selected dependency shall remove only that edge, with bar-level Unlink retaining and communicating remove-all scope. | Edge-level delete prevents destructive removal of unrelated links. | `dep-select-delete` asserts only selected edge removed and other dependencies remain. | Legacy SR-DEP-07; dependency delete command |
| SR-DEP-08 | Pressing Esc or clicking background while a dependency is selected shall clear the dependency selection without other side effects. | Dependency selection needs the same escape semantics as other item selections. | `link-esc-cancel` and dependency selection reset traces; ownSel clears and depCount remains unchanged. | Legacy SR-DEP-08; SRS-InteractionConventions SR-IXC-09 |

## Open / Related

- Dependency type picker and per-edge re-anchor remain future work.
- Auto-scheduling on dependency violation is out of scope for this SRS.
