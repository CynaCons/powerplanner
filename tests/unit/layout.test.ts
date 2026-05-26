import { describe, it, expect } from 'vitest';
import { layoutDocument } from '../../src/layout/engine';
import type { GanttDocument } from '../../src/types/document';

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
    style: { theme: 'dark', preset: 'default' },
    ...overrides,
  };
}

describe('layout engine', () => {
  it('positions a bar at viewStart at x=0', () => {
    const doc = makeDoc({
      tasks: [{ id: 't1', rowId: 'r1', label: 'A', start: '2026-06-01', end: '2026-06-08' }],
    });
    const l = layoutDocument({ doc, viewStart: '2026-06-01', pxPerDay: 10 });
    expect(l.tasks[0].x).toBe(0);
    // 8 days inclusive: end - start = 7 days; width = 7*10 + 10 = 80
    expect(l.tasks[0].width).toBe(80);
  });

  it('two overlapping tasks split into sub-rows', () => {
    const doc = makeDoc({
      tasks: [
        { id: 't1', rowId: 'r1', label: 'A', start: '2026-06-01', end: '2026-06-15' },
        { id: 't2', rowId: 'r1', label: 'B', start: '2026-06-08', end: '2026-06-20' },
      ],
    });
    const l = layoutDocument({ doc, viewStart: '2026-06-01', pxPerDay: 10 });
    expect(l.tasks[0].subRow).toBe(0);
    expect(l.tasks[1].subRow).toBe(1);
    expect(l.rowHeights[0]).toBeGreaterThan(36);
  });

  it('non-overlapping tasks stay in one row', () => {
    const doc = makeDoc({
      tasks: [
        { id: 't1', rowId: 'r1', label: 'A', start: '2026-06-01', end: '2026-06-08' },
        { id: 't2', rowId: 'r1', label: 'B', start: '2026-06-10', end: '2026-06-20' },
      ],
    });
    const l = layoutDocument({ doc, viewStart: '2026-06-01', pxPerDay: 10 });
    expect(l.tasks[0].subRow).toBe(0);
    expect(l.tasks[1].subRow).toBe(0);
  });

  it('renders milestone at expected x', () => {
    const doc = makeDoc({
      milestones: [{ id: 'm1', rowId: 'r1', label: 'M', date: '2026-06-10' }],
    });
    const l = layoutDocument({ doc, viewStart: '2026-06-01', pxPerDay: 10 });
    expect(l.milestones[0].x).toBe(90);
  });

  it('builds dependency path between two tasks', () => {
    const doc = makeDoc({
      tasks: [
        { id: 't1', rowId: 'r1', label: 'A', start: '2026-06-01', end: '2026-06-08' },
        { id: 't2', rowId: 'r1', label: 'B', start: '2026-06-15', end: '2026-06-22' },
      ],
      dependencies: [{ id: 'd1', from: 't1', to: 't2', type: 'finish-to-start' }],
    });
    const l = layoutDocument({ doc, viewStart: '2026-06-01', pxPerDay: 10 });
    expect(l.dependencies).toHaveLength(1);
    expect(l.dependencies[0].fromX).toBeGreaterThan(0);
    expect(l.dependencies[0].toX).toBeGreaterThan(l.dependencies[0].fromX);
  });
});
