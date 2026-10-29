import { useState } from 'react';
import {
  MousePointer2, Plus, Diamond, Hand, BoxSelect,
  ZoomIn, ZoomOut, Maximize2, Settings2, Rows3, Flag, Brackets as BracketIcon,
} from 'lucide-react';
import { useToolStore, type Tool } from '../stores/toolStore';
import { resolveInsertTarget } from './insertTarget';
import type { Selectable } from '../stores/selectionStore';
import { bracketSpanFor } from './bracketSpan';
import { useViewportStore } from '../stores/viewportStore';
import { useDocumentStore } from '../stores/documentStore';
import { useSelectionStore } from '../stores/selectionStore';
import { newId } from '../utils/ids';
import { SelectionGroup } from './SelectionGroup';
import { todayISO, addDays } from '../utils/dates';
import type { TimeScale, ThemeName } from '../types/document';
import { gridDelimiterOf, weekendDisplayOf, type GridDelimiter, type WeekendDisplay } from '../types/document';
import type { DocOp } from '../stores/docOps';

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

/** What the bar calls each armed tool. Select is the resting state and says nothing. */
const ARMED_LABEL: Partial<Record<Tool, string>> = {
  'add-task': 'Placing a task',
  'add-milestone': 'Placing a milestone',
  marquee: 'Marquee select',
  pan: 'Panning',
};

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

  const setSelection = useSelectionStore((s) => s.setSelection);
  const reveal = useViewportStore((s) => s.reveal);
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


  /**
   * The row an insert lands on (SRS-EDIT-028), creating one where the planner
   * has a section selected — a heading is not a place work goes.
   */
  const insertRowId = (): string | null => {
    const target = resolveInsertTarget(doc, selection);
    if (!target) return null;
    if (target.kind === 'row') return target.rowId;
    const id = newId('row');
    addRow({ id, label: 'New row', groupId: target.parentId }, target.index);
    return id;
  };

  /** Insert, select it, and make sure the planner can see it. */
  const landOn = (rowId: string, item: Selectable) => {
    setSelection([item]);
    reveal(rowId);
  };

  return (
    <div className="cmdbar" role="toolbar" aria-label="Command bar">
      {/*
        What is armed, said out loud (SRS-EDIT-027).

        The only cue used to be a crosshair, and the tool row is replaced by the
        selection group the moment the inserted item is selected — so the mode
        was invisible at exactly the moment it was doing damage.
      */}
      {ARMED_LABEL[activeTool] && (
        <span className="cmdbar__armed" data-pp-armed={activeTool} role="status">
          {ARMED_LABEL[activeTool]}
          <span className="cmdbar__armed-esc">Esc</span>
        </span>
      )}
      {docContext ? (
        <DocumentGroup />
      ) : selection.length > 0 ? (
        <SelectionGroup />
      ) : (
          <Group>
            {TOOLS.map((t) => {
              const Icon = t.icon;
              return (
                <BarButton
                  key={t.id}
                  active={activeTool === t.id}
                  // No explicit stickiness: the store decides, so a marquee
                  // cannot become a mode while pan still can (SRS-EDIT-026).
                  onClick={() => setTool(t.id)}
                  title={`${t.label} (${t.shortcut})`}
                >
                  <Icon size={15} strokeWidth={1.75} />
                </BarButton>
              );
            })}
          </Group>
      )}

      {/* Insert stays reachable with something selected (SRS-EDIT-028): the
          planner inserts next to what they are looking at, and having to
          deselect first is what sent every insert to row zero. */}
      {!docContext && (
          <Group label="Insert">
            <BarButton onClick={() => addRow({ id: newId('row'), label: `Row ${doc.rows.length + 1}`, groupId: null })} title="Add row">
              <Rows3 size={14} strokeWidth={1.75} /><span>Row</span>
            </BarButton>
            <BarButton
              disabled={doc.rows.length === 0}
              onClick={() => {
                const rowId = insertRowId();
                if (!rowId) return;
                const today = todayISO();
                const id = newId('task');
                addTask({ id, rowId, label: 'New task', start: today, end: addDays(today, 7), percentComplete: 0 });
                landOn(rowId, { kind: 'task', id });
              }}
              title="Add task (T)"
            >
              <Plus size={14} strokeWidth={1.75} /><span>Task</span>
            </BarButton>
            <BarButton
              disabled={doc.rows.length === 0}
              onClick={() => {
                const rowId = insertRowId();
                if (!rowId) return;
                const id = newId('ms');
                addMilestone({ id, rowId, label: 'New milestone', date: todayISO() });
                landOn(rowId, { kind: 'milestone', id });
              }}
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
                const span = bracketSpanFor(doc, selection);
                addBracket({ id: newId('br'), label: 'Phase', ...span });
              }}
              title="Add bracket over the selection"
            >
              <BracketIcon size={14} strokeWidth={1.75} /><span>Bracket</span>
            </BarButton>
          </Group>
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
        <BarButton onClick={() => zoom(0.8)} title="Zoom out (−)" ariaLabel="Zoom out">
          <ZoomOut size={15} strokeWidth={1.75} />
        </BarButton>
        <span className="cmdbar__zoom" title="Pixels per day">
          {pxPerDay >= 1 ? `${pxPerDay.toFixed(1)}px/d` : `${(1 / pxPerDay).toFixed(0)}d/px`}
        </span>
        <BarButton onClick={() => zoom(1.25)} title="Zoom in (+)" ariaLabel="Zoom in">
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

    </div>
  );
}


/* ------------------------------------------------------------------ */

function DocumentGroup() {
  const theme = useDocumentStore((s) => s.doc.style.theme);
  const setTheme = useDocumentStore((s) => s.setTheme);
  const fiscalYearStart = useDocumentStore((s) => s.doc.calendar.fiscalYearStart);
  const setFiscalYearStart = useDocumentStore((s) => s.setFiscalYearStart);
  const allowOverlap = useDocumentStore((s) => s.doc.style.allowOverlap ?? false);
  const setAllowOverlap = useDocumentStore((s) => s.setAllowOverlap);
  const calendar = useDocumentStore((s) => s.doc.calendar);
  const applyBatch = useDocumentStore((s) => s.applyBatch);
  const gridDelimiter = gridDelimiterOf(calendar);
  const weekendDisplay = weekendDisplayOf(calendar);

  /**
   * Written as operations, which is what an agent sends too (SRS-ABR-025).
   *
   * This used to call `replaceDocument` with a whole new document, and that is
   * how the two settings came to have no operation at all: nothing forced the
   * op vocabulary to keep up with a control that bypassed it. Going through
   * `applyBatch` means a person and an agent now travel one path, so the next
   * setting added here cannot silently become unreachable to an agent. Still
   * one undo frame — that is what a batch is.
   */
  const patchCalendar = (patch: { gridDelimiter?: GridDelimiter; weekendDisplay?: WeekendDisplay }) => {
    const ops: DocOp[] = [];
    if (patch.gridDelimiter !== undefined) ops.push({ op: 'set_grid_delimiter', delimiter: patch.gridDelimiter });
    if (patch.weekendDisplay !== undefined) ops.push({ op: 'set_weekend_display', display: patch.weekendDisplay });
    if (ops.length > 0) applyBatch(ops, 'user');
  };

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
      <span className="cmdbar__field-label">Bars on the same dates</span>
      <div className="cmdbar__scale" role="radiogroup" aria-label="Bars on the same dates">
        {OVERLAP.map((o) => (
          <button
            key={String(o.value)}
            role="radio"
            aria-checked={allowOverlap === o.value}
            className={allowOverlap === o.value ? 'is-active' : ''}
            title={o.title}
            onClick={() => setAllowOverlap(o.value)}
          >
            {o.label}
          </button>
        ))}
      </div>
      <span className="cmdbar__field-label">Delimiters</span>
      <div className="cmdbar__scale" role="radiogroup" aria-label="Grid delimiters">
        {DELIMITER_CHOICES.map((c) => (
          <button
            key={c.value}
            role="radio"
            aria-checked={gridDelimiter === c.value}
            className={gridDelimiter === c.value ? 'is-active' : ''}
            title={c.title}
            onClick={() => patchCalendar({ gridDelimiter: c.value })}
          >
            {c.label}
          </button>
        ))}
      </div>
      <span className="cmdbar__field-label">Weekends</span>
      <div className="cmdbar__scale" role="radiogroup" aria-label="Weekend display">
        {WEEKEND_CHOICES.map((c) => (
          <button
            key={c.value}
            role="radio"
            aria-checked={weekendDisplay === c.value}
            className={weekendDisplay === c.value ? 'is-active' : ''}
            title={c.title}
            onClick={() => patchCalendar({ weekendDisplay: c.value })}
          >
            {c.label}
          </button>
        ))}
      </div>
      <WorkingDays />
    </Group>
  );
}

/**
 * The delimiter granularity (SRS-TAX-054). Five, not the three the design demo
 * shows, because the scale selector already offers quarter and day and a
 * planner who reads at one of those should be able to rule at it too.
 */
const DELIMITER_CHOICES: ReadonlyArray<{ value: GridDelimiter; label: string; title: string }> = [
  { value: 'none', label: 'None', title: 'No vertical rules in the plot' },
  { value: 'day', label: 'Day', title: 'A rule on every day boundary' },
  { value: 'week', label: 'Week', title: 'A rule on every week boundary' },
  { value: 'month', label: 'Month', title: 'A rule on the first of each month' },
  { value: 'quarter', label: 'Qtr', title: 'A rule on each fiscal quarter boundary' },
];

const WEEKEND_CHOICES: ReadonlyArray<{ value: WeekendDisplay; label: string; title: string }> = [
  { value: 'off', label: 'Off', title: 'Non-working days look like any other day' },
  { value: 'shaded', label: 'Shaded', title: 'Non-working days are shaded behind the bars' },
];

/**
 * Stacked or overlapping, the think-cell choice (SRS-LAY-024, SRS-LAY-025).
 *
 * The engine has packed bars into lanes since it was written and has taken an
 * `allowOverlap` argument for just as long, which nothing ever passed. This
 * is the control that reaches it.
 *
 * It used to read "Stack | Overlap", which names the implementation rather
 * than the outcome, with the explanation hidden in a tooltip on a 20px
 * button — "I can change whether the bars are stacked or overlapped. I don’t
 * really understand that." The label now states what happens to the bars and
 * the field label states when it happens, so the choice reads without
 * hovering anything.
 */
export const OVERLAP = [
  { value: false, label: 'Own line', title: 'Two bars that cover the same dates each get their own line' },
  { value: true, label: 'Same line', title: 'Two bars that cover the same dates draw on top of each other' },
] as const;

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
 *
 * The shading is the only thing in the web app this set feeds, and it was
 * invisible at every scale, so the control read as inert: toggling a day
 * changed nothing anyone could see. It is paired with Weekends above now —
 * this picks WHICH days are off, that one decides whether the plot says so
 * (SRS-TAX-056).
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
