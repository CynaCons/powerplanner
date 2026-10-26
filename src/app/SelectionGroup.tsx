import { useLayoutEffect, useRef, useState } from 'react';
import { createPortal } from 'react-dom';
import { Trash2, SlidersHorizontal, Brackets as BracketIcon, Plus, Rows3 } from 'lucide-react';
import { useDocumentStore } from '../stores/documentStore';
import { useSelectionStore } from '../stores/selectionStore';
import { addDays, todayISO } from '../utils/dates';
import { newId } from '../utils/ids';
import { ColorPalette } from './ColorPalette';
import { LabelPositionPicker } from './LabelPositionPicker';
import type { Task, Milestone, Bracket, LabelPlacement } from '../types/document';

/**
 * Everything the inspector could edit, in the command bar (PLAN.md v2.13.2 /
 * C2).
 *
 * The split is deliberate: what reduces to a button is a button, and what does
 * not lives one press away in Edit. Nudges sit beside a drag rather than
 * replacing it — dates and percent are still changed by dragging the chart,
 * which is the direct manipulation the interaction conventions in AGENTS.md
 * require; these are the precise path, not the only one.
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

/**
 * The controls that do not reduce to a button, one press away.
 *
 * Rendered in a portal, not inside the bar: `.cmdbar` carries
 * `overflow-y: hidden` so its groups can scroll on a narrow window, which
 * clipped an in-flow popover out of existence. The e2e assertions still
 * passed, because presence in the DOM is not the same as being visible to a
 * person — found by looking at it.
 */
function EditPopover({ title, children }: { title: string; children: React.ReactNode }) {
  const [open, setOpen] = useState(false);
  const btnRef = useRef<HTMLButtonElement | null>(null);
  const [pos, setPos] = useState<{ left: number; bottom: number } | null>(null);

  useLayoutEffect(() => {
    if (!open || !btnRef.current) return;
    const r = btnRef.current.getBoundingClientRect();
    setPos({
      left: Math.max(8, Math.min(window.innerWidth - 268, r.left - 120)),
      bottom: window.innerHeight - r.top + 8,
    });
  }, [open]);

  return (
    <>
      <button
        ref={btnRef}
        className={`cmdbar__btn ${open ? 'is-active' : ''}`}
        onClick={() => setOpen((o) => !o)}
        aria-expanded={open}
        aria-label={title}
        title={title}
      >
        <SlidersHorizontal size={14} strokeWidth={1.75} />
      </button>
      {open && pos
        ? createPortal(
            <div
              className="cmdbar__popover"
              role="dialog"
              aria-label={title}
              style={{ left: pos.left, bottom: pos.bottom }}
            >
              {children}
            </div>,
            document.body,
          )
        : null}
    </>
  );
}

function Field({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <label className="cmdbar__pop-field">
      <span>{label}</span>
      {children}
    </label>
  );
}

function RowSelect({ value, onChange }: { value: string; onChange: (v: string) => void }) {
  const rows = useDocumentStore((s) => s.doc.rows);
  return (
    <Field label="Row">
      <select className="cmdbar__select" value={value} onChange={(e) => onChange(e.target.value)}>
        {rows.map((r) => (
          <option key={r.id} value={r.id}>{r.label}</option>
        ))}
      </select>
    </Field>
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
      <EditPopover title="Edit task">
        <RowSelect value={task.rowId} onChange={(rowId) => updateTask(task.id, { rowId })} />
        <Field label="Start">
          <input type="date" className="cmdbar__select" value={task.start} onChange={(e) => updateTask(task.id, { start: e.target.value })} />
        </Field>
        <Field label="End">
          <input type="date" className="cmdbar__select" value={task.end} onChange={(e) => updateTask(task.id, { end: e.target.value })} />
        </Field>
        <Field label={`Complete — ${pct}%`}>
          <input
            type="range"
            min={0}
            max={100}
            value={pct}
            onChange={(e) => updateTask(task.id, { percentComplete: Number(e.target.value) })}
          />
        </Field>
        <Field label="Notes">
          <textarea
            className="cmdbar__textarea"
            value={task.notes ?? ''}
            placeholder="Context, links, decisions…"
            onChange={(e) => updateTask(task.id, { notes: e.target.value })}
          />
        </Field>
        <LabelPositionPicker
          current={task.labelPlacement}
          onPick={(p: LabelPlacement | undefined) => updateTask(task.id, { labelPlacement: p, labelOffset: null })}
          kind="task"
        />
      </EditPopover>
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
      <EditPopover title="Edit milestone">
        <RowSelect value={milestone.rowId} onChange={(rowId) => update(milestone.id, { rowId })} />
        <Field label="Date">
          <input type="date" className="cmdbar__select" value={milestone.date} onChange={(e) => update(milestone.id, { date: e.target.value })} />
        </Field>
        <LabelPositionPicker
          current={milestone.labelPlacement}
          onPick={(p: LabelPlacement | undefined) => update(milestone.id, { labelPlacement: p, labelOffset: null })}
          kind="milestone"
        />
      </EditPopover>
      <DeleteButton what="milestone" onClick={() => { remove(milestone.id); clear(); }} />
    </Group>
  );
}

function BracketFields({ bracket }: { bracket: Bracket }) {
  const update = useDocumentStore((s) => s.updateBracket);
  const remove = useDocumentStore((s) => s.deleteBracket);
  const clear = useSelectionStore((s) => s.clear);

  return (
    <Group label="Bracket">
      <SelectionName value={bracket.label} />
      <EditPopover title="Edit bracket">
        <Field label="Start">
          <input type="date" className="cmdbar__select" value={bracket.start} onChange={(e) => update(bracket.id, { start: e.target.value })} />
        </Field>
        <Field label="End">
          <input type="date" className="cmdbar__select" value={bracket.end} onChange={(e) => update(bracket.id, { end: e.target.value })} />
        </Field>
      </EditPopover>
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
