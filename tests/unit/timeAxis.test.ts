import { describe, it, expect } from 'vitest';
import { buildAxis } from '../../src/layout/timeAxis';
import { isoWeekNumber } from '../../src/utils/dates';

/**
 * What the axis says, as opposed to where it puts things.
 *
 * `buildAxis` had no test of its labels at all, which is how the week scale
 * shipped for nineteen versions labelled "Oct 19" — a date, under a month band
 * that already said October, in a band whose whole job is to say which week
 * this is (owner report 2026-10-27).
 */
describe('buildAxis labels', () => {
  const WIDTH = 1200;

  describe('week scale (SRS-TAX-023)', () => {
    it('labels the minor band with calendar week numbers', () => {
      const axis = buildAxis('2026-10-19', 6, WIDTH, 'week', 1);
      const labels = axis.minor.ticks.map((t) => t.label);
      expect(labels.length).toBeGreaterThan(4);
      expect(labels.slice(0, 4)).toEqual(['W43', 'W44', 'W45', 'W46']);
      for (const l of labels) expect(l).toMatch(/^W\d{1,2}$/);
    });

    it('never labels a week with a date', () => {
      const axis = buildAxis('2026-10-19', 6, WIDTH, 'week', 1);
      for (const t of axis.minor.ticks) {
        expect(t.label).not.toMatch(/\d{1,2}$/.source + '$'); // no bare day number
        expect(t.label).not.toMatch(/Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec/);
      }
    });

    it('agrees with isoWeekNumber on each tick it emits', () => {
      const axis = buildAxis('2026-11-02', 4, WIDTH, 'week', 1);
      for (const t of axis.minor.ticks) {
        expect(t.label).toBe(`W${isoWeekNumber(t.iso)}`);
      }
    });

    it('carries the week numbers through the year boundary', () => {
      const axis = buildAxis('2026-12-07', 6, WIDTH, 'week', 1);
      const labels = axis.minor.ticks.map((t) => t.label);
      // 2026 ends on a Thursday, so it has a W53 and then W01 of 2027.
      expect(labels).toContain('W53');
      expect(labels).toContain('W1');
      expect(labels.indexOf('W53')).toBeLessThan(labels.indexOf('W1'));
    });

    it('also numbers the weeks on the day scale', () => {
      const axis = buildAxis('2026-10-19', 24, WIDTH, 'day', 1);
      expect(axis.minor.ticks[0].label).toBe('W43');
      expect(axis.micro?.ticks[0].label).toBe('19');
    });
  });

  describe('month band (SRS-TAX-025)', () => {
    it('carries the year on the leading tick and on January only', () => {
      const axis = buildAxis('2026-10-01', 4, WIDTH, 'week', 1);
      const labels = axis.major.ticks.map((t) => t.label);
      expect(labels[0]).toBe('Oct 2026');
      expect(labels).toContain('Nov');
      expect(labels).toContain('Dec');
      expect(labels).toContain('Jan 2027');
      expect(labels).not.toContain('Nov 2026');
    });

    it('states the year once a view sits inside a single year', () => {
      const axis = buildAxis('2026-03-01', 10, WIDTH, 'week', 1);
      const withYear = axis.major.ticks.filter((t) => /\d{4}/.test(t.label));
      expect(withYear).toHaveLength(1);
      expect(withYear[0].label).toMatch(/2026$/);
    });
  });

  describe('the other scales keep their labels', () => {
    it('quarter and year bands are unchanged', () => {
      const q = buildAxis('2026-01-01', 1, WIDTH, 'quarter', 1);
      expect(q.minor.ticks[0].label).toBe('Q1 2026');
      expect(q.major.ticks[0].label).toBe('2026');

      const fy = buildAxis('2026-01-01', 1, WIDTH, 'quarter', 4);
      expect(fy.major.ticks[0].label).toBe('FY2026');
    });
  });
});
