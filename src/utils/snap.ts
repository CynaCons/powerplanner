import type { TimeScale, ISODate } from '../types/document';
import { addDays, snapToScale, parseISO } from './dates';

export function snapDelta(deltaDays: number, scale: TimeScale): number {
  switch (scale) {
    case 'day':
      return deltaDays;
    case 'week':
      return Math.round(deltaDays / 7) * 7;
    case 'month':
      return Math.round(deltaDays / 30) * 30;
    case 'quarter':
      return Math.round(deltaDays / 91) * 91;
    case 'year':
      return Math.round(deltaDays / 365) * 365;
  }
}

export function snapDate(iso: ISODate, scale: TimeScale, fiscalYearStart = 1): ISODate {
  return snapToScale(iso, scale, fiscalYearStart);
}

export function snapDateOrPreserve(iso: ISODate, scale: TimeScale, fiscalYearStart: number, snapOn: boolean): ISODate {
  if (!snapOn) return iso;
  return snapDate(iso, scale, fiscalYearStart);
}

export { addDays, parseISO };
