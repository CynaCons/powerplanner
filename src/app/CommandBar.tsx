import { useState } from 'react';
import {
  MousePointer2, Plus, Diamond, Hand, BoxSelect,
  ZoomIn, ZoomOut, Maximize2, Settings2, Rows3, Flag, Brackets as BracketIcon,
} from 'lucide-react';
import { useToolStore, type Tool } from '../stores/toolStore';
import { useViewportStore } from '../stores/viewportStore';
import { useDocumentStore } from '../stores/documentStore';
import { useSelectionStore } from '../stores/selectionStore';
import { newId } from '../utils/ids';
import { SelectionGroup } from './SelectionGroup';
import { todayISO, addDays } from '../utils/dates';
import type { TimeScale, ThemeName } from '../types/document';

/**
 * The contextual command bar (PLAN.md Phase 16 / v2.13.1).
 *
 * Replaces the tool palette. Mirrors the native add-in's R8 anatomy
 * (`docs/onslide-experience-spec.md`): the **left group swaps with the
 * selection**, the **right group is always the global one**. The right group
 * never moving is what makes a contextual bar learnable rather than jumpy.
 *
 * Decisions taken from the interactive prototype
 * (`docs/mockup/web-command-bar-mockup.html`, user 2026-08-07):
 *  - floating dock, hovering over the chart
 *  - tools shown only when nothing is selected
 *  - a gear opens a document context, clearing the selection
 *
 * C1 builds the shell and the document context. Per-selection editing contents
 * land in C2; until then the left group names what is selected.
 */

const TOOLS: Array<{ id: Tool; label: string; icon: typeof MousePointer2; shortcut: string }> = [
  { id: 'select', label: 'Select', icon: MousePointer2, shortcut: 'V' },
  { id: 'marquee', label: 'Marquee select', icon: BoxSelect, shortcut: 'R' },
  { id: 'pan', label: 'Pan', icon: Hand, shortcut: 'H' },
];

const SCALES: TimeScale[] = ['day', 'week', 'month', 'quarter', 'year'];
const SCALE_LABEL: Record<TimeScale, string> = { day: 'D', week: 'W', month: 'M', quarter: 'Q', year: 'Y' };
const THEMES: { id: ThemeName; label: string }[] = [
  { id: 'dark', label: 'Dark' },
  { id: 'light', label: 'Light' },
  { id: 'print', label: 'Print' },
];
const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

export function CommandBar() {
  const [docContext, setDocContext] = useState(false);

  const activeTool = useToolStore((s) => s.activeTool);
  const setTool = useToolStore((s) => s.setTool);
  const scale = useDocumentStore((s) => s.doc.calendar.scale);
  const setScale = useDocumentStore((s) => s.setScale);
  const zoom = useViewportStore((s) => s.zoom);
  const pxPerDay = useViewportStore((s) => s.pxPerDay);
  const fit = useViewportStore((s) => s.fit);
  const doc = useDocumentStore((s) => s.doc);
  const selection = useSelectionStore((s) => s.items);
  const clearSelection = useSelectionStore((s) => s.clear);

  const addTask = useDocumentStore((s) => s.addTask);
  const addMilestone = useDocumentStore((s) => s.addMilestone);
  const addRow = useDocumentStore((s) => s.addRow);
  const addMarker = useDocumentStore((s) => s.addMarker);
  const addBracket = useDocumentStore((s) => s.addBracket);

  const onFit = () => {
    const dates = [...doc.tasks.flatMap((t) => [t.start, t.end]), ...doc.milestones.map((m) => m.date)];
    if (dates.length === 0) return;
    const min = dates.reduce((a, b) => (a < b ? a : b));
    const max = dates.reduce((a, b) => (a > b ? a : b));
    const totalWidth = document.querySelector('.chart-area')?.clientWidth ?? 800;
    fit(min, max, Math.max(100, totalWidth - (totalWidth < 600 ? 120 : 200)));
  };

  /** The gear clears the selection: a document context and a selected item
   *  cannot both own the left side of the bar. */
  const toggleDocContext = () => {
    const next = !docContext;
    setDocContext(next);
    if (next) clearSelection();
  };

  // Selecting anything leaves the document context — the bar follows the
  // user's attention rather than trapping them in a mode.
  if (docContext && selection.length > 0) setDocContext(false);

  const idle = selection.length === 0 && !docContext;

  return (
    <div className="cmdbar" role="toolbar" aria-label="Command bar">
      {docContext ? (
        <DocumentGroup />
      ) : selection.length > 0 ? (
        <SelectionGroup />
      ) : (
        <>
          {/* Tools only when idle (user choice). Pan therefore needs a
              deselect first, which was accepted as a trade for a shorter bar. */}
          <Group>
            {TOOLS.map((t) => {
              const Icon = t.icon;
              return (
                <BarButton
                  key={t.id}
                  active={activeTool === t.id}
                  onClick={() => setTool(t.id, true)}
                  title={`${t.label} (${t.shortcut})`}
                >
                  <Icon size={15} strokeWidth={1.75} />
                </BarButton>
              );
            })}
          </Group>
          <Group label="Insert">
            <BarButton onClick={() => addRow({ id: newId('row'), label: `Row ${doc.rows.length + 1}`, groupId: null })} title="Add row">
              <Rows3 size={14} strokeWidth={1.75} /><span>Row</span>
            </BarButton>
            <BarButton
              disabled={doc.rows.length === 0}
              onClick={() => {
                const today = todayISO();
                addTask({ id: newId('task'), rowId: doc.rows[0].id, label: 'New task', start: today, end: addDays(today, 7), percentComplete: 0 });
              }}
              title="Add task (T)"
            >
              <Plus size={14} strokeWidth={1.75} /><span>Task</span>
            </BarButton>
            <BarButton
              disabled={doc.rows.length === 0}
              onClick={() => addMilestone({ id: newId('ms'), rowId: doc.rows[0].id, label: 'New milestone', date: todayISO() })}
              title="Add milestone (Y)"
            >
              <Diamond size={14} strokeWidth={1.75} /><span>Milestone</span>
            </BarButton>
            <BarButton
              onClick={() => addMarker({ id: newId('mk'), type: 'deadline', label: 'Deadline', date: todayISO() })}
              title="Add deadline marker"
            >
              <Flag size={14} strokeWidth={1.75} /><span>Marker</span>
            </BarButton>
            <BarButton
              onClick={() => {
                const today = todayISO();
                addBracket({ id: newId('br'), label: 'Phase', start: today, end: addDays(today, 14), rowIds: doc.rows.length ? [doc.rows[0].id] : [] });
              }}
              title="Add bracket"
            >
              <BracketIcon size={14} strokeWidth={1.75} /><span>Bracket</span>
            </BarButton>
          </Group>
        </>
      )}

      {/* Global group — pinned. Never moves as the selection changes. */}
      <div className="cmdbar__group cmdbar__group--right">
        <span className="cmdbar__group-label">Scale</span>
        <div className="cmdbar__scale">
          {SCALES.map((s) => (
            <button
              key={s}
              className={scale === s ? 'is-active' : ''}
              onClick={() => setScale(s)}
              aria-pressed={scale === s}
              title={`Scale: ${s[0].toUpperCase() + s.slice(1)}`}
            >
              {SCALE_LABEL[s]}
            </button>
          ))}
        </div>
        <BarButton onClick={() => zoom(0.8, 400)} title="Zoom out (−)" ariaLabel="Zoom out">
          <ZoomOut size={15} strokeWidth={1.75} />
        </BarButton>
        <span className="cmdbar__zoom" title="Pixels per day">
          {pxPerDay >= 1 ? `${pxPerDay.toFixed(1)}px/d` : `${(1 / pxPerDay).toFixed(0)}d/px`}
        </span>
        <BarButton onClick={() => zoom(1.25, 400)} title="Zoom in (+)" ariaLabel="Zoom in">
          <ZoomIn size={15} strokeWidth={1.75} />
        </BarButton>
        <BarButton onClick={onFit} title="Fit to data (Home)" ariaLabel="Fit">
          <Maximize2 size={15} strokeWidth={1.75} />
        </BarButton>
        <BarButton
          active={docContext}
          onClick={toggleDocContext}
          title={docContext ? 'Close document settings' : 'Document settings — clears the selection'}
          ariaLabel="Document settings"
        >
          <Settings2 size={15} strokeWidth={1.75} />
        </BarButton>
      </div>

      {idle && (
        <span className="cmdbar__hint">Select for quick actions · right-click for the rest</span>
      )}
    </div>
  );
}

/* ------------------------------------------------------------------ */

function DocumentGroup() {
  const theme = useDocumentStore((s) => s.doc.style.theme);
  const setTheme = useDocumentStore((s) => s.setTheme);
  const fiscalYearStart = useDocumentStore((s) => s.doc.calendar.fiscalYearStart);
  const setFiscalYearStart = useDocumentStore((s) => s.setFiscalYearStart);

  return (
    <Group label="Document">
      <span className="cmdbar__field-label">Theme</span>
      <div className="cmdbar__scale" role="radiogroup" aria-label="Theme">
        {THEMES.map((t) => (
          <button
            key={t.id}
            role="radio"
            aria-checked={theme === t.id}
            className={theme === t.id ? 'is-active' : ''}
            onClick={() => setTheme(t.id)}
          >
            {t.label}
          </button>
        ))}
      </div>
      <span className="cmdbar__field-label">Fiscal year starts</span>
      <select
        className="cmdbar__select"
        aria-label="Fiscal year starts"
        value={fiscalYearStart}
        onChange={(e) => setFiscalYearStart(Number(e.target.value))}
      >
        {MONTHS.map((label, i) => (
          <option key={label} value={i + 1}>{label}</option>
        ))}
      </select>
      <WorkingDays />
    </Group>
  );
}

const DOW = [
  { day: 1, label: 'M' },
  { day: 2, label: 'T' },
  { day: 3, label: 'W' },
  { day: 4, label: 'T' },
  { day: 5, label: 'F' },
  { day: 6, label: 'S' },
  { day: 0, label: 'S' },
];

/**
 * Working days drive the weekend shading, so until now a plan could be shaded
 * wrong with no way to correct it from the UI — the agent bridge could set it
 * and a person could not (SRS-ABR-019, owner directive 2026-10-23).
 */
function WorkingDays() {
  const workingDays = useDocumentStore((s) => s.doc.calendar.workingDays);
  const setWorkingDays = useDocumentStore((s) => s.setWorkingDays);

  const toggle = (day: number) => {
    const next = workingDays.includes(day)
      ? workingDays.filter((d) => d !== day)
      : [...workingDays, day];
    setWorkingDays(next);
  };

  return (
    <>
      <span className="cmdbar__field-label">Working days</span>
      <div className="cmdbar__scale" role="group" aria-label="Working days">
        {DOW.map(({ day, label }) => (
          <button
            key={day}
            aria-pressed={workingDays.includes(day)}
            aria-label={`${FULL_DOW[day]}, ${workingDays.includes(day) ? 'a working day' : 'not a working day'}`}
            className={workingDays.includes(day) ? 'is-active' : ''}
            onClick={() => toggle(day)}
          >
            {label}
          </button>
        ))}
      </div>
    </>
  );
}

const FULL_DOW: Record<number, string> = {
  0: 'Sunday',
  1: 'Monday',
  2: 'Tuesday',
  3: 'Wednesday',
  4: 'Thursday',
  5: 'Friday',
  6: 'Saturday',
};

function Group({ label, children }: { label?: string; children: React.ReactNode }) {
  return (
    <div className="cmdbar__group">
      {label && <span className="cmdbar__group-label">{label}</span>}
      {children}
    </div>
  );
}

interface BarButtonProps {
  children: React.ReactNode;
  onClick?: () => void;
  title?: string;
  ariaLabel?: string;
  active?: boolean;
  disabled?: boolean;
}

function BarButton({ children, onClick, title, ariaLabel, active, disabled }: BarButtonProps) {
  return (
    <button
      className={`cmdbar__btn ${active ? 'is-active' : ''}`}
      onClick={onClick}
      title={title}
      aria-label={ariaLabel}
      aria-pressed={active}
      disabled={disabled}
    >
      {children}
    </button>
  );
}
