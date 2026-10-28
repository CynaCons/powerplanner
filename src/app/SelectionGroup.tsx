import { Trash2, SlidersHorizontal, Brackets as BracketIcon, Plus, Rows3 } from 'lucide-react';
import { useDocumentStore } from '../stores/documentStore';
import { useSelectionStore, type Selectable } from '../stores/selectionStore';
import { useViewStore } from '../stores/viewStore';
import { addDays, todayISO } from '../utils/dates';
import { newId } from '../utils/ids';
import { ColorPalette } from './ColorPalette';
import type { Task, Milestone, Bracket } from '../types/document';

/**
 * Quick actions for what is selected (PLAN.md v2.20.0).
 *
 * The rule the owner set, three times: the bottom bar is not an editing
 * surface. What survives here is what reduces to a single press and changes
 * one thing — a colour, a day, ten percent, a delete. Anything with a text
 * field, a date picker or a dropdown in it moved to the details dialog, which
 * opens from this bar's one door, from the right-click menu, and from Enter.
 *
 * Nudges sit beside a drag rather than replacing it — dates and percent are
 * still changed by dragging the chart, which is the direct manipulation the
 * interaction conventions in AGENTS.md require; these are the precise path.
 */
export function SelectionGroup() {
  const items = useSelectionStore((s) => s.items);
  const doc = useDocumentStore((s) => s.doc);

  if (items.length === 0) return null;

  // The thing you clicked owns the bar; the row it sits on is selected too,
  // but only so the gutter highlights with it. Keying off the whole selection
  // instead would mean clicking a bar showed the ROW's actions, and colour,
  // dates, percent and notes would have no home at all.
  const sel = items[0];
  if (sel.kind === 'row') return <RowFields rowId={sel.id} />;

  // A bar plus its row is two items by design, so it is not a multi-select.
  const isBarWithItsRow = items.length === 2 && items[1]?.kind === 'row';
  if (items.length > 1 && !isBarWithItsRow) {
    return <MultiFields count={items.length} />;
  }
  if (sel.kind === 'task') {
    const task = doc.tasks.find((t) => t.id === sel.id);
    return task ? <TaskFields task={task} /> : null;
  }
  if (sel.kind === 'milestone') {
    const m = doc.milestones.find((x) => x.id === sel.id);
    return m ? <MilestoneFields milestone={m} /> : null;
  }
  if (sel.kind === 'bracket') {
    const b = doc.brackets.find((x) => x.id === sel.id);
    return b ? <BracketFields bracket={b} /> : null;
  }
  return null;
}

function Group({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div className="cmdbar__group">
      <span className="cmdbar__group-label">{label}</span>
      {children}
    </div>
  );
}

/**
 * What is selected, named but not editable here.
 *
 * The bar is for quick, context-dependent actions; renaming belongs on the
 * name itself, in the row gutter (owner directive 2026-10-26). Everything
 * else lives on the right-click menu.
 */
function SelectionName({ value }: { value: string }) {
  return <span className="cmdbar__selname" title={`${value} — rename it in the row list`}>{value}</span>;
}

function Nudge({ label, title, onClick }: { label: string; title: string; onClick: () => void }) {
  return (
    <button className="cmdbar__btn" title={title} aria-label={title} onClick={onClick}>
      {label}
    </button>
  );
}

function DeleteButton({ what, onClick }: { what: string; onClick: () => void }) {
  return (
    <button className="cmdbar__btn" title={`Delete ${what}`} aria-label={`Delete ${what}`} onClick={onClick}>
      <Trash2 size={14} strokeWidth={1.75} />
    </button>
  );
}

function WrapInBracket({ taskIds }: { taskIds: string[] }) {
  const doc = useDocumentStore((s) => s.doc);
  const addBracket = useDocumentStore((s) => s.addBracket);
  const tasks = doc.tasks.filter((t) => taskIds.includes(t.id));
  if (tasks.length === 0) return null;

  const wrap = () => {
    const start = tasks.reduce((a, t) => (t.start < a ? t.start : a), tasks[0].start);
    const end = tasks.reduce((a, t) => (t.end > a ? t.end : a), tasks[0].end);
    const rowIds = Array.from(new Set(tasks.map((t) => t.rowId)));
    addBracket({ id: newId('br'), label: 'Phase', start, end, rowIds });
  };

  return (
    <button className="cmdbar__btn" title="Wrap in bracket (B)" aria-label="Wrap in bracket" onClick={wrap}>
      <BracketIcon size={14} strokeWidth={1.75} />
    </button>
  );
}

/**
 * The door to the details dialog. Opens it; never edits anything itself.
 *
 * It names its own subject. Letting the dialog infer one from the selection
 * meant the Row button opened Task details for the first task on that row,
 * because selecting a row selects everything on it.
 */
function DetailsButton({ what, target }: { what: string; target: Selectable }) {
  const openDetails = useViewStore((s) => s.openDetails);
  return (
    <button
      className="cmdbar__btn"
      title={`${what} details… (Enter)`}
      aria-label={`${what} details`}
      onClick={() => openDetails(target)}
    >
      <SlidersHorizontal size={14} strokeWidth={1.75} />
    </button>
  );
}

function TaskFields({ task }: { task: Task }) {
  const updateTask = useDocumentStore((s) => s.updateTask);
  const deleteTask = useDocumentStore((s) => s.deleteTask);
  const clear = useSelectionStore((s) => s.clear);
  const pct = task.percentComplete ?? 0;

  const shift = (days: number) =>
    updateTask(task.id, { start: addDays(task.start, days), end: addDays(task.end, days) });
  const bump = (by: number) =>
    updateTask(task.id, { percentComplete: Math.max(0, Math.min(100, pct + by)) });

  return (
    <Group label="Task">
      <SelectionName value={task.label} />
      <ColorPalette value={task.color ?? '#1F4E79'} onChange={(color) => updateTask(task.id, { color })} />
      <Nudge label="−1d" title="Move a day earlier" onClick={() => shift(-1)} />
      <Nudge label="+1d" title="Move a day later" onClick={() => shift(1)} />
      <Nudge label="−10%" title="Ten percent less complete" onClick={() => bump(-10)} />
      <Nudge label="+10%" title="Ten percent more complete" onClick={() => bump(10)} />
      <WrapInBracket taskIds={[task.id]} />
      <DetailsButton what="Task" target={{ kind: 'task', id: task.id }} />
      <DeleteButton what="task" onClick={() => { deleteTask(task.id); clear(); }} />
    </Group>
  );
}

function MilestoneFields({ milestone }: { milestone: Milestone }) {
  const update = useDocumentStore((s) => s.updateMilestone);
  const remove = useDocumentStore((s) => s.deleteMilestone);
  const clear = useSelectionStore((s) => s.clear);

  return (
    <Group label="Milestone">
      <SelectionName value={milestone.label} />
      <Nudge label="−1d" title="Move a day earlier" onClick={() => update(milestone.id, { date: addDays(milestone.date, -1) })} />
      <Nudge label="+1d" title="Move a day later" onClick={() => update(milestone.id, { date: addDays(milestone.date, 1) })} />
      <DetailsButton what="Milestone" target={{ kind: 'milestone', id: milestone.id }} />
      <DeleteButton what="milestone" onClick={() => { remove(milestone.id); clear(); }} />
    </Group>
  );
}

function BracketFields({ bracket }: { bracket: Bracket }) {
  const remove = useDocumentStore((s) => s.deleteBracket);
  const clear = useSelectionStore((s) => s.clear);

  return (
    <Group label="Bracket">
      <SelectionName value={bracket.label} />
      <DetailsButton what="Bracket" target={{ kind: 'bracket', id: bracket.id }} />
      <DeleteButton what="bracket" onClick={() => { remove(bracket.id); clear(); }} />
    </Group>
  );
}

function RowFields({ rowId }: { rowId: string }) {
  const doc = useDocumentStore((s) => s.doc);
  const addTask = useDocumentStore((s) => s.addTask);
  const addRow = useDocumentStore((s) => s.addRow);
  const moveRow = useDocumentStore((s) => s.moveRow);
  const deleteRow = useDocumentStore((s) => s.deleteRow);
  const setSelection = useSelectionStore((s) => s.setSelection);
  const clear = useSelectionStore((s) => s.clear);

  const row = doc.rows.find((r) => r.id === rowId);
  if (!row) return null;
  const index = doc.rows.findIndex((r) => r.id === rowId);
  const onRow = doc.tasks.filter((t) => t.rowId === rowId);

  const addTaskHere = () => {
    const start = onRow.length
      ? onRow.reduce((a, t) => (t.end > a ? t.end : a), onRow[0].end)
      : todayISO();
    const id = newId('task');
    addTask({ id, rowId, label: 'New task', start, end: addDays(start, 7), percentComplete: 0 });
    setSelection([{ kind: 'task', id }, { kind: 'row', id: rowId }]);
  };

  const addRowBelow = () => {
    const id = newId('row');
    addRow({ id, label: 'New row', groupId: row.groupId ?? null });
    moveRow(id, index + 1);
    setSelection([{ kind: 'row', id }]);
  };

  return (
    <Group label="Row">
      <SelectionName value={row.label} />
      <button className="cmdbar__btn" title="Add a task on this row" aria-label="Add task here" onClick={addTaskHere}>
        <Plus size={14} strokeWidth={1.75} /><span>Task</span>
      </button>
      <button className="cmdbar__btn" title="Add a row below this one" aria-label="Add row below" onClick={addRowBelow}>
        <Rows3 size={14} strokeWidth={1.75} /><span>Row below</span>
      </button>
      <WrapInBracket taskIds={onRow.map((t) => t.id)} />
      <DetailsButton what="Row" target={{ kind: 'row', id: rowId }} />
      <DeleteButton what="row" onClick={() => { deleteRow(rowId); clear(); }} />
    </Group>
  );
}

function MultiFields({ count }: { count: number }) {
  const items = useSelectionStore((s) => s.items);
  const clear = useSelectionStore((s) => s.clear);
  const store = useDocumentStore();

  const deleteAll = () => {
    for (const it of items) {
      if (it.kind === 'task') store.deleteTask(it.id);
      else if (it.kind === 'milestone') store.deleteMilestone(it.id);
      else if (it.kind === 'bracket') store.deleteBracket(it.id);
    }
    clear();
  };

  return (
    <Group label="Selection">
      <span className="cmdbar__selname">{count} items selected</span>
      <WrapInBracket taskIds={items.filter((i) => i.kind === 'task').map((i) => i.id)} />
      <DeleteButton what="selection" onClick={deleteAll} />
    </Group>
  );
}
