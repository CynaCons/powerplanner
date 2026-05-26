import { useDocumentStore } from '../stores/documentStore';
import { useViewportStore } from '../stores/viewportStore';
import { useEditStore } from '../stores/editStore';
import { Undo2, Redo2, Plus, Calendar, Maximize2, Diamond, Magnet, Rows3 } from 'lucide-react';
import { newId } from '../utils/ids';
import { todayISO, addDays } from '../utils/dates';

export function Header() {
  const title = useDocumentStore((s) => s.doc.title);
  const setTitle = useDocumentStore((s) => s.setTitle);
  const undo = useDocumentStore((s) => s.undo);
  const redo = useDocumentStore((s) => s.redo);
  const canUndo = useDocumentStore((s) => s.past.length > 0);
  const canRedo = useDocumentStore((s) => s.future.length > 0);
  const addTask = useDocumentStore((s) => s.addTask);
  const addMilestone = useDocumentStore((s) => s.addMilestone);
  const addRow = useDocumentStore((s) => s.addRow);
  const rows = useDocumentStore((s) => s.doc.rows);
  const fit = useViewportStore((s) => s.fit);
  const doc = useDocumentStore((s) => s.doc);
  const snapEnabled = useEditStore((s) => s.snapEnabled);
  const setSnap = useEditStore((s) => s.setSnap);

  const onFit = () => {
    const dates = [
      ...doc.tasks.flatMap((t) => [t.start, t.end]),
      ...doc.milestones.map((m) => m.date),
    ];
    if (dates.length === 0) return;
    const min = dates.reduce((a, b) => (a < b ? a : b));
    const max = dates.reduce((a, b) => (a > b ? a : b));
    const chartArea = document.querySelector('.chart-area');
    const width = (chartArea?.clientWidth ?? 800) - 200;
    fit(min, max, Math.max(200, width));
  };

  const onAddTask = () => {
    if (rows.length === 0) return;
    const today = todayISO();
    addTask({
      id: newId('task'),
      rowId: rows[0].id,
      label: 'New task',
      start: today,
      end: addDays(today, 7),
      percentComplete: 0,
    });
  };

  const onAddMilestone = () => {
    if (rows.length === 0) return;
    addMilestone({ id: newId('ms'), rowId: rows[0].id, label: 'New milestone', date: todayISO() });
  };

  const onAddRow = () => {
    addRow({ id: newId('row'), label: `Row ${rows.length + 1}`, groupId: null });
  };

  return (
    <header className="app-header">
      <div className="brand">
        <span className="brand-mark" />
        <span>PowerPlanner</span>
        <input
          aria-label="Chart title"
          value={title}
          onChange={(e) => setTitle(e.target.value)}
          style={{ marginLeft: 16, width: 280, background: 'transparent', border: 'none' }}
        />
      </div>
      <div className="toolbar">
        <button onClick={onAddTask} title="Add task (N)" className="icon-btn">
          <Plus size={14} /> Task
        </button>
        <button onClick={onAddMilestone} title="Add milestone (M)" className="icon-btn">
          <Diamond size={14} /> Milestone
        </button>
        <button onClick={onAddRow} title="Add row" className="icon-btn">
          <Rows3 size={14} /> Row
        </button>
        <button
          onClick={() => setSnap(!snapEnabled)}
          title="Toggle snap to scale (S)"
          className="icon-btn"
          style={{ background: snapEnabled ? 'var(--color-accent-soft)' : undefined, color: snapEnabled ? 'white' : undefined }}
        >
          <Magnet size={14} /> Snap
        </button>
        <button onClick={onFit} title="Fit to data (Home)" className="icon-btn">
          <Maximize2 size={14} /> Fit
        </button>
        <button onClick={undo} disabled={!canUndo} title="Undo (Ctrl+Z)" className="icon-btn">
          <Undo2 size={14} />
        </button>
        <button onClick={redo} disabled={!canRedo} title="Redo (Ctrl+Shift+Z)" className="icon-btn">
          <Redo2 size={14} />
        </button>
        <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6, color: 'var(--color-text-dim)', marginLeft: 8 }}>
          <Calendar size={12} /> v0.1
        </span>
      </div>
    </header>
  );
}
