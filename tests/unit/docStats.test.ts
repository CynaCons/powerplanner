import { describe, it, expect } from 'vitest';
import { computeDocStats } from '../../src/utils/docStats';
import type { GanttDocument } from '../../src/types/document';

/**
 * Gates for the statistics the status bar absorbed from the inspector
 * (PLAN.md v2.13.0 / C0a). The numbers were previously computed inline in the
 * inspector with no coverage; moving them made them testable.
 */

function makeDoc(overrides: Partial<GanttDocument> = {}): GanttDocument {
  return {
    schemaVersion: 1,
    title: 'Test',
    calendar: { scale: 'week', fiscalYearStart: 1, workingDays: [1, 2, 3, 4, 5], holidays: [] },
    rows: [{ id: 'r1', label: 'Row 1', groupId: null }],
    tasks: [],
    milestones: [],
    brackets: [],
    dependencies: [],
    markers: [],
    style: { theme: 'light', preset: 'default' },
    ...overrides,
  };
}

const task = (id: string, start: string, end: string, pct?: number) => ({
  id,
  rowId: 'r1',
  label: id,
  start,
  end,
  ...(pct === undefined ? {} : { percentComplete: pct }),
});

describe('computeDocStats', () => {
  it('reports an empty document without inventing dates', () => {
    const s = computeDocStats(makeDoc());
    expect(s).toMatchObject({ tasks: 0, milestones: 0, min: null, max: null, spanDays: 0, avgPercent: 0 });
    expect(s.spanLabel).toBe('—');
  });

  it('spans from the earliest to the latest dated entity, inclusive', () => {
    const s = computeDocStats(
      makeDoc({
        tasks: [task('t1', '2026-06-01', '2026-06-10')],
        milestones: [{ id: 'm1', rowId: 'r1', label: 'M', date: '2026-06-20' }],
      }),
    );
    expect(s.min).toBe('2026-06-01');
    expect(s.max).toBe('2026-06-20');
    expect(s.spanDays).toBe(20);
  });

  it('scales the span label by magnitude', () => {
    const days = computeDocStats(makeDoc({ tasks: [task('t', '2026-06-01', '2026-06-10')] }));
    expect(days.spanLabel).toBe('10d');

    const weeks = computeDocStats(makeDoc({ tasks: [task('t', '2026-01-01', '2026-04-01')] }));
    expect(weeks.spanLabel).toMatch(/^\d+w$/);

    const years = computeDocStats(makeDoc({ tasks: [task('t', '2026-09-01', '2028-02-18')] }));
    expect(years.spanLabel).toMatch(/^\d\.\dy$/);
  });

  it('averages percent complete, treating an unset value as zero', () => {
    const s = computeDocStats(
      makeDoc({
        tasks: [task('a', '2026-06-01', '2026-06-02', 100), task('b', '2026-06-01', '2026-06-02')],
      }),
    );
    expect(s.avgPercent).toBe(50);
  });

  it('counts milestones even when no task carries dates', () => {
    const s = computeDocStats(
      makeDoc({ milestones: [{ id: 'm1', rowId: 'r1', label: 'Ship', date: '2026-06-20' }] }),
    );
    expect(s.milestones).toBe(1);
    expect(s.min).toBe('2026-06-20');
    expect(s.spanDays).toBe(1);
  });
});
