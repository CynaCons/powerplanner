import { useContextMenuStore } from '../stores/contextMenuStore';
import { useDocumentStore } from '../stores/documentStore';
import { useSelectionStore } from '../stores/selectionStore';
import { useEditStore } from '../stores/editStore';
import { ContextMenu, type ContextMenuItem } from './ContextMenu';
import { Pencil, Copy, Trash2, Plus, Diamond, Brackets as BracketIcon, Flag, GitBranch, Palette } from 'lucide-react';
import { newId } from '../utils/ids';
import { addDays, todayISO } from '../utils/dates';
import type { DependencyType } from '../types/document';

const DEP_TYPES: { value: DependencyType; label: string }[] = [
  { value: 'finish-to-start', label: 'Finish → Start (default)' },
  { value: 'start-to-start', label: 'Start → Start' },
  { value: 'finish-to-finish', label: 'Finish → Finish' },
  { value: 'start-to-finish', label: 'Start → Finish' },
];

export function ChartContextMenu() {
  const { open, x, y, target, close } = useContextMenuStore();
  const doc = useDocumentStore((s) => s.doc);
  const docActions = useDocumentStore();
  const setSelection = useSelectionStore((s) => s.setSelection);
  const beginEdit = useEditStore((s) => s.beginEdit);

  if (!open || !target) return null;

  const items: ContextMenuItem[] = [];

  if (target.kind === 'background') {
    const date = target.date ?? todayISO();
    const rowId = target.rowId ?? doc.rows[0]?.id;
    items.push({
      id: 'add-task',
      label: 'Add task here',
      icon: Plus,
      shortcut: 'T',
      onClick: () => {
        if (!rowId) return;
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
        if (!rowId) return;
        const id = newId('ms');
        docActions.addMilestone({ id, rowId, label: 'New milestone', date });
        setSelection([{ kind: 'milestone', id }]);
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

  if (target.kind === 'task' && target.id) {
    const task = doc.tasks.find((t) => t.id === target.id);
    if (!task) { close(); return null; }
    items.push({ id: 'edit', label: 'Edit label', icon: Pencil, shortcut: 'F2', onClick: () => beginEdit(task.id) });
    items.push({ id: 'color', label: 'Change color in inspector…', icon: Palette, onClick: () => setSelection([{ kind: 'task', id: task.id }]) });
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
    items.push({ id: 's1', separator: true });
    items.push({ id: 'del', label: 'Delete task', icon: Trash2, danger: true, shortcut: 'Del', onClick: () => docActions.deleteTask(task.id) });
  }

  if (target.kind === 'milestone' && target.id) {
    const m = doc.milestones.find((x) => x.id === target.id);
    if (!m) { close(); return null; }
    items.push({ id: 'edit', label: 'Edit label', icon: Pencil, onClick: () => setSelection([{ kind: 'milestone', id: m.id }]) });
    items.push({
      id: 'dup',
      label: 'Duplicate',
      icon: Copy,
      onClick: () => {
        const id = newId('ms');
        docActions.addMilestone({ ...m, id, date: addDays(m.date, 1) });
      },
    });
    items.push({ id: 's1', separator: true });
    items.push({ id: 'del', label: 'Delete milestone', icon: Trash2, danger: true, onClick: () => docActions.deleteMilestone(m.id) });
  }

  if (target.kind === 'bracket' && target.id) {
    items.push({ id: 'edit', label: 'Edit in inspector', icon: Pencil, onClick: () => setSelection([{ kind: 'bracket', id: target.id! }]) });
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
    items.push({ id: 'edit', label: 'Edit label', icon: Pencil, onClick: () => setSelection([{ kind: 'marker', id: target.id! }]) });
    items.push({ id: 's1', separator: true });
    items.push({ id: 'del', label: 'Delete marker', icon: Trash2, danger: true, onClick: () => docActions.deleteMarker(target.id!) });
  }

  if (items.length === 0) { close(); return null; }

  return <ContextMenu x={x} y={y} items={items} onClose={close} />;
}
