import { describe, it, expect } from 'vitest';
import { snapDelta } from '../../src/utils/snap';
import type { TimeScale } from '../../src/types/document';

/**
 * The grid a drag lands on.
 *
 * `snapDelta` quantised to the VIEW SCALE, and the document ships at month
 * scale with the magnet on, so the app's one primary verb did nothing: every
 * drag under 15 days committed zero and the bar sprang back. Measured live on
 * 2026-10-27 at 1.3 px/day — asked 10d, got 0d; asked 20d, got 30d.
 *
 * How far you are zoomed out is a property of the window, not of the calendar.
 */
describe('snapDelta', () => {
  const COARSE: TimeScale[] = ['week', 'month', 'quarter', 'year'];

  it('takes a day exactly on the day scale', () => {
    for (const d of [1, -1, 3, 17, -22]) expect(snapDelta(d, 'day')).toBe(d);
  });

  it('never quantises a drag to more than a week, at any zoom', () => {
    for (const scale of COARSE) {
      expect(snapDelta(10, scale)).toBe(7);
      expect(snapDelta(20, scale)).toBe(21);
      expect(snapDelta(45, scale)).toBe(42);
      expect(snapDelta(-10, scale)).toBe(-7);
    }
  });

  it('a short drag is never silently thrown away as a month, quarter or year', () => {
    // The old behaviour: round(10/30)*30 = 0, round(10/91)*91 = 0,
    // round(10/365)*365 = 0. The bar followed the cursor and sprang back.
    for (const scale of COARSE) {
      for (const days of [4, 7, 10, 14]) {
        expect(Math.abs(snapDelta(days, scale) - days)).toBeLessThanOrEqual(3);
      }
    }
  });

  it('is symmetric, so dragging back undoes dragging forward', () => {
    for (const scale of ['day', ...COARSE] as TimeScale[]) {
      for (const d of [1, 5, 10, 33, 100]) {
        expect(snapDelta(-d, scale)).toBe(-snapDelta(d, scale));
      }
    }
  });
});
