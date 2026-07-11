# SRS - Native On-Slide Creation & Editing Flows

Native-specific requirements for creating and editing tasks, milestones, rows,
and notes in the PowerPoint on-slide Gantt editor. Creation routes must be
discoverable, reliable on empty charts, and consistent across app bar,
right-click, hover, drag, double-click, and keyboard entry points.

This file migrates the legacy prose creation and editing flow requirements for
v2.6.8.

Traces up to: `../srs/SRS-editing.md`, `../interaction.md`,
`docs/onslide-experience-spec.md`, `docs/design-tokens.md`,
`spec/srs-native/SRS-OverlayLifecycle.md`,
`spec/srs-native/SRS-RowAndTaskSelection.md`,
`spec/srs-native/SRS-InteractionConventions.md`, and
`native/tools/harness_driver.py`.

Reference impl: `native/PowerPlannerAddin/` (Overlay.cpp, GanttHitTest.*,
GanttAppBar.h, GanttBuilder.cpp, GanttScene*, command registry and PP_DOC /
PP_PROJ metadata).

## Reliability On All Chart States

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-CRE-01 | Day-to-pixel projection for creation gestures shall be derived from chart projection metadata (`PP_PROJ` minDay, pad, ptPerDay, originX, and axis geometry) rather than existing task shapes. | Creation must work on empty charts and rows-only charts. | `create-task-empty-chart` exercises drag, menu, and double-click creation after deleting tasks; asserts taskCount increases. | Legacy SR-CRE-01; PP_PROJ; BuildProjectedScene |
| SR-CRE-02 | A non-executable creation action shall either be disabled or provide visible feedback instead of silently doing nothing. | Silent no-ops make creation feel broken and undiscoverable. | Disabled-state/menu assertions; screenshot review for hint pill or grayed action at 40% alpha. | Legacy SR-CRE-02; command registry; menus/appbar |
| SR-CRE-03 | After successful creation, the new element shall become the own-selection with context chrome visible on the next paint and existing labels and content still visible. | Mutation feedback confirms success and keeps users oriented. | Creation traces assert ownSelKind/ownSelId, appBarGroups, rowLabelCount, and no-flash invariant. | Legacy SR-CRE-03; SRS-OverlayLifecycle SR-LIFE-13 |

## Discoverability

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-CRE-04 | The per-row hover quick-add affordance shall add a task in that row rather than inserting a row. | Spec B2.5 defines the row hover plus as a task quick-add, while row insertion has separate affordances. | `hover-quick-add-task` asserts taskCount +1 and rowCount stable. | Legacy SR-CRE-04; onslide-experience-spec B2.5 |
| SR-CRE-05 | Hovering an empty timeline cell for approximately 600 ms shall show a transient token-styled hint pill explaining drag-to-create and right-click alternatives. | Users need to discover timeline creation without documentation. | Hover-hold screenshot shows the hint pill no more than once per hover/selection session and never during drags, menus, or editors. | Legacy SR-CRE-05; creation hint pill |
| SR-CRE-06 | Double-clicking an empty timeline cell shall create a task at that row and date with the default span. | Double-click creation is a familiar, low-friction path for empty areas. | `create-task-empty-chart` double-click stage asserts taskCount +1 and new task date/row. | Legacy SR-CRE-06; GanttHitTest empty cell |
| SR-CRE-07 | Milestone creation shall be available from app bar insert and empty-cell right-click, with a cell-created milestone landing on that row and date. | Milestones are first-class Gantt elements and must not depend on existing task bars. | `create-milestone-cell` asserts milestone count and placement on zero-task charts. | Legacy SR-CRE-07; app bar and context menu creation |
| SR-CRE-08 | Every context menu shall be complete for its target zone and mirror the shared app bar command registry as the single source of truth. | Context menus and app bar commands must not drift. | Command registry matrix compares menu/appbar actions per hit zone and selection context. | Legacy SR-CRE-08; GanttAppBar model; GanttHitTest menus |

## Editing Affordance Completeness

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SR-EDT-01 | The task context shall expose Rename directly as a button or menu item that maps to the inline/card label editor path. | Task rename must have parity with row rename and not rely only on double-click. | `task-rename-route` asserts command availability and editor opening. | Legacy SR-EDT-01; SRS-InteractionConventions SR-IXC-16 |
| SR-EDT-02 | The global scale, labels, and grid controls shall remain reachable while an item is selected, with current selection preserved when scale changes. | Scale changes are chart-level operations users still need in item context. | `scale-while-task-selected` asserts scale group reachability and ownSel stability after scale change. | Legacy SR-EDT-02; appbar_matrix |
| SR-EDT-03 | Day nudge controls, when present, shall use visually directional minus/plus glyphs rather than two identical `1d` labels. | Directional commands must communicate whether they move earlier or later. | App bar/menu screenshot review and command label assertions. | Legacy SR-EDT-03; app bar command rendering |

## Open / Related

- Brackets remain out of scope for this SRS.
- Collapse chevron behavior is tracked separately in the plan.
