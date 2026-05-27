import { useDocumentStore } from '../stores/documentStore';
import { useViewportStore } from '../stores/viewportStore';
import { useEditStore } from '../stores/editStore';
import {
  Undo2,
  Redo2,
  Plus,
  Maximize2,
  Diamond,
  Magnet,
  Rows3,
  Brackets as BracketIcon,
  Flag,
  HelpCircle,
} from 'lucide-react';
import { useSelectionStore } from '../stores/selectionStore';
import { newId } from '../utils/ids';
import { todayISO, addDays } from '../utils/dates';
import { ExportMenu } from './ExportMenu';
import { BrandLogo } from './BrandLogo';

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
      addBracket({
        id: newId('br'),
        label: 'Phase',
        start: today,
        end: addDays(today, 14),
        rowIds: rows.length > 0 ? [rows[0].id] : [],
      });
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
        <BrandLogo variant="full" size={22} />
        <div className="brand__sep" aria-hidden="true" />
        <TitleChip title={title} onChange={setTitle} />
      </div>

      <div className="toolbar">
        <Cluster>
          <IconBtn onClick={onAddTask} title="Add task (T)" icon={<Plus size={14} strokeWidth={1.75} />} label="Task" />
          <IconBtn
            onClick={onAddMilestone}
            title="Add milestone (Y)"
            icon={<Diamond size={14} strokeWidth={1.75} />}
            label="Milestone"
          />
          <IconBtn
            onClick={onAddBracket}
            title="Add bracket from selection (B)"
            icon={<BracketIcon size={14} strokeWidth={1.75} />}
            label="Bracket"
          />
          <IconBtn
            onClick={onAddDeadline}
            title="Add deadline marker"
            icon={<Flag size={14} strokeWidth={1.75} />}
            label="Deadline"
          />
          <IconBtn onClick={onAddRow} title="Add row" icon={<Rows3 size={14} strokeWidth={1.75} />} label="Row" />
        </Cluster>

        <Cluster>
          <IconBtn
            onClick={() => setSnap(!snapEnabled)}
            title="Toggle snap to scale (S)"
            icon={<Magnet size={14} strokeWidth={1.75} />}
            label="Snap"
            active={snapEnabled}
          />
          <IconBtn onClick={onFit} title="Fit to data (Home)" icon={<Maximize2 size={14} strokeWidth={1.75} />} label="Fit" />
        </Cluster>

        <Cluster>
          <IconBtn
            onClick={undo}
            disabled={!canUndo}
            title="Undo (Ctrl+Z)"
            icon={<Undo2 size={14} strokeWidth={1.75} />}
          />
          <IconBtn
            onClick={redo}
            disabled={!canRedo}
            title="Redo (Ctrl+Shift+Z)"
            icon={<Redo2 size={14} strokeWidth={1.75} />}
          />
        </Cluster>

        <IconBtn
          onClick={() => window.dispatchEvent(new KeyboardEvent('keydown', { key: '?', shiftKey: true }))}
          title="Keyboard shortcuts (?)"
          icon={<HelpCircle size={14} strokeWidth={1.75} />}
        />
        <ExportMenu />
      </div>
    </header>
  );
}

/* ------------------------------------------------------------------ */
/* sub-components                                                      */
/* ------------------------------------------------------------------ */

function TitleChip({ title, onChange }: { title: string; onChange: (v: string) => void }) {
  return (
    <input
      aria-label="Chart title"
      value={title}
      onChange={(e) => onChange(e.target.value)}
      placeholder="Untitled plan"
      className="title-chip"
    />
  );
}

function Cluster({ children }: { children: React.ReactNode }) {
  return <div className="app-header__cluster">{children}</div>;
}

interface IconBtnProps {
  onClick?: () => void;
  title?: string;
  icon: React.ReactNode;
  label?: string;
  disabled?: boolean;
  active?: boolean;
}

function IconBtn({ onClick, title, icon, label, disabled, active }: IconBtnProps) {
  return (
    <button
      onClick={onClick}
      disabled={disabled}
      title={title}
      aria-pressed={active}
      className={`app-header__btn ${label ? 'label-hideable' : ''} ${active ? 'is-active' : ''}`}
    >
      {icon}
      {label && <span>{label}</span>}
    </button>
  );
}
