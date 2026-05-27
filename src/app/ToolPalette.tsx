import { useToolStore, type Tool } from '../stores/toolStore';
import { useViewportStore } from '../stores/viewportStore';
import { useDocumentStore } from '../stores/documentStore';
import { MousePointer2, Plus, Diamond, Hand, BoxSelect, ZoomIn, ZoomOut, Maximize2 } from 'lucide-react';
import type { TimeScale } from '../types/document';

const TOOLS: Array<{ id: Tool; label: string; icon: typeof MousePointer2; shortcut: string }> = [
  { id: 'select', label: 'Select', icon: MousePointer2, shortcut: 'V' },
  { id: 'add-task', label: 'Add task', icon: Plus, shortcut: 'T' },
  { id: 'add-milestone', label: 'Add milestone', icon: Diamond, shortcut: 'Y' },
  { id: 'marquee', label: 'Marquee select', icon: BoxSelect, shortcut: 'R' },
  { id: 'pan', label: 'Pan', icon: Hand, shortcut: 'H' },
];

const SCALES: TimeScale[] = ['day', 'week', 'month', 'quarter', 'year'];
const SCALE_LABEL: Record<TimeScale, string> = { day: 'D', week: 'W', month: 'M', quarter: 'Q', year: 'Y' };

export function ToolPalette() {
  const activeTool = useToolStore((s) => s.activeTool);
  const setTool = useToolStore((s) => s.setTool);
  const scale = useDocumentStore((s) => s.doc.calendar.scale);
  const setScale = useDocumentStore((s) => s.setScale);
  const zoom = useViewportStore((s) => s.zoom);
  const pxPerDay = useViewportStore((s) => s.pxPerDay);
  const fit = useViewportStore((s) => s.fit);
  const doc = useDocumentStore((s) => s.doc);

  const onFit = () => {
    const dates = [...doc.tasks.flatMap((t) => [t.start, t.end]), ...doc.milestones.map((m) => m.date)];
    if (dates.length === 0) return;
    const min = dates.reduce((a, b) => (a < b ? a : b));
    const max = dates.reduce((a, b) => (a > b ? a : b));
    const chartArea = document.querySelector('.chart-area');
    const totalWidth = chartArea?.clientWidth ?? 800;
    const gutter = totalWidth < 600 ? 120 : 200;
    fit(min, max, Math.max(100, totalWidth - gutter));
  };

  return (
    <div className="tool-palette" role="toolbar" aria-label="Chart tools">
      <div className="tool-palette__group">
        {TOOLS.map((t) => {
          const Icon = t.icon;
          const active = activeTool === t.id;
          return (
            <button
              key={t.id}
              className={`tool-palette__btn ${active ? 'is-active' : ''}`}
              onClick={() => setTool(t.id, true)}
              aria-pressed={active}
              title={`${t.label} (${t.shortcut})`}
            >
              <Icon size={16} strokeWidth={1.75} />
            </button>
          );
        })}
      </div>

      <div className="tool-palette__divider" />

      <div className="tool-palette__group">
        {SCALES.map((s) => (
          <button
            key={s}
            className={`tool-palette__btn tool-palette__scale ${scale === s ? 'is-active' : ''}`}
            onClick={() => setScale(s)}
            title={`Scale: ${s[0].toUpperCase() + s.slice(1)}`}
            aria-pressed={scale === s}
          >
            <span className="tool-palette__scale-label">{SCALE_LABEL[s]}</span>
          </button>
        ))}
      </div>

      <div className="tool-palette__divider" />

      <div className="tool-palette__group">
        <button
          className="tool-palette__btn"
          onClick={() => zoom(0.8, 400)}
          title="Zoom out (−)"
          aria-label="Zoom out"
        >
          <ZoomOut size={16} strokeWidth={1.75} />
        </button>
        <div className="tool-palette__zoom-readout" title="Pixels per day">
          {pxPerDay >= 1 ? `${pxPerDay.toFixed(1)}px/d` : `${(1 / pxPerDay).toFixed(0)}d/px`}
        </div>
        <button
          className="tool-palette__btn"
          onClick={() => zoom(1.25, 400)}
          title="Zoom in (+)"
          aria-label="Zoom in"
        >
          <ZoomIn size={16} strokeWidth={1.75} />
        </button>
        <button
          className="tool-palette__btn"
          onClick={onFit}
          title="Fit to data (Home)"
          aria-label="Fit"
        >
          <Maximize2 size={16} strokeWidth={1.75} />
        </button>
      </div>
    </div>
  );
}
