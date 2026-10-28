import type { ISODate, TimeScale } from '../types/document';
import {
  addDays,
  addMonths,
  formatISO,
  parseISO,
  startOfMonth,
  startOfQuarter,
  startOfWeek,
  startOfYear,
  diffDays,
  fiscalQuarterOf,
  isoWeekNumber,
} from '../utils/dates';

export interface AxisTick {
  iso: ISODate;
  label: string;
  x: number;
}

export interface AxisLevel {
  ticks: AxisTick[];
  height: number;
}

export interface AxisLayout {
  major: AxisLevel;
  minor: AxisLevel;
  micro?: AxisLevel;
}

const MONTH_NAMES = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

export function dateToX(iso: ISODate, viewStart: ISODate, pxPerDay: number): number {
  return diffDays(viewStart, iso) * pxPerDay;
}

export function xToDate(x: number, viewStart: ISODate, pxPerDay: number): ISODate {
  return addDays(viewStart, Math.round(x / pxPerDay));
}

export function dateToXFractional(iso: ISODate, viewStart: ISODate, pxPerDay: number): number {
  return diffDays(viewStart, iso) * pxPerDay;
}

export function visibleEnd(viewStart: ISODate, pxPerDay: number, widthPx: number): ISODate {
  const days = Math.ceil(widthPx / pxPerDay) + 1;
  return addDays(viewStart, days);
}

function thinTicks(level: AxisLevel, minSpacingPx: number): AxisLevel {
  if (level.ticks.length <= 1) return level;
  const kept: AxisTick[] = [level.ticks[0]];
  for (let i = 1; i < level.ticks.length; i++) {
    if (level.ticks[i].x - kept[kept.length - 1].x >= minSpacingPx) {
      kept.push(level.ticks[i]);
    }
  }
  return { ticks: kept, height: level.height };
}

export function buildAxis(
  viewStart: ISODate,
  pxPerDay: number,
  widthPx: number,
  scale: TimeScale,
  fiscalYearStart: number,
): AxisLayout {
  const end = visibleEnd(viewStart, pxPerDay, widthPx);
  let result: AxisLayout;

  switch (scale) {
    case 'day':
      result = {
        major: monthTicks(viewStart, end, pxPerDay),
        minor: weekTicks(viewStart, end, pxPerDay),
        micro: dayTicks(viewStart, end, pxPerDay),
      };
      break;
    case 'week':
      result = {
        major: monthTicks(viewStart, end, pxPerDay),
        minor: weekTicks(viewStart, end, pxPerDay),
      };
      break;
    case 'month':
      result = {
        major: quarterTicks(viewStart, end, pxPerDay, fiscalYearStart),
        minor: monthTicks(viewStart, end, pxPerDay),
      };
      break;
    case 'quarter':
      result = {
        major: yearTicks(viewStart, end, pxPerDay, fiscalYearStart),
        minor: quarterTicks(viewStart, end, pxPerDay, fiscalYearStart),
      };
      break;
    case 'year':
      result = {
        major: yearTicks(viewStart, end, pxPerDay, fiscalYearStart),
        minor: yearTicks(viewStart, end, pxPerDay, fiscalYearStart),
      };
      break;
  }

  // Skip overlapping labels based on actual pxPerDay
  result.major = thinTicks(result.major, 60);
  result.minor = thinTicks(result.minor, 36);
  if (result.micro) {
    result.micro = thinTicks(result.micro, 16);
  }

  // The year goes on last, once we know which month ticks actually survive
  // and where they sit (SRS-TAX-025).
  if (scale === 'day' || scale === 'week') {
    result.major = withYears(result.major);
  } else if (scale === 'month') {
    result.minor = withYears(result.minor);
  }
  return result;
}

/**
 * Add the year to the month ticks that need it, and only those.
 *
 * Two rules, both about what a reader can actually see:
 *  - the first tick whose LABEL lands on screen carries the year, so the view
 *    always states which year it is. The leading tick is generated at the
 *    start of its month, which is usually off the left edge, and its label
 *    goes with it.
 *  - any tick whose year differs from the previous surviving one carries it,
 *    so the turn of the year is always marked even when thinning removed
 *    January itself. A two-year view at week scale otherwise read
 *    "Oct · Dec · Feb · Apr" with nothing to say which February.
 */
function withYears(level: AxisLevel): AxisLevel {
  // TimeAxis draws a major label at x + 8; anything further left is cut off.
  const LABEL_PAD = 8;
  let previousYear: number | null = null;
  let statedOnce = false;
  const ticks = level.ticks.map((t) => {
    const year = parseISO(t.iso).getUTCFullYear();
    const readable = t.x + LABEL_PAD >= 0;
    const turnsTheYear = previousYear !== null && year !== previousYear;
    const show = turnsTheYear || (readable && !statedOnce);
    previousYear = year;
    if (show) statedOnce = true;
    return show ? { ...t, label: `${t.label} ${year}` } : t;
  });
  // Every tick is off the left edge — a view narrower than one month. State it
  // on the last one rather than nowhere.
  if (!statedOnce && ticks.length > 0) {
    const last = ticks[ticks.length - 1];
    ticks[ticks.length - 1] = { ...last, label: `${last.label} ${parseISO(last.iso).getUTCFullYear()}` };
  }
  return { ticks, height: level.height };
}

function dayTicks(start: ISODate, end: ISODate, pxPerDay: number): AxisLevel {
  const ticks: AxisTick[] = [];
  let cursor = start;
  while (parseISO(cursor) <= parseISO(end)) {
    const d = parseISO(cursor);
    ticks.push({ iso: cursor, label: String(d.getUTCDate()), x: dateToX(cursor, start, pxPerDay) });
    cursor = addDays(cursor, 1);
  }
  return { ticks, height: 20 };
}

function weekTicks(start: ISODate, end: ISODate, pxPerDay: number): AxisLevel {
  const ticks: AxisTick[] = [];
  let cursor = startOfWeek(start);
  while (parseISO(cursor) <= parseISO(end)) {
    ticks.push({
      iso: cursor,
      label: `W${isoWeekNumber(cursor)}`,
      x: dateToX(cursor, start, pxPerDay),
    });
    cursor = addDays(cursor, 7);
  }
  return { ticks, height: 20 };
}

function monthTicks(start: ISODate, end: ISODate, pxPerDay: number): AxisLevel {
  const ticks: AxisTick[] = [];
  let cursor = startOfMonth(start);
  while (parseISO(cursor) <= parseISO(end)) {
    const d = parseISO(cursor);
    // Month name only. Which ticks carry a year is decided in `withYears`,
    // AFTER thinning and with the real x positions — deciding it here chose
    // ticks that were then thinned away or drawn off the left edge, and a
    // week-scale chart could end up showing no year at all.
    ticks.push({
      iso: cursor,
      label: MONTH_NAMES[d.getUTCMonth()],
      x: dateToX(cursor, start, pxPerDay),
    });
    cursor = addMonths(cursor, 1);
  }
  return { ticks, height: 24 };
}

function quarterTicks(start: ISODate, end: ISODate, pxPerDay: number, fiscalYearStart: number): AxisLevel {
  const ticks: AxisTick[] = [];
  let cursor = startOfQuarter(start, fiscalYearStart);
  while (parseISO(cursor) <= parseISO(end)) {
    const { fy, q } = fiscalQuarterOf(cursor, fiscalYearStart);
    ticks.push({
      iso: cursor,
      label: `Q${q} ${fy}`,
      x: dateToX(cursor, start, pxPerDay),
    });
    cursor = addMonths(cursor, 3);
  }
  return { ticks, height: 24 };
}

function yearTicks(start: ISODate, end: ISODate, pxPerDay: number, fiscalYearStart: number): AxisLevel {
  const ticks: AxisTick[] = [];
  let cursor = startOfYear(start);
  while (parseISO(cursor) <= parseISO(end)) {
    const d = parseISO(cursor);
    let label = String(d.getUTCFullYear());
    if (fiscalYearStart !== 1) {
      label = `FY${d.getUTCFullYear()}`;
    }
    ticks.push({ iso: cursor, label, x: dateToX(cursor, start, pxPerDay) });
    const next = new Date(Date.UTC(d.getUTCFullYear() + 1, 0, 1));
    cursor = formatISO(next);
  }
  return { ticks, height: 28 };
}
