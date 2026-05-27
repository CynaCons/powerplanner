import { useDocumentStore } from '../stores/documentStore';
import { useSelectionStore } from '../stores/selectionStore';
import type { Task, Milestone, Bracket } from '../types/document';
import { ColorPalette } from './ColorPalette';

export function SelectionInspector() {
  const items = useSelectionStore((s) => s.items);
  const doc = useDocumentStore((s) => s.doc);

  if (items.length === 0) return null;
  if (items.length > 1) {
    return (
      <>
        <h3>Selection</h3>
        <p style={{ fontSize: 11, color: 'var(--color-text-soft)' }}>{items.length} items selected.</p>
      </>
    );
  }

  const sel = items[0];
  if (sel.kind === 'task') {
    const task = doc.tasks.find((t) => t.id === sel.id);
    if (!task) return null;
    return <TaskFields task={task} />;
  }
  if (sel.kind === 'milestone') {
    const m = doc.milestones.find((x) => x.id === sel.id);
    if (!m) return null;
    return <MilestoneFields milestone={m} />;
  }
  if (sel.kind === 'bracket') {
    const b = doc.brackets.find((x) => x.id === sel.id);
    if (!b) return null;
    return <BracketFields bracket={b} />;
  }
  return null;
}

function TaskFields({ task }: { task: Task }) {
  const updateTask = useDocumentStore((s) => s.updateTask);
  const deleteTask = useDocumentStore((s) => s.deleteTask);
  const rows = useDocumentStore((s) => s.doc.rows);
  const clear = useSelectionStore((s) => s.clear);

  return (
    <>
      <h3>Task</h3>
      <div className="field">
        <label>Label</label>
        <input value={task.label} onChange={(e) => updateTask(task.id, { label: e.target.value })} />
      </div>
      <div className="field">
        <label>Row</label>
        <select value={task.rowId} onChange={(e) => updateTask(task.id, { rowId: e.target.value })}>
          {rows.map((r) => (
            <option key={r.id} value={r.id}>
              {r.label}
            </option>
          ))}
        </select>
      </div>
      <div className="field-row">
        <div className="field">
          <label>Start</label>
          <input type="date" value={task.start} onChange={(e) => updateTask(task.id, { start: e.target.value })} />
        </div>
        <div className="field">
          <label>End</label>
          <input type="date" value={task.end} onChange={(e) => updateTask(task.id, { end: e.target.value })} />
        </div>
      </div>
      <div className="field">
        <label>Percent complete ({task.percentComplete ?? 0}%)</label>
        <input
          type="range"
          min={0}
          max={100}
          value={task.percentComplete ?? 0}
          onChange={(e) => updateTask(task.id, { percentComplete: Number(e.target.value) })}
        />
      </div>
      <div className="field">
        <label>Color</label>
        <ColorPalette value={task.color ?? '#7a82c9'} onChange={(c) => updateTask(task.id, { color: c })} />
      </div>
      <button
        onClick={() => {
          deleteTask(task.id);
          clear();
        }}
        style={{ color: '#ef4444' }}
      >
        Delete task
      </button>
    </>
  );
}

function MilestoneFields({ milestone }: { milestone: Milestone }) {
  const update = useDocumentStore((s) => s.updateMilestone);
  const remove = useDocumentStore((s) => s.deleteMilestone);
  const rows = useDocumentStore((s) => s.doc.rows);
  const clear = useSelectionStore((s) => s.clear);

  return (
    <>
      <h3>Milestone</h3>
      <div className="field">
        <label>Label</label>
        <input value={milestone.label} onChange={(e) => update(milestone.id, { label: e.target.value })} />
      </div>
      <div className="field">
        <label>Row</label>
        <select value={milestone.rowId} onChange={(e) => update(milestone.id, { rowId: e.target.value })}>
          {rows.map((r) => (
            <option key={r.id} value={r.id}>
              {r.label}
            </option>
          ))}
        </select>
      </div>
      <div className="field">
        <label>Date</label>
        <input type="date" value={milestone.date} onChange={(e) => update(milestone.id, { date: e.target.value })} />
      </div>
      <button onClick={() => { remove(milestone.id); clear(); }} style={{ color: '#ef4444' }}>
        Delete milestone
      </button>
    </>
  );
}

function BracketFields({ bracket }: { bracket: Bracket }) {
  const update = useDocumentStore((s) => s.updateBracket);
  const remove = useDocumentStore((s) => s.deleteBracket);
  const clear = useSelectionStore((s) => s.clear);

  return (
    <>
      <h3>Bracket</h3>
      <div className="field">
        <label>Label</label>
        <input value={bracket.label} onChange={(e) => update(bracket.id, { label: e.target.value })} />
      </div>
      <div className="field-row">
        <div className="field">
          <label>Start</label>
          <input type="date" value={bracket.start} onChange={(e) => update(bracket.id, { start: e.target.value })} />
        </div>
        <div className="field">
          <label>End</label>
          <input type="date" value={bracket.end} onChange={(e) => update(bracket.id, { end: e.target.value })} />
        </div>
      </div>
      <button onClick={() => { remove(bracket.id); clear(); }} style={{ color: '#ef4444' }}>
        Delete bracket
      </button>
    </>
  );
}
