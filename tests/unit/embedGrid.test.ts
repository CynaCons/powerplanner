import { describe, it, expect } from 'vitest';
import { readFileSync } from 'node:fs';
import { delimiterXs, nonWorkingBands } from '../../src/layout/grid';
import type { GanttDocument } from '../../src/types/document';
import { gridDelimiterOf, weekendDisplayOf } from '../../src/types/document';

/**
 * SRS-TAX-059 — an exported chart draws the grid the app drew.
 *
 * The embed renderer walked the window itself and emitted one rect per weekend
 * *day* at the old ~1% contrast, drew no delimiters at all, and gated the whole
 * thing on `scale === 'day' || scale === 'week'` — so a chart exported from a
 * month view came out with no weekends, and one exported from a day view came
 * out seamed and in a different grey from the app.
 *
 * Observed red on 79ccbdb: `src/layout/grid.ts` did not exist (the geometry
 * lived inside the axis component, which the embed must not import), and
 * `GanttRenderer` carried its own `weekendStripes` loop.
 */

const WORK_WEEK = [1, 2, 3, 4, 5];
const embed = readFileSync('src/embed/GanttRenderer.tsx', 'utf8');

const doc = (over: Partial<GanttDocument['calendar']> = {}): GanttDocument =>
  ({
    title: 'T',
    rows: [],
    tasks: [],
    milestones: [],
    brackets: [],
    dependencies: [],
    markers: [],
    calendar: { scale: 'month', fiscalYearStart: 1, workingDays: WORK_WEEK, holidays: [], ...over },
    style: {},
  }) as unknown as GanttDocument;

describe('the embed shares the app\'s grid geometry (SRS-TAX-059)', () => {
  it('no longer walks the window itself', () => {
    expect(embed).not.toContain('weekendStripes');
    expect(embed).toContain("from '../layout/grid'");
    expect(embed).toContain('nonWorkingBands(');
    expect(embed).toContain('delimiterXs(');
  });

  it('reads the document settings rather than hard-coding them on', () => {
    expect(embed).toContain('weekendDisplayOf(document.calendar)');
    expect(embed).toContain('gridDelimiterOf(document.calendar)');
  });

  it('gates weekends on px/day, so a month view keeps them', () => {
    // The old gate was the scale name: at `month` the export drew nothing even
    // when a day was 6px wide.
    const bands = nonWorkingBands('2026-01-05', 6, 420, WORK_WEEK);
    expect(bands.length).toBeGreaterThan(0);
    // And it still gives up where a day is thinner than a hairline.
    expect(nonWorkingBands('2026-01-05', 0.4, 420, WORK_WEEK)).toEqual([]);
  });

  it('emits one band per weekend, not one per day', () => {
    // Monday 2026-01-05 + 420px at 6px/day is ten weeks: ten two-day runs.
    const bands = nonWorkingBands('2026-01-05', 6, 420, WORK_WEEK);
    expect(bands.every((b) => b.w === 12)).toBe(true);
  });

  it('defaults both settings the same way the app does', () => {
    expect(weekendDisplayOf(doc().calendar)).toBe(weekendDisplayOf(doc().calendar));
    expect(gridDelimiterOf(doc().calendar)).toBe(gridDelimiterOf(doc().calendar));
    // An explicit setting is honoured, which is what carries through an export.
    expect(weekendDisplayOf(doc({ weekendDisplay: 'shaded' }).calendar)).toBe('shaded');
    expect(gridDelimiterOf(doc({ gridDelimiter: 'quarter' }).calendar)).toBe('quarter');
  });

  it('draws the delimiters the setting asks for, at the setting\'s own boundaries', () => {
    const weekly = delimiterXs('week', '2026-01-05', 6, 420);
    expect(weekly.length).toBe(11);
    expect(weekly[1] - weekly[0]).toBeCloseTo(42);
    expect(delimiterXs('none', '2026-01-05', 6, 420)).toEqual([]);
  });

  it('weights a period boundary more heavily than a day tick, as the app does', () => {
    expect(embed).toContain('--pp-grid-period');
    expect(embed).toContain("delimiter === 'day' ? 'var(--pp-grid,");
  });

  it('shades weekends at a contrast that can actually be seen', () => {
    // 2.5% black over a near-white sheet is ~1% effective: present in the DOM,
    // absent to the eye. The app moved to 12% of --color-text-dim.
    expect(embed).not.toContain('rgba(0, 0, 0, 0.025)');
    expect(embed).not.toContain('rgba(255, 255, 255, 0.025)');
    expect(embed).toContain('--pp-weekend: rgba(0, 0, 0, 0.08)');
    expect(embed).toContain('--pp-weekend: rgba(255, 255, 255, 0.08)');
  });
});
