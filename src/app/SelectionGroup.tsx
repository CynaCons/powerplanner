import { useLayoutEffect, useRef, useState } from 'react';
import { createPortal } from 'react-dom';
import { Trash2, SlidersHorizontal } from 'lucide-react';
import { useDocumentStore } from '../stores/documentStore';
import { useSelectionStore } from '../stores/selectionStore';
import { addDays } from '../utils/dates';
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

  if (items.length > 1) {
    return <MultiFields count={items.length} />;
  }

  const sel = items[0];
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

function LabelInput({ value, onChange }: { value: string; onChange: (v: string) => void }) {
  return (
    <input
      className="cmdbar__input"
      aria-label="Label"
      value={value}
      onChange={(e) => onChange(e.target.value)}
    />
  );
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
      <LabelInput value={task.label} onChange={(label) => updateTask(task.id, { label })} />
      <ColorPalette value={task.color ?? '#1F4E79'} onChange={(color) => updateTask(task.id, { color })} />
      <Nudge label="−1d" title="Move a day earlier" onClick={() => shift(-1)} />
      <Nudge label="+1d" title="Move a day later" onClick={() => shift(1)} />
      <Nudge label="−10%" title="Ten percent less complete" onClick={() => bump(-10)} />
      <Nudge label="+10%" title="Ten percent more complete" onClick={() => bump(10)} />
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
      <LabelInput value={milestone.label} onChange={(label) => update(milestone.id, { label })} />
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
      <LabelInput value={bracket.label} onChange={(label) => update(bracket.id, { label })} />
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
      <DeleteButton what="selection" onClick={deleteAll} />
    </Group>
  );
}
