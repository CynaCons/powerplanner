import { useCallback, useEffect } from 'react';
import { createPortal } from 'react-dom';
import { X } from 'lucide-react';
import { useDocumentStore } from '../stores/documentStore';
import { useViewStore } from '../stores/viewStore';
import { ColorPalette } from './ColorPalette';
import { LabelPositionPicker } from './LabelPositionPicker';
import type { LabelPlacement } from '../types/document';

/**
 * Everything about one element that is typed rather than dragged.
 *
 * It used to live in a popover hanging off the command bar, which put dates,
 * percent, notes and the row a task belongs to inside a 40px strip of quick
 * actions. The owner rejected that three times — "the bottom toolbar is not
 * made for editing tasks" — so the toolbar now holds only what reduces to one
 * press, and anything with a field in it is here.
 *
 * Reached from the right-click menu, from Enter on a selection, and from the
 * one button in the bar that admits it is a door rather than an editor.
 */
/** Any field leaving focus closes the gesture frame (SRS-EDIT-022). */
const endFrame = () => useDocumentStore.getState().endFrame();

export function DetailsDialog() {
  const open = useViewStore((s) => s.detailsOpen);
  const sel = useViewStore((s) => s.detailsTarget);
  const closeDetails = useViewStore((s) => s.closeDetails);
  const close = useCallback(() => {
    endFrame();
    closeDetails();
  }, [closeDetails]);
  const doc = useDocumentStore((s) => s.doc);

  useEffect(() => {
    if (!open) return;
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') {
        e.stopPropagation();
        close();
      }
    };
    window.addEventListener('keydown', onKey, true);
    return () => window.removeEventListener('keydown', onKey, true);
  }, [open, close]);

  // Delete the thing being edited and the dialog has nothing to show. Without
  // this it merely rendered nothing while `detailsOpen` stayed true, and the
  // next ordinary click on a bar popped a modal nobody asked for.
  const subjectExists =
    !sel ? false
    : sel.kind === 'task' ? doc.tasks.some((t) => t.id === sel.id)
    : sel.kind === 'milestone' ? doc.milestones.some((m) => m.id === sel.id)
    : sel.kind === 'bracket' ? doc.brackets.some((b) => b.id === sel.id)
    : sel.kind === 'row' ? doc.rows.some((r) => r.id === sel.id)
    : false;

  useEffect(() => {
    if (open && !subjectExists) closeDetails();
  }, [open, subjectExists, closeDetails]);

  // Escape, the scrim and unmount all have to close the frame, and a blur
  // does not reliably fire when React removes the node.
  useEffect(() => endFrame, []);

  if (!open || !sel || !subjectExists) return null;

  let title = '';
  let body: React.ReactNode = null;

  if (sel.kind === 'task') {
    const task = doc.tasks.find((t) => t.id === sel.id);
    if (!task) return null;
    title = `Task — ${task.label}`;
    body = <TaskBody id={task.id} />;
  } else if (sel.kind === 'milestone') {
    const m = doc.milestones.find((x) => x.id === sel.id);
    if (!m) return null;
    title = `Milestone — ${m.label}`;
    body = <MilestoneBody id={m.id} />;
  } else if (sel.kind === 'bracket') {
    const b = doc.brackets.find((x) => x.id === sel.id);
    if (!b) return null;
    title = `Bracket — ${b.label}`;
    body = <BracketBody id={b.id} />;
  } else if (sel.kind === 'row') {
    const r = doc.rows.find((x) => x.id === sel.id);
    if (!r) return null;
    title = `Row — ${r.label}`;
    body = <RowBody id={r.id} />;
  } else {
    return null;
  }

  return createPortal(
    <div className="dialog-scrim" onMouseDown={close}>
      <div
        className="dialog"
        role="dialog"
        aria-modal="true"
        aria-label={title}
        onMouseDown={(e) => e.stopPropagation()}
      >
        <div className="dialog__head">
          <h2 className="dialog__title">{title}</h2>
          <button className="dialog__close" onClick={close} aria-label="Close">
            <X size={15} strokeWidth={1.75} />
          </button>
        </div>
        {body}
      </div>
    </div>,
    document.body,
  );
}

function Field({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <label className="dialog__field">
      <span>{label}</span>
      {children}
    </label>
  );
}

function RowSelect({ value, onChange }: { value: string; onChange: (v: string) => void }) {
  const rows = useDocumentStore((s) => s.doc.rows);
  return (
    <Field label="Row">
      <select className="dialog__input" value={value} onChange={(e) => onChange(e.target.value)}>
        {rows.map((r) => (
          <option key={r.id} value={r.id}>{r.label}</option>
        ))}
      </select>
    </Field>
  );
}

function TaskBody({ id }: { id: string }) {
  const task = useDocumentStore((s) => s.doc.tasks.find((t) => t.id === id));
  const updateTask = useDocumentStore((s) => s.updateTask);
  if (!task) return null;
  const pct = task.percentComplete ?? 0;

  return (
    <>
      <Field label="Name">
        <input
          className="dialog__input"
          value={task.label}
          autoFocus
          onChange={(e) => updateTask(task.id, { label: e.target.value }, `task:${task.id}:label`)}
          onBlur={endFrame}
        />
      </Field>
      <RowSelect value={task.rowId} onChange={(rowId) => updateTask(task.id, { rowId })} />
      <div className="dialog__row">
        <Field label="Start">
          <input type="date" className="dialog__input" value={task.start} onChange={(e) => updateTask(task.id, { start: e.target.value }, `task:${task.id}:start`)} onBlur={endFrame} />
        </Field>
        <Field label="End">
          <input type="date" className="dialog__input" value={task.end} onChange={(e) => updateTask(task.id, { end: e.target.value }, `task:${task.id}:end`)} onBlur={endFrame} />
        </Field>
      </div>
      <Field label={`Complete — ${pct}%`}>
        <input
          type="range"
          min={0}
          max={100}
          value={pct}
          onChange={(e) => updateTask(task.id, { percentComplete: Number(e.target.value) }, `task:${task.id}:pct`)}
          onBlur={endFrame}
        />
      </Field>
      <Field label="Colour">
        <ColorPalette value={task.color ?? '#1F4E79'} onChange={(color) => updateTask(task.id, { color })} />
      </Field>
      <Field label="Notes">
        <textarea
          className="dialog__textarea"
          value={task.notes ?? ''}
          placeholder="Context, links, decisions…"
          onChange={(e) => updateTask(task.id, { notes: e.target.value }, `task:${task.id}:notes`)}
          onBlur={endFrame}
        />
      </Field>
      <LabelPositionPicker
        current={task.labelPlacement}
        onPick={(p: LabelPlacement | undefined) => updateTask(task.id, { labelPlacement: p, labelOffset: null })}
        kind="task"
      />
    </>
  );
}

function MilestoneBody({ id }: { id: string }) {
  const m = useDocumentStore((s) => s.doc.milestones.find((x) => x.id === id));
  const update = useDocumentStore((s) => s.updateMilestone);
  if (!m) return null;

  return (
    <>
      <Field label="Name">
        <input
          className="dialog__input"
          value={m.label}
          autoFocus
          onChange={(e) => update(m.id, { label: e.target.value })}
        />
      </Field>
      <RowSelect value={m.rowId} onChange={(rowId) => update(m.id, { rowId })} />
      <Field label="Date">
        <input type="date" className="dialog__input" value={m.date} onChange={(e) => update(m.id, { date: e.target.value })} />
      </Field>
      <LabelPositionPicker
        current={m.labelPlacement}
        onPick={(p: LabelPlacement | undefined) => update(m.id, { labelPlacement: p, labelOffset: null })}
        kind="milestone"
      />
    </>
  );
}

function BracketBody({ id }: { id: string }) {
  const b = useDocumentStore((s) => s.doc.brackets.find((x) => x.id === id));
  const update = useDocumentStore((s) => s.updateBracket);
  if (!b) return null;

  return (
    <>
      <Field label="Name">
        <input
          className="dialog__input"
          value={b.label}
          autoFocus
          onChange={(e) => update(b.id, { label: e.target.value })}
        />
      </Field>
      <div className="dialog__row">
        <Field label="Start">
          <input type="date" className="dialog__input" value={b.start} onChange={(e) => update(b.id, { start: e.target.value })} />
        </Field>
        <Field label="End">
          <input type="date" className="dialog__input" value={b.end} onChange={(e) => update(b.id, { end: e.target.value })} />
        </Field>
      </div>
    </>
  );
}

function RowBody({ id }: { id: string }) {
  const row = useDocumentStore((s) => s.doc.rows.find((r) => r.id === id));
  const rows = useDocumentStore((s) => s.doc.rows);
  const updateRow = useDocumentStore((s) => s.updateRow);
  if (!row) return null;
  const sections = rows.filter((r) => r.id !== row.id && !r.groupId);

  return (
    <>
      <Field label="Name">
        <input
          className="dialog__input"
          value={row.label}
          autoFocus
          onChange={(e) => updateRow(row.id, { label: e.target.value }, `row:${row.id}:label`)}
        />
      </Field>
      <Field label="Section">
        <select
          className="dialog__input"
          value={row.groupId ?? ''}
          onChange={(e) => updateRow(row.id, { groupId: e.target.value || null })}
        >
          <option value="">— none —</option>
          {sections.map((s) => (
            <option key={s.id} value={s.id}>{s.label}</option>
          ))}
        </select>
      </Field>
    </>
  );
}
