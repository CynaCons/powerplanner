import { useDocumentStore } from '../stores/documentStore';
import { useViewportStore } from '../stores/viewportStore';
import { useEditStore } from '../stores/editStore';
import { Undo2, Redo2, Plus, Maximize2, Diamond, Magnet, Rows3, Brackets as BracketIcon, Flag, HelpCircle } from 'lucide-react';
import { useSelectionStore } from '../stores/selectionStore';
import { newId } from '../utils/ids';
import { todayISO, addDays } from '../utils/dates';
import { ExportMenu } from './ExportMenu';

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
  const addBracket = useDocumentStore((s) => s.addBracket);
  const addMarker = useDocumentStore((s) => s.addMarker);
  const selection = useSelectionStore((s) => s.items);
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
    const totalWidth = chartArea?.clientWidth ?? 800;
    const gutter = totalWidth < 600 ? 120 : 200;
    fit(min, max, Math.max(100, totalWidth - gutter));
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

  const onAddBracket = () => {
    const selectedTaskIds = selection.filter((s) => s.kind === 'task').map((s) => s.id);
    const tasks = doc.tasks.filter((t) => selectedTaskIds.includes(t.id));
    if (tasks.length === 0) {
      const today = todayISO();
      addBracket({ id: newId('br'), label: 'Phase', start: today, end: addDays(today, 14), rowIds: rows.length > 0 ? [rows[0].id] : [] });
      return;
    }
    const start = tasks.reduce((a, t) => (t.start < a ? t.start : a), tasks[0].start);
    const end = tasks.reduce((a, t) => (t.end > a ? t.end : a), tasks[0].end);
    const rowIds = Array.from(new Set(tasks.map((t) => t.rowId)));
    addBracket({ id: newId('br'), label: 'Phase', start, end, rowIds });
  };

  const onAddDeadline = () => {
    addMarker({ id: newId('mk'), type: 'deadline', label: 'Deadline', date: todayISO() });
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
          style={{ marginLeft: 16, width: 240, background: 'transparent', border: 'none' }}
        />
      </div>
      <div className="toolbar">
        <button onClick={onAddTask} title="Add task (N)" className="icon-btn label-hideable">
          <Plus size={14} /> <span>Task</span>
        </button>
        <button onClick={onAddMilestone} title="Add milestone (M)" className="icon-btn label-hideable">
          <Diamond size={14} /> <span>Milestone</span>
        </button>
        <button onClick={onAddBracket} title="Add bracket from selection (B)" className="icon-btn label-hideable">
          <BracketIcon size={14} /> <span>Bracket</span>
        </button>
        <button onClick={onAddDeadline} title="Add deadline marker" className="icon-btn label-hideable">
          <Flag size={14} /> <span>Deadline</span>
        </button>
        <button onClick={onAddRow} title="Add row" className="icon-btn label-hideable">
          <Rows3 size={14} /> <span>Row</span>
        </button>
        <button
          onClick={() => setSnap(!snapEnabled)}
          title="Toggle snap to scale (S)"
          className="icon-btn label-hideable"
          style={{ background: snapEnabled ? 'var(--color-accent-soft)' : undefined, color: snapEnabled ? 'white' : undefined }}
        >
          <Magnet size={14} /> <span>Snap</span>
        </button>
        <button onClick={onFit} title="Fit to data (Home)" className="icon-btn label-hideable">
          <Maximize2 size={14} /> <span>Fit</span>
        </button>
        <button onClick={undo} disabled={!canUndo} title="Undo (Ctrl+Z)" className="icon-btn">
          <Undo2 size={14} />
        </button>
        <button onClick={redo} disabled={!canRedo} title="Redo (Ctrl+Shift+Z)" className="icon-btn">
          <Redo2 size={14} />
        </button>
        <button
          onClick={() => window.dispatchEvent(new KeyboardEvent('keydown', { key: '?', shiftKey: true }))}
          title="Keyboard shortcuts (?)"
          className="icon-btn"
        >
          <HelpCircle size={14} />
        </button>
        <ExportMenu />
      </div>
    </header>
  );
}
