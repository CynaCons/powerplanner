import { describe, it, expect } from 'vitest';
import { anchorOffsetDays, anchoredX, columnSpan, dateToX } from '../../src/layout/timeAxis';

/**
 * SRS-ELEM-070 — a mark sits where it is put, inside its column.
 *
 * Owner request 2026-10-30: "we should be able to put it either at the
 * beginning, at the end, or at the middle" of the column a date falls in. The
 * column is whatever the roadmap's delimiter draws, so the middle of a month
 * grid is the middle of the month, not noon on the day.
 */

const VIEW = '2026-01-01';
const PX = 10;

describe('the column a date falls in', () => {
  it('is the day itself where there is no coarser grid', () => {
    expect(columnSpan('2026-03-11', 'none')).toEqual({ start: '2026-03-11', days: 1 });
    expect(columnSpan('2026-03-11', 'day')).toEqual({ start: '2026-03-11', days: 1 });
  });

  it('is the week, starting Monday, as the axis draws it', () => {
    // 2026-03-11 is a Wednesday.
    expect(columnSpan('2026-03-11', 'week')).toEqual({ start: '2026-03-09', days: 7 });
  });

  it('is the calendar month, with its own length', () => {
    expect(columnSpan('2026-03-11', 'month')).toEqual({ start: '2026-03-01', days: 31 });
    expect(columnSpan('2026-02-11', 'month')).toEqual({ start: '2026-02-01', days: 28 });
  });

  it('is the quarter, and quarters are not all the same length', () => {
    expect(columnSpan('2026-02-11', 'quarter')).toEqual({ start: '2026-01-01', days: 90 });
    expect(columnSpan('2026-05-11', 'quarter')).toEqual({ start: '2026-04-01', days: 91 });
  });
});

describe('where in the column the mark is drawn', () => {
  it('does not move a mark that has never been given an anchor', () => {
    // Every document written before the field renders exactly where it did.
    for (const d of ['none', 'day', 'week', 'month', 'quarter'] as const) {
      expect(anchorOffsetDays('2026-03-11', d, undefined)).toBe(0);
      expect(anchorOffsetDays('2026-03-11', d, 'start')).toBe(0);
    }
  });

  it('puts the middle of a month grid in the middle of the month', () => {
    // 11 March is 10 days into a 31-day column; its middle is 15.5 days in.
    expect(anchorOffsetDays('2026-03-11', 'month', 'middle')).toBeCloseTo(5.5, 10);
    expect(anchorOffsetDays('2026-03-11', 'month', 'end')).toBe(21);
  });

  it('puts the end of a week grid at the end of that week', () => {
    // Wednesday is 2 days into the week; its end is 7 days in.
    expect(anchorOffsetDays('2026-03-11', 'week', 'end')).toBe(5);
    expect(anchorOffsetDays('2026-03-11', 'week', 'middle')).toBeCloseTo(1.5, 10);
  });

  it('moves the first of a month to the far side of it, not into the next one', () => {
    // The case the request is about: a release at the end of March reading as
    // the end of March rather than straddling the line into April.
    const offset = anchorOffsetDays('2026-03-01', 'month', 'end');
    expect(offset).toBe(31);
    expect(anchorOffsetDays('2026-03-31', 'month', 'end')).toBe(1);
  });

  it('is half a day either side of nothing on a day grid', () => {
    expect(anchorOffsetDays('2026-03-11', 'day', 'middle')).toBe(0.5);
    expect(anchorOffsetDays('2026-03-11', 'day', 'end')).toBe(1);
  });
});

describe('anchoredX', () => {
  it('is dateToX when there is no anchor', () => {
    expect(anchoredX('2026-03-11', VIEW, PX, 'month', undefined)).toBe(
      dateToX('2026-03-11', VIEW, PX),
    );
  });

  it('adds the offset in pixels, at the zoom in force', () => {
    const plain = dateToX('2026-03-11', VIEW, PX);
    expect(anchoredX('2026-03-11', VIEW, PX, 'week', 'end')).toBe(plain + 5 * PX);
    expect(anchoredX('2026-03-11', VIEW, 2, 'week', 'end')).toBe(
      dateToX('2026-03-11', VIEW, 2) + 5 * 2,
    );
  });
});
