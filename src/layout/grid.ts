import type { GridDelimiter, ISODate } from '../types/document';
import {
  addDays,
  addMonths,
  diffDays,
  isWeekend,
  startOfMonth,
  startOfQuarter,
  startOfWeek,
} from '../utils/dates';

/**
 * The grid behind the bars: where the delimiters fall, and which stretches of
 * days are not worked.
 *
 * Pure geometry, in `layout/` rather than beside the axis component, because
 * the standalone embed renderer needs the same two answers and must not pull a
 * React component tree in to get them — SRS-TAX-054 and SRS-TAX-056 apply to
 * an exported chart exactly as they do to the app (SRS-TAX-059).
 */

/**
 * Below this, a single day is narrower than a hairline and the mask degrades
 * into noise — adjacent bands touch and the whole plot greys over.
 */
export const WEEKEND_MIN_PX_PER_DAY = 1.5;

/**
 * Below this, the chosen unit is not drawn at all (SRS-TAX-055).
 *
 * `pxPerDay` goes down to 0.05, where day boundaries stand a twentieth of a
 * pixel apart: the plot becomes a solid block and the frame carries thousands
 * of nodes. Falling back to a coarser unit would be worse than drawing
 * nothing, because it would make the setting zoom-derived — the behaviour the
 * owner explicitly rejected.
 */
export const DELIMITER_MIN_PX = 4;

/** A stop, so a pathological zoom cannot spin the generator. */
const MAX_DELIMITERS = 2000;

/** Nominal days per step, used only to decide whether a unit is drawable. */
const NOMINAL_DAYS: Record<Exclude<GridDelimiter, 'none'>, number> = {
  day: 1,
  week: 7,
  month: 28,
  quarter: 90,
};

/**
 * The x of every boundary of `granularity` across the visible width
 * (SRS-TAX-054), in px from the left edge of the plot.
 *
 * Boundaries come from the calendar, not from the view edge, so a rule falls
 * on the first of the month however the window happens to be scrolled — the
 * same rule SRS-TAX-020 already imposes on the axis ticks above.
 */
export function delimiterXs(
  granularity: GridDelimiter,
  viewStart: ISODate,
  pxPerDay: number,
  widthPx: number,
  fiscalYearStart = 1,
): number[] {
  if (granularity === 'none') return [];
  if (NOMINAL_DAYS[granularity] * pxPerDay < DELIMITER_MIN_PX) return [];

  const step = (iso: ISODate): ISODate =>
    granularity === 'day' ? addDays(iso, 1)
    : granularity === 'week' ? addDays(iso, 7)
    : granularity === 'month' ? addMonths(iso, 1)
    : addMonths(iso, 3);

  // Start on the boundary at or before the view edge, so the first rule inside
  // the window is a real one rather than a rule drawn at x=0 by construction.
  let iso =
    granularity === 'day' ? viewStart
    : granularity === 'week' ? startOfWeek(viewStart)
    : granularity === 'month' ? startOfMonth(viewStart)
    : startOfQuarter(viewStart, fiscalYearStart);

  const xs: number[] = [];
  for (let guard = 0; guard < MAX_DELIMITERS; guard++) {
    const x = diffDays(viewStart, iso) * pxPerDay;
    if (x > widthPx) break;
    if (x >= 0) xs.push(x);
    iso = step(iso);
  }
  return xs;
}

export interface GridBand {
  x: number;
  w: number;
}

/**
 * One band per RUN of consecutive non-working days (SRS-TAX-056).
 *
 * A rect per day drew a visible seam down the middle of every weekend, because
 * two abutting translucent fills double up where they meet at a fractional
 * pixel. Runs also halve the node count on a wide view.
 */
export function nonWorkingBands(
  viewStart: ISODate,
  pxPerDay: number,
  widthPx: number,
  workingDays: number[],
): GridBand[] {
  if (pxPerDay < WEEKEND_MIN_PX_PER_DAY) return [];
  // Every day working is a valid, and common, setting: say so cheaply rather
  // than walking the window to discover it.
  if (workingDays.length >= 7) return [];

  const bands: GridBand[] = [];
  const days = Math.ceil(widthPx / pxPerDay) + 1;
  let runStart = -1;
  for (let i = 0; i <= days; i++) {
    const off = i < days && isWeekend(addDays(viewStart, i), workingDays);
    if (off && runStart < 0) runStart = i;
    if (!off && runStart >= 0) {
      bands.push({ x: runStart * pxPerDay, w: (i - runStart) * pxPerDay });
      runStart = -1;
    }
  }
  return bands;
}
