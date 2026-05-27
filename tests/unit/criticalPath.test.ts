import { describe, it, expect } from 'vitest';
import { computeCriticalPath } from '../../src/layout/criticalPath';
import type { GanttDocument, Task, Dependency } from '../../src/types/document';

function makeDoc(tasks: Task[], dependencies: Dependency[] = []): GanttDocument {
  return {
    schemaVersion: 1,
    title: 'CPM Test',
    calendar: { scale: 'day', fiscalYearStart: 1, workingDays: [1, 2, 3, 4, 5], holidays: [] },
    rows: [{ id: 'r1', label: 'Row 1', groupId: null }],
    tasks,
    milestones: [],
    brackets: [],
    dependencies,
    markers: [],
    style: { theme: 'light', preset: 'default' },
  };
}

const T = (id: string, start: string, end: string): Task => ({
  id, rowId: 'r1', label: id, start, end,
});
const D = (id: string, from: string, to: string, type: Dependency['type'] = 'finish-to-start'): Dependency =>
  ({ id, from, to, type });

describe('computeCriticalPath', () => {
  it('linear FS chain of 3 tasks is fully critical', () => {
    const doc = makeDoc(
      [T('a', '2026-01-01', '2026-01-04'), T('b', '2026-01-04', '2026-01-09'), T('c', '2026-01-09', '2026-01-11')],
      [D('d1', 'a', 'b'), D('d2', 'b', 'c')],
    );
    const r = computeCriticalPath(doc);
    expect(r.hasCycle).toBe(false);
    expect(r.criticalTaskIds).toEqual(new Set(['a', 'b', 'c']));
    expect(r.taskFloat.get('a')).toBe(0);
    expect(r.taskFloat.get('b')).toBe(0);
    expect(r.projectDurationDays).toBe(10);
  });

  it('parallel paths: only the longer branch is critical', () => {
    const doc = makeDoc(
      [
        T('start', '2026-01-01', '2026-01-02'),
        T('short', '2026-01-02', '2026-01-04'),
        T('long', '2026-01-02', '2026-01-09'),
        T('end', '2026-01-09', '2026-01-10'),
      ],
      [D('d1', 'start', 'short'), D('d2', 'start', 'long'), D('d3', 'short', 'end'), D('d4', 'long', 'end')],
    );
    const r = computeCriticalPath(doc);
    expect(r.criticalTaskIds.has('start')).toBe(true);
    expect(r.criticalTaskIds.has('long')).toBe(true);
    expect(r.criticalTaskIds.has('end')).toBe(true);
    expect(r.criticalTaskIds.has('short')).toBe(false);
    expect(r.taskFloat.get('short')).toBe(5);
    expect(r.projectDurationDays).toBe(9);
  });

  it('start-to-start dependency propagates ES, not EF', () => {
    const doc = makeDoc(
      [T('a', '2026-01-01', '2026-01-06'), T('b', '2026-01-01', '2026-01-04')],
      [D('d1', 'a', 'b', 'start-to-start')],
    );
    const r = computeCriticalPath(doc);
    expect(r.criticalTaskIds.has('a')).toBe(true);
    expect(r.taskFloat.get('a')).toBe(0);
    expect(r.taskFloat.get('b')).toBe(2);
    expect(r.projectDurationDays).toBe(5);
  });

  it('finish-to-finish dependency anchors EF of successor', () => {
    const doc = makeDoc(
      [T('a', '2026-01-01', '2026-01-08'), T('b', '2026-01-06', '2026-01-08')],
      [D('d1', 'a', 'b', 'finish-to-finish')],
    );
    const r = computeCriticalPath(doc);
    expect(r.criticalTaskIds.has('a')).toBe(true);
    expect(r.criticalTaskIds.has('b')).toBe(true);
    expect(r.projectDurationDays).toBe(7);
  });

  it('disconnected tasks with no deps are each critical', () => {
    const doc = makeDoc([
      T('a', '2026-01-01', '2026-01-05'),
      T('b', '2026-02-01', '2026-02-03'),
      T('c', '2026-03-01', '2026-03-10'),
    ]);
    const r = computeCriticalPath(doc);
    expect(r.criticalTaskIds).toEqual(new Set(['a', 'b', 'c']));
  });

  it('cycle detection returns empty critical set with hasCycle=true', () => {
    const doc = makeDoc(
      [T('a', '2026-01-01', '2026-01-04'), T('b', '2026-01-04', '2026-01-07'), T('c', '2026-01-07', '2026-01-10')],
      [D('d1', 'a', 'b'), D('d2', 'b', 'c'), D('d3', 'c', 'a')],
    );
    const r = computeCriticalPath(doc);
    expect(r.hasCycle).toBe(true);
    expect(r.criticalTaskIds.size).toBe(0);
  });

  it('mixed 5-task graph: one non-critical side branch', () => {
    const doc = makeDoc(
      [
        T('a', '2026-01-01', '2026-01-03'),
        T('b', '2026-01-03', '2026-01-07'),
        T('c', '2026-01-07', '2026-01-09'),
        T('d', '2026-01-07', '2026-01-10'),
        T('e', '2026-01-10', '2026-01-11'),
      ],
      [D('d1', 'a', 'b'), D('d2', 'b', 'd'), D('d3', 'b', 'c'), D('d4', 'd', 'e'), D('d5', 'c', 'e')],
    );
    const r = computeCriticalPath(doc);
    expect(r.projectDurationDays).toBe(10);
    expect(r.criticalTaskIds.has('a')).toBe(true);
    expect(r.criticalTaskIds.has('b')).toBe(true);
    expect(r.criticalTaskIds.has('d')).toBe(true);
    expect(r.criticalTaskIds.has('e')).toBe(true);
    expect(r.criticalTaskIds.has('c')).toBe(false);
    expect(r.taskFloat.get('c')).toBe(1);
  });
});
