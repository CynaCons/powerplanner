import { useDocumentStore } from '../stores/documentStore';
import { useViewportStore } from '../stores/viewportStore';
import { Undo2, Redo2, Plus, Calendar, Maximize2 } from 'lucide-react';
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
  const rows = useDocumentStore((s) => s.doc.rows);
  const fit = useViewportStore((s) => s.fit);
  const doc = useDocumentStore((s) => s.doc);

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
