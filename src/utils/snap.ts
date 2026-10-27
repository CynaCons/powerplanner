import type { TimeScale, ISODate } from '../types/document';
import { addDays, snapToScale, parseISO } from './dates';

/**
 * The grid a drag lands on, in days.
 *
 * This used to quantise to the VIEW SCALE: a month view snapped to 30 days, a
 * quarter view to 91, a year view to 365. The document ships at month scale
 * with the magnet on, so every drag under 15 days committed ZERO — the bar
 * followed the cursor and then sprang back — and a 20-day drag committed 30.
 * The app has one primary verb and that verb did nothing, measured live on
 * 2026-10-27 (1.3 px/day: asked 10d, got 0d; asked 20d, got 30d).
 *
 * How far you are zoomed out is a property of the window, not of the calendar.
 * A week is the coarsest unit anyone actually schedules in, so that is the
 * coarsest this goes, and SRS-EDIT-002 requires whole days either way.
 */
export function snapDelta(deltaDays: number, scale: TimeScale): number {
  // On the day scale a single day is resolvable on screen, so take it exactly.
  if (scale === 'day') return deltaDays;
  return Math.round(deltaDays / 7) * 7;
}

export function snapDate(iso: ISODate, scale: TimeScale, fiscalYearStart = 1): ISODate {
  return snapToScale(iso, scale, fiscalYearStart);
}

export function snapDateOrPreserve(iso: ISODate, scale: TimeScale, fiscalYearStart: number, snapOn: boolean): ISODate {
  if (!snapOn) return iso;
  return snapDate(iso, scale, fiscalYearStart);
}

export { addDays, parseISO };
