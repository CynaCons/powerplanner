import { useContextMenuStore } from '../stores/contextMenuStore';
import { useDocumentStore } from '../stores/documentStore';
import { useSelectionStore } from '../stores/selectionStore';
import { useEditStore } from '../stores/editStore';
import { useViewStore } from '../stores/viewStore';
import { useViewportStore } from '../stores/viewportStore';
import { ContextMenu, type ContextMenuItem } from './ContextMenu';
import { LabelPositionPicker } from './LabelPositionPicker';
import { Pencil, Copy, Trash2, Plus, Diamond, Brackets as BracketIcon, Flag, GitBranch, SlidersHorizontal, ArrowUp, ArrowDown, FolderPlus, FolderInput, Rows3 } from 'lucide-react';
import { newId } from '../utils/ids';
import { addDays, todayISO } from '../utils/dates';
import type { DependencyType, LabelPlacement } from '../types/document';

const DEP_TYPES: { value: DependencyType; label: string }[] = [
  { value: 'finish-to-start', label: 'Finish → Start (default)' },
  { value: 'start-to-start', label: 'Start → Start' },
  { value: 'finish-to-finish', label: 'Finish → Finish' },
  { value: 'start-to-finish', label: 'Start → Finish' },
];


/**
 * Where a label sits relative to its own marker.
 *
 * The placements existed in the model and the renderer from the start, but
 * nothing could set them: only an agent could, which left a person with no way
 * to resolve an overlap the allocator could not (owner report 2026-10-23).
 * `undefined` means automatic — the allocator decides and routes around every
 * explicit choice.
 */
/**
 * Where a label sits relative to its own marker.
 *
 * The placements existed in the model and the renderer from the start, but
 * nothing could set them: only an agent could (owner report 2026-10-23).
 * `undefined` means automatic — the allocator decides and routes around every
 * explicit choice.
 */
function labelPositionItem(
  current: LabelPlacement | undefined,
  onPick: (p: LabelPlacement | undefined) => void,
  kind: 'task' | 'milestone',
): ContextMenuItem {
  return {
    id: 'label-position',
    custom: <LabelPositionPicker current={current} onPick={onPick} kind={kind} />,
  };
}

export function ChartContextMenu() {
  const { open, x, y, target, close } = useContextMenuStore();
  const doc = useDocumentStore((s) => s.doc);
  const docActions = useDocumentStore();
  const setSelection = useSelectionStore((s) => s.setSelection);
  const openDetailsFor = useViewStore((s) => s.openDetails);

  /**
   * Select the thing, then open its details. Selecting first is what makes the
   * dialog know what it is editing, and it leaves the quick actions in the bar
   * pointing at the same element the user just right-clicked.
   */
  const openDetails = (id: string, kind: 'task' | 'milestone' | 'bracket' | 'row') => {
    setSelection([{ kind, id }]);
    openDetailsFor({ kind, id });
  };
  const beginEdit = useEditStore((s) => s.beginEdit);
  // A row inserted off-screen is a row the planner cannot see they made
  // (SRS-EDIT-028).
  const reveal = useViewportStore((s) => s.reveal);

  if (!open || !target) return null;

  const items: ContextMenuItem[] = [];

  if (target.kind === 'background') {
    const date = target.date ?? todayISO();
    const clickedRow = target.rowId ?? doc.rows[0]?.id;

    /**
     * The row the insert lands on, created if there is none (SRS-EDIT-061).
     *
     * Both inserts used to read the clicked row and `return` when it was
     * undefined, which is exactly the state an empty roadmap is in — so the
     * first gesture a first-time user makes on an empty chart was a menu item
     * that did nothing and said nothing.
     */
    const rowForInsert = (): string => {
      if (clickedRow) return clickedRow;
      const id = newId('row');
      docActions.addRow({ id, label: 'Row 1', groupId: null });
      return id;
    };

    items.push({
      id: 'add-task',
      label: 'Add task here',
      icon: Plus,
      shortcut: 'T',
      onClick: () => {
        const rowId = rowForInsert();
        const id = newId('task');
        docActions.addTask({ id, rowId, label: 'New task', start: date, end: addDays(date, 7), percentComplete: 0 });
        setSelection([{ kind: 'task', id }]);
      },
    });
    items.push({
      id: 'add-milestone',
      label: 'Add milestone here',
      icon: Diamond,
      shortcut: 'Y',
      onClick: () => {
        const rowId = rowForInsert();
        const id = newId('ms');
        docActions.addMilestone({ id, rowId, label: 'New milestone', date });
        setSelection([{ kind: 'milestone', id }]);
      },
    });
    /**
     * A row, from the canvas (SRS-EDIT-060).
     *
     * Until now a row came from the command bar’s Insert group or from an
     * existing row’s own menu — both of which need a row to exist and be found
     * first — and from nowhere on the chart, which is where the planner is
     * already pointing when they run out of rows. It lands under the row the
     * click was on, so the menu uses the one piece of context it has.
     */
    items.push({
      id: 'add-row-here',
      label: clickedRow ? 'Add row below' : 'Add row',
      icon: Rows3,
      onClick: () => {
        const id = newId('row');
        const at = clickedRow ? doc.rows.findIndex((r) => r.id === clickedRow) : -1;
        const under = doc.rows[at];
        docActions.addRow({ id, label: `Row ${doc.rows.length + 1}`, groupId: under?.groupId ?? null }, at >= 0 ? at + 1 : undefined);
        setSelection([{ kind: 'row', id }]);
        reveal(id);
      },
    });
    items.push({ id: 's1', separator: true });
    items.push({
      id: 'add-deadline',
      label: 'Add deadline marker',
      icon: Flag,
      onClick: () => docActions.addMarker({ id: newId('mk'), type: 'deadline', label: 'Deadline', date }),
    });
  }

  /**
   * Row operations. Everything an agent can do to the row list, a person can do
   * here (SRS-ABR-019) — ordering, sections, and membership of one.
   */
  if (target.kind === 'row' && target.id) {
    const rowId = target.id;
    const index = doc.rows.findIndex((r) => r.id === rowId);
    const row = doc.rows[index];
    if (!row) { close(); return null; }
    const isSection = doc.rows.some((r) => r.groupId === rowId);
    // Sections this row could move *to*: not itself, and not the one it is
    // already in — offering either is a no-op dressed up as a choice.
    const sections = doc.rows.filter(
      (r) => doc.rows.some((c) => c.groupId === r.id) && r.id !== rowId && r.id !== row.groupId,
    );

    items.push({
      id: 'rename-row',
      label: 'Rename row…',
      icon: Pencil,
      onClick: () => {
        openDetails(rowId, 'row');
      },
    });
    items.push({ id: 'rs1', separator: true });
    items.push({
      id: 'move-up',
      label: 'Move up',
      icon: ArrowUp,
      disabled: index <= 0,
      onClick: () => docActions.moveRow(rowId, Math.max(0, index - 1)),
    });
    items.push({
      id: 'move-down',
      label: 'Move down',
      icon: ArrowDown,
      disabled: index >= doc.rows.length - 1,
      onClick: () => docActions.moveRow(rowId, index + 1),
    });
    items.push({ id: 'rs2', separator: true });

    if (!isSection) {
      items.push({
        id: 'new-section',
        label: 'Wrap in a new section',
        icon: FolderPlus,
        onClick: () => {
          const id = newId('row');
          docActions.addRow({ id, label: 'New section', groupId: null });
          docActions.moveRow(id, index);
          docActions.updateRow(rowId, { groupId: id });
        },
      });
      for (const s of sections) {
        items.push({
          id: `into-${s.id}`,
          label: `Move into “${s.label}”`,
          icon: FolderInput,
          onClick: () => docActions.updateRow(rowId, { groupId: s.id }),
        });
      }
      if (row.groupId) {
        items.push({
          id: 'ungroup',
          label: 'Take out of its section',
          icon: FolderInput,
          onClick: () => docActions.updateRow(rowId, { groupId: null }),
        });
      }
    }

    items.push({ id: 'rs3', separator: true });
    items.push({
      id: 'delete-row',
      label: isSection ? 'Delete section and its rows' : 'Delete row',
      icon: Trash2,
      onClick: () => docActions.deleteRow(rowId),
    });
  }

  if (target.kind === 'task' && target.id) {
    const task = doc.tasks.find((t) => t.id === target.id);
    if (!task) { close(); return null; }
    items.push({ id: 'edit', label: 'Edit label', icon: Pencil, shortcut: 'F2', onClick: () => beginEdit(task.id) });
    items.push({
      id: 'details',
      label: 'Details…',
      icon: SlidersHorizontal,
      shortcut: 'Enter',
      onClick: () => openDetails(task.id, 'task'),
    });
    items.push({
      id: 'dup',
      label: 'Duplicate',
      icon: Copy,
      onClick: () => {
        const id = newId('task');
        docActions.addTask({ ...task, id, label: `${task.label} copy`, start: addDays(task.start, 1), end: addDays(task.end, 1) });
        setSelection([{ kind: 'task', id }]);
      },
    });
    items.push({
      id: 'bracket',
      label: 'Wrap in bracket',
      icon: BracketIcon,
      onClick: () => docActions.addBracket({ id: newId('br'), label: 'Phase', start: task.start, end: task.end, rowIds: [task.rowId] }),
    });
    items.push({ id: 'lp-sep', separator: true });
    items.push(
      labelPositionItem(
        task.labelPlacement,
        // Choosing a position also releases a dragged label, or it could
        // never be handed back to the allocator.
        (p) => docActions.updateTask(task.id, { labelPlacement: p, labelOffset: null }),
        'task',
      ),
    );
    items.push({ id: 's1', separator: true });
    items.push({ id: 'del', label: 'Delete task', icon: Trash2, danger: true, shortcut: 'Del', onClick: () => docActions.deleteTask(task.id) });
  }

  if (target.kind === 'milestone' && target.id) {
    const m = doc.milestones.find((x) => x.id === target.id);
    if (!m) { close(); return null; }
    items.push({
      id: 'details',
      label: 'Details…',
      icon: SlidersHorizontal,
      shortcut: 'Enter',
      onClick: () => openDetails(m.id, 'milestone'),
    });
    items.push({
      id: 'dup',
      label: 'Duplicate',
      icon: Copy,
      onClick: () => {
        const id = newId('ms');
        docActions.addMilestone({ ...m, id, date: addDays(m.date, 1) });
      },
    });
    items.push({ id: 'lp-sep', separator: true });
    items.push(
      labelPositionItem(
        m.labelPlacement,
        (p) => docActions.updateMilestone(m.id, { labelPlacement: p, labelOffset: null }),
        'milestone',
      ),
    );
    items.push({ id: 's1', separator: true });
    items.push({ id: 'del', label: 'Delete milestone', icon: Trash2, danger: true, onClick: () => docActions.deleteMilestone(m.id) });
  }

  if (target.kind === 'bracket' && target.id) {
    items.push({
      id: 'details',
      label: 'Details…',
      icon: SlidersHorizontal,
      shortcut: 'Enter',
      onClick: () => openDetails(target.id!, 'bracket'),
    });
    items.push({ id: 's1', separator: true });
    items.push({ id: 'del', label: 'Delete bracket', icon: Trash2, danger: true, onClick: () => docActions.deleteBracket(target.id!) });
  }

  if (target.kind === 'dependency' && target.id) {
    const dep = doc.dependencies.find((d) => d.id === target.id);
    if (!dep) { close(); return null; }
    for (const dt of DEP_TYPES) {
      items.push({
        id: dt.value,
        label: dt.label + (dep.type === dt.value ? '  ✓' : ''),
        icon: GitBranch,
        onClick: () => docActions.updateDependency(dep.id, { type: dt.value }),
      });
    }
    items.push({ id: 's1', separator: true });
    items.push({ id: 'del', label: 'Delete dependency', icon: Trash2, danger: true, onClick: () => docActions.deleteDependency(dep.id) });
  }

  if (target.kind === 'marker' && target.id) {
    /* It used to set the selection and nothing else — an item called "Edit
       label" that opened no editor, which is why a marker could be created
       and never renamed. SR-IXC-16: this verb starts an in-place rename, in
       every context and for every element type. */
    items.push({
      id: 'edit',
      label: 'Rename',
      icon: Pencil,
      shortcut: 'F2',
      onClick: () => {
        setSelection([{ kind: 'marker', id: target.id! }]);
        beginEdit(target.id!);
      },
    });
    items.push({ id: 's1', separator: true });
    items.push({ id: 'del', label: 'Delete marker', icon: Trash2, danger: true, onClick: () => docActions.deleteMarker(target.id!) });
  }

  if (items.length === 0) { close(); return null; }

  return <ContextMenu x={x} y={y} items={items} onClose={close} />;
}
