import type { ISODate, TimeScale } from '../types/document';

const MS_PER_DAY = 86_400_000;

export function parseISO(iso: ISODate): Date {
  const [y, m, d] = iso.split('-').map(Number);
  return new Date(Date.UTC(y, m - 1, d));
}

export function formatISO(date: Date): ISODate {
  const y = date.getUTCFullYear();
  const m = String(date.getUTCMonth() + 1).padStart(2, '0');
  const d = String(date.getUTCDate()).padStart(2, '0');
  return `${y}-${m}-${d}`;
}

export function addDays(iso: ISODate, days: number): ISODate {
  const t = parseISO(iso).getTime() + days * MS_PER_DAY;
  return formatISO(new Date(t));
}

export function diffDays(a: ISODate, b: ISODate): number {
  return Math.round((parseISO(b).getTime() - parseISO(a).getTime()) / MS_PER_DAY);
}

export function isBefore(a: ISODate, b: ISODate): boolean {
  return parseISO(a).getTime() < parseISO(b).getTime();
}

export function clampDate(iso: ISODate, min: ISODate, max: ISODate): ISODate {
  if (isBefore(iso, min)) return min;
  if (isBefore(max, iso)) return max;
  return iso;
}

export function todayISO(): ISODate {
  const now = new Date();
  return formatISO(new Date(Date.UTC(now.getFullYear(), now.getMonth(), now.getDate())));
}

export function startOfWeek(iso: ISODate): ISODate {
  const d = parseISO(iso);
  const day = d.getUTCDay();
  const offset = day === 0 ? -6 : 1 - day;
  return addDays(iso, offset);
}

/**
 * The ISO-8601 week number (SRS-TAX-023/-024).
 *
 * Weeks run Monday to Sunday — which `startOfWeek` already assumes — and a week
 * belongs to the year that owns its **Thursday**. That rule is the whole reason
 * this is not `floor(dayOfYear / 7)`: it is what makes 29 December 2025 week 1
 * of 2026 and gives some years a week 53. A planner's calendar, their status
 * report and this axis all have to agree on "W42", so the agreement has to be
 * ISO's and not an easier one.
 */
export function isoWeekNumber(iso: ISODate): number {
  const d = parseISO(iso);
  // Step to this week's Thursday. Monday = 0, so +3 lands on Thursday.
  const dow = (d.getUTCDay() + 6) % 7;
  const thursday = new Date(d.getTime() + (3 - dow) * MS_PER_DAY);
  // 4 January is in week 1 by definition, so its week's Thursday anchors the
  // count for whichever year the first Thursday turned out to belong to.
  const jan4 = new Date(Date.UTC(thursday.getUTCFullYear(), 0, 4));
  const jan4Dow = (jan4.getUTCDay() + 6) % 7;
  const week1Thursday = new Date(jan4.getTime() + (3 - jan4Dow) * MS_PER_DAY);
  return 1 + Math.round((thursday.getTime() - week1Thursday.getTime()) / (7 * MS_PER_DAY));
}

/**
 * The year the ISO week belongs to, which is not always the year the date is
 * in — 29 December 2025 is W01 of 2026. Only needed where a week number is
 * shown next to a year.
 */
export function isoWeekYear(iso: ISODate): number {
  const d = parseISO(iso);
  const dow = (d.getUTCDay() + 6) % 7;
  return new Date(d.getTime() + (3 - dow) * MS_PER_DAY).getUTCFullYear();
}

export function startOfMonth(iso: ISODate): ISODate {
  const d = parseISO(iso);
  return formatISO(new Date(Date.UTC(d.getUTCFullYear(), d.getUTCMonth(), 1)));
}

export function startOfQuarter(iso: ISODate, fiscalYearStart = 1): ISODate {
  const d = parseISO(iso);
  const month = d.getUTCMonth();
  const monthsSinceFY = (month - (fiscalYearStart - 1) + 12) % 12;
  const qStart = month - (monthsSinceFY % 3);
  return formatISO(new Date(Date.UTC(d.getUTCFullYear(), qStart, 1)));
}

export function startOfYear(iso: ISODate): ISODate {
  const d = parseISO(iso);
  return formatISO(new Date(Date.UTC(d.getUTCFullYear(), 0, 1)));
}

export function endOfMonth(iso: ISODate): ISODate {
  const d = parseISO(iso);
  return formatISO(new Date(Date.UTC(d.getUTCFullYear(), d.getUTCMonth() + 1, 0)));
}

export function addMonths(iso: ISODate, n: number): ISODate {
  const d = parseISO(iso);
  return formatISO(new Date(Date.UTC(d.getUTCFullYear(), d.getUTCMonth() + n, d.getUTCDate())));
}

export function isWeekend(iso: ISODate, workingDays: number[]): boolean {
  const day = parseISO(iso).getUTCDay();
  return !workingDays.includes(day);
}

export function snapToScale(iso: ISODate, scale: TimeScale, fiscalYearStart = 1): ISODate {
  switch (scale) {
    case 'day':
      return iso;
    case 'week':
      return startOfWeek(iso);
    case 'month':
      return startOfMonth(iso);
    case 'quarter':
      return startOfQuarter(iso, fiscalYearStart);
    case 'year':
      return startOfYear(iso);
  }
}

export function fiscalQuarterOf(iso: ISODate, fiscalYearStart = 1): { fy: number; q: number } {
  const d = parseISO(iso);
  const month = d.getUTCMonth();
  const monthsSinceFY = (month - (fiscalYearStart - 1) + 12) % 12;
  const q = Math.floor(monthsSinceFY / 3) + 1;
  const fy = month >= fiscalYearStart - 1 ? d.getUTCFullYear() : d.getUTCFullYear() - 1;
  return { fy, q };
}
