import type { AxisLayout } from '../layout/timeAxis';
import type { ISODate, TimeScale } from '../types/document';
import { isoWeekNumber, parseISO } from '../utils/dates';

interface Props {
  axis: AxisLayout;
}

/**
 * The time axis is the chart's strongest graphic element, deliberately.
 *
 * Every Gantt tool renders its axis as grey 10px text, which makes the
 * calendar feel like a caption on the bars. PowerRoadmap's whole claim is that
 * the chart is calendar-true, so the calendar is drawn as the structure the
 * bars hang from: the month is set in ink at reading size, it sits on a ruled
 * baseline that runs the full width, and the weeks drop from that rule as
 * measured ticks. Everything else in the chart stays quiet so this can carry.
 * (PLAN.md v2.16 — owner report 2026-10-23.)
 */
export function TimeAxis({ axis }: Props) {
  const majorH = axis.major.height;
  const minorH = axis.minor.height;
  const microH = axis.micro?.height ?? 0;

  return (
    <g>
      {/* Month band */}
      <rect x={0} y={0} width={10000} height={majorH} fill="var(--color-bg-panel)" />
      {axis.major.ticks.map((t, i) => (
        <g key={`maj-${i}`} transform={`translate(${t.x} 0)`}>
          {/* The month division is a short riser, not a full-height rule: the
              baseline below already carries the structure, and two competing
              rules read as a table. */}
          <line x1={0} y1={majorH - 14} x2={0} y2={majorH} stroke="var(--color-text)" strokeWidth={1} />
          <text x={8} y={majorH - 10} fill="var(--color-text)" fontSize={15} fontWeight={600}>
            {t.label}
          </text>
        </g>
      ))}
      {/* The ruled baseline. The one heavy line in the chart. */}
      <line x1={0} y1={majorH} x2={10000} y2={majorH} stroke="var(--color-text)" strokeWidth={1.5} />

      {/* Week band — ticks drop from the rule, labels sit clear of them */}
      <g transform={`translate(0 ${majorH})`}>
        <rect x={0} y={0} width={10000} height={minorH} fill="var(--color-bg)" />
        {axis.minor.ticks.map((t, i) => (
          <g key={`min-${i}`} transform={`translate(${t.x} 0)`}>
            <line x1={0} y1={0} x2={0} y2={6} stroke="var(--color-text-dim)" strokeWidth={1} />
            <text x={4} y={minorH - 5} fill="var(--color-text-soft)" fontSize={11}>
              {t.label}
            </text>
          </g>
        ))}
      </g>

      {/* Day numbers, when the scale is fine enough to show them */}
      {axis.micro && (
        <g transform={`translate(0 ${majorH + minorH})`}>
          <rect x={0} y={0} width={10000} height={microH} fill="var(--color-bg)" />
          {axis.micro.ticks.map((t, i) => (
            <text key={`mic-${i}`} x={t.x + 2} y={microH - 4} fill="var(--color-text-dim)" fontSize={10}>
              {t.label}
            </text>
          ))}
        </g>
      )}
    </g>
  );
}

/* ------------------------------------------------------------------ *
 * Delimiters, the weekend mask, and the two pills.
 *
 * These belong to the axis even though they are painted in the plot: they are
 * the calendar projected downwards through the chart. Keeping them here keeps
 * one module answerable for where a date lands on screen.
 * ------------------------------------------------------------------ */


/**
 * The grid helpers moved to `layout/grid` so the embed renderer can share them
 * (SRS-TAX-059). Re-exported here because the axis is where callers look for
 * them, and the app imports both the component and the geometry from this file.
 */
export {
  DELIMITER_MIN_PX,
  WEEKEND_MIN_PX_PER_DAY,
  delimiterXs,
  nonWorkingBands,
  type GridBand,
} from '../layout/grid';

const DOW_SHORT = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
const MONTHS_SHORT = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

/**
 * The date a drag is about to commit, said in the idiom of the scale the
 * planner is reading (SRS-TAX-058).
 *
 * Always a whole date, never a period name: `snapDelta` quantises the delta to
 * whole days or whole weeks, so there is always an exact day to report, and
 * "Nov 2026" would hide the very value the pill exists to disclose.
 */
export function formatForScale(iso: ISODate, scale: TimeScale): string {
  const d = parseISO(iso);
  const dom = d.getUTCDate();
  const mon = MONTHS_SHORT[d.getUTCMonth()];
  switch (scale) {
    case 'day':
      // The weekday is the thing you are navigating by on a day axis.
      return `${DOW_SHORT[d.getUTCDay()]} ${dom} ${mon}`;
    case 'week':
      // The axis labels weeks as W{n} (SRS-TAX-023); the pill speaks that too.
      return `${dom} ${mon} · W${isoWeekNumber(iso)}`;
    default:
      return `${dom} ${mon} ${d.getUTCFullYear()}`;
  }
}

export interface DragPill {
  /** In the SVG's own coordinates — the pill is drawn outside every transform. */
  x: number;
  y: number;
  text: string;
}

/** Roughly the advance of the 11px UI face, enough to size a box around a date. */
const PILL_CHAR_PX = 6.4;

/**
 * The live date readout that follows a date drag (SRS-TAX-058, SR-IXC-03/04).
 *
 * Drawn above the pointer, not under it: the cursor and the hand holding the
 * mouse are both below, and a readout there is the one place it cannot be read.
 */
export function DragDatePill({ pill, minX, maxX }: { pill: DragPill; minX: number; maxX: number }) {
  const w = Math.round(pill.text.length * PILL_CHAR_PX + 18);
  const half = w / 2;
  // Held inside the plot, so a drag that runs to either edge does not take the
  // readout off the sheet with it.
  const cx = Math.max(minX + half, Math.min(maxX - half, pill.x));
  const y = pill.y - 30;
  return (
    <g pointerEvents="none">
      <rect className="axis-drag-pill" x={cx - half} y={y} width={w} height={20} rx={10} />
      <text className="axis-drag-pill-text" x={cx} y={y + 14} textAnchor="middle" fontSize={11} fontWeight={600}>
        {pill.text}
      </text>
    </g>
  );
}
