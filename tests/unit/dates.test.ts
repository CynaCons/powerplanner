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
