import { describe, it, expect } from 'vitest';
import {
  addDays,
  diffDays,
  startOfWeek,
  startOfMonth,
  startOfQuarter,
  fiscalQuarterOf,
  isWeekend,
  snapToScale,
  formatISO,
  parseISO,
  isoWeekNumber,
  isoWeekYear,
} from '../../src/utils/dates';

describe('dates', () => {
  it('addDays handles month rollover', () => {
    expect(addDays('2026-01-30', 5)).toBe('2026-02-04');
    expect(addDays('2026-03-01', -1)).toBe('2026-02-28');
  });

  it('diffDays is end-minus-start in calendar days', () => {
    expect(diffDays('2026-01-01', '2026-01-15')).toBe(14);
    expect(diffDays('2026-01-15', '2026-01-01')).toBe(-14);
  });

  it('parse/format roundtrip', () => {
    expect(formatISO(parseISO('2026-06-15'))).toBe('2026-06-15');
  });

  it('startOfWeek snaps to Monday', () => {
    // 2026-06-14 is a Sunday in UTC
    expect(startOfWeek('2026-06-14')).toBe('2026-06-08');
    expect(startOfWeek('2026-06-15')).toBe('2026-06-15');
  });

  it('startOfMonth snaps to day 1', () => {
    expect(startOfMonth('2026-06-15')).toBe('2026-06-01');
    expect(startOfMonth('2026-02-28')).toBe('2026-02-01');
  });

  it('fiscalQuarterOf with calendar fiscal year', () => {
    expect(fiscalQuarterOf('2026-01-15', 1)).toEqual({ fy: 2026, q: 1 });
    expect(fiscalQuarterOf('2026-04-15', 1)).toEqual({ fy: 2026, q: 2 });
    expect(fiscalQuarterOf('2026-12-31', 1)).toEqual({ fy: 2026, q: 4 });
  });

  it('fiscalQuarterOf with April fiscal year', () => {
    expect(fiscalQuarterOf('2026-04-15', 4)).toEqual({ fy: 2026, q: 1 });
    expect(fiscalQuarterOf('2026-01-15', 4)).toEqual({ fy: 2025, q: 4 });
  });

  it('isWeekend respects workingDays mask', () => {
    expect(isWeekend('2026-06-14', [1, 2, 3, 4, 5])).toBe(true); // Sunday
    expect(isWeekend('2026-06-15', [1, 2, 3, 4, 5])).toBe(false); // Monday
  });

  it('snapToScale collapses to start of unit', () => {
    expect(snapToScale('2026-06-15', 'day')).toBe('2026-06-15');
    expect(snapToScale('2026-06-15', 'month')).toBe('2026-06-01');
    expect(snapToScale('2026-06-15', 'year')).toBe('2026-01-01');
  });

  it('startOfQuarter respects fiscal year', () => {
    expect(startOfQuarter('2026-08-15', 1)).toBe('2026-07-01');
    expect(startOfQuarter('2026-05-15', 4)).toBe('2026-04-01');
  });
});

describe('isoWeekNumber (SRS-TAX-023, SRS-TAX-024)', () => {
  it('numbers a mid-year week the way a calendar does', () => {
    // 2026-10-19 is a Monday; ISO week 43 of 2026.
    expect(isoWeekNumber('2026-10-19')).toBe(43);
    expect(isoWeekNumber('2026-10-25')).toBe(43); // the Sunday of the same week
    expect(isoWeekNumber('2026-10-26')).toBe(44); // the next Monday
  });

  it('gives every day of one week the same number', () => {
    const monday = '2026-10-19';
    const numbers = new Set(
      Array.from({ length: 7 }, (_, i) => isoWeekNumber(addDays(monday, i))),
    );
    expect([...numbers]).toEqual([43]);
  });

  it('applies the Thursday rule at the turn of the year', () => {
    // The week of 29 Dec 2025 holds Thursday 1 Jan 2026, so it is 2026 W01 —
    // not a 53rd week of 2025. This is the case a dayOfYear/7 count gets wrong.
    expect(isoWeekNumber('2025-12-29')).toBe(1);
    expect(isoWeekYear('2025-12-29')).toBe(2026);
    expect(isoWeekNumber('2026-01-01')).toBe(1);
    // 2026 ends on a Thursday, so its final week is W53 and runs into 2027.
    expect(isoWeekNumber('2026-12-28')).toBe(53);
    expect(isoWeekNumber('2027-01-03')).toBe(53);
    expect(isoWeekYear('2027-01-03')).toBe(2026);
    expect(isoWeekNumber('2027-01-04')).toBe(1);
  });

  it('starts a 53-week year at W01 and a 52-week year where ISO says', () => {
    // 2027-01-01 is a Friday, so it belongs to 2026 W53.
    expect(isoWeekNumber('2027-01-01')).toBe(53);
    // 2024-01-01 is a Monday, so it is W01 of its own year.
    expect(isoWeekNumber('2024-01-01')).toBe(1);
    expect(isoWeekYear('2024-01-01')).toBe(2024);
    // 2023-01-01 is a Sunday, the last day of 2022 W52.
    expect(isoWeekNumber('2023-01-01')).toBe(52);
    expect(isoWeekYear('2023-01-01')).toBe(2022);
  });

  it('agrees with startOfWeek about where a week begins', () => {
    for (const iso of ['2026-01-15', '2026-06-03', '2026-12-31', '2027-03-08']) {
      expect(isoWeekNumber(iso)).toBe(isoWeekNumber(startOfWeek(iso)));
    }
  });
});
