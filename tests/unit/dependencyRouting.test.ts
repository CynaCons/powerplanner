import { describe, it, expect } from 'vitest';
import { layoutDocument, DEP_STUB } from '../../src/layout/engine';
import type { DependencyType, GanttDocument } from '../../src/types/document';

/**
 * SRS-ELEM-042/043 — a connector points at its successor, with clearance.
 *
 * The shipped v2.21.0 build drew four of the six links in the sample chart
 * backwards. The turn sat at `midX = (fromX + toX) / 2`, which says nothing
 * about which side of the successor it lands on, and a bar is painted through
 * its end day — so a task finishing on the day the next one starts has its
 * right cap a day to the RIGHT of the successor's left cap. The last leg then
 * ran right-to-left and the `orient="auto"` marker rotated the head 180°.
 * Observed on the running app: `M 362.64 882 L 361.67 882 L 361.67 954 L
 * 360.69 954` — a backwards leg not quite one pixel long.
 *
 * These tests read the path back as points, because the path is what the
 * renderer and the exporter both draw and the head's rotation is taken from
 * its last segment.
 */

function makeDoc(overrides: Partial<GanttDocument> = {}): GanttDocument {
  return {
    schemaVersion: 1,
    title: 'Test',
    calendar: { scale: 'week', fiscalYearStart: 1, workingDays: [1, 2, 3, 4, 5], holidays: [] },
    rows: [
      { id: 'r1', label: 'Row 1', groupId: null },
      { id: 'r2', label: 'Row 2', groupId: null },
    ],
    tasks: [],
    milestones: [],
    brackets: [],
    dependencies: [],
    markers: [],
    style: { theme: 'light', preset: 'default' },
    ...overrides,
  };
}

interface Pt { x: number; y: number }

function points(path: string): Pt[] {
  return path
    .trim()
    .split(/(?=[ML])/)
    .map((seg) => seg.trim().replace(/^[ML]\s*/, ''))
    .filter(Boolean)
    .map((pair) => {
      const [x, y] = pair.split(/\s+/).map(Number);
      return { x, y };
    });
}

/** Sign of the last segment: +1 travelling right, -1 left, 0 not horizontal. */
function lastLegDir(path: string): number {
  const p = points(path);
  const a = p[p.length - 2];
  const b = p[p.length - 1];
  return Math.sign(Number((b.x - a.x).toFixed(6)));
}

/** Sign of the first segment, the stub leaving the predecessor. */
function firstLegDir(path: string): number {
  const p = points(path);
  return Math.sign(Number((p[1].x - p[0].x).toFixed(6)));
}

function legLengths(path: string): number[] {
  const p = points(path);
  return p.slice(1).map((b, i) => Math.hypot(b.x - p[i].x, b.y - p[i].y));
}

/**
 * Two tasks and one link. `gap` is how the successor sits against the
 * predecessor: a positive gap starts it that many days after the predecessor
 * ends, 0 starts it on the same day (the case that broke), negative overlaps.
 */
function link(type: DependencyType, gap: number, sameRow = false) {
  const doc = makeDoc({
    tasks: [
      { id: 't1', rowId: 'r1', label: 'A', start: '2026-06-01', end: '2026-06-10' },
      {
        id: 't2',
        rowId: sameRow ? 'r1' : 'r2',
        label: 'B',
        start: addDay('2026-06-10', gap),
        end: addDay('2026-06-10', gap + 9),
      },
    ],
    dependencies: [{ id: 'd1', from: 't1', to: 't2', type }],
  });
  const l = layoutDocument({ doc, viewStart: '2026-06-01', pxPerDay: 10, allowOverlap: sameRow });
  return l.dependencies[0];
}

function addDay(iso: string, days: number): string {
  const d = new Date(`${iso}T00:00:00Z`);
  d.setUTCDate(d.getUTCDate() + days);
  return d.toISOString().slice(0, 10);
}

describe('dependency routing (SRS-ELEM-042)', () => {
  it('a finish-to-start link enters its successor travelling right', () => {
    const d = link('finish-to-start', 5);
    expect(d.entryDir).toBe(1);
    expect(lastLegDir(d.path)).toBe(1);
  });

  it('a zero-gap finish-to-start link still enters travelling right', () => {
    // The shipped defect, exactly: the successor starts on the day the
    // predecessor ends, so the predecessor's painted cap is one day further
    // right than the successor's start.
    const d = link('finish-to-start', 0);
    expect(d.fromX).toBeGreaterThan(d.toX);
    expect(d.entryDir).toBe(1);
    expect(lastLegDir(d.path)).toBe(1);
  });

  it('an overlapping finish-to-start link still enters travelling right', () => {
    const d = link('finish-to-start', -4);
    expect(d.entryDir).toBe(1);
    expect(lastLegDir(d.path)).toBe(1);
  });

  it('a start-to-start link enters the successor start travelling right', () => {
    const d = link('start-to-start', 5);
    expect(d.entryDir).toBe(1);
    expect(firstLegDir(d.path)).toBe(-1); // leaves the predecessor's start cap
    expect(lastLegDir(d.path)).toBe(1);
  });

  it('a finish-to-finish link enters the successor finish travelling left', () => {
    const d = link('finish-to-finish', 5);
    expect(d.entryDir).toBe(-1);
    expect(firstLegDir(d.path)).toBe(1);
    expect(lastLegDir(d.path)).toBe(-1);
  });

  it('a start-to-finish link enters the successor finish travelling left', () => {
    const d = link('start-to-finish', 5);
    expect(d.entryDir).toBe(-1);
    expect(lastLegDir(d.path)).toBe(-1);
  });

  it('every link type points into its successor at every spacing', () => {
    const types: DependencyType[] = [
      'finish-to-start',
      'start-to-start',
      'finish-to-finish',
      'start-to-finish',
    ];
    for (const type of types) {
      for (const gap of [12, 1, 0, -5, -9]) {
        const d = link(type, gap);
        expect(lastLegDir(d.path), `${type} at gap ${gap}`).toBe(d.entryDir);
      }
    }
  });

  it('the endpoints stay on the caps the link type names (SRS-ELEM-041)', () => {
    const doc = makeDoc({
      tasks: [
        { id: 't1', rowId: 'r1', label: 'A', start: '2026-06-01', end: '2026-06-10' },
        { id: 't2', rowId: 'r2', label: 'B', start: '2026-06-15', end: '2026-06-24' },
      ],
      dependencies: [
        { id: 'fs', from: 't1', to: 't2', type: 'finish-to-start' },
        { id: 'ss', from: 't1', to: 't2', type: 'start-to-start' },
        { id: 'ff', from: 't1', to: 't2', type: 'finish-to-finish' },
        { id: 'sf', from: 't1', to: 't2', type: 'start-to-finish' },
      ],
    });
    const l = layoutDocument({ doc, viewStart: '2026-06-01', pxPerDay: 10 });
    const t1 = l.tasks.find((t) => t.task.id === 't1')!;
    const t2 = l.tasks.find((t) => t.task.id === 't2')!;
    const by = (id: string) => l.dependencies.find((d) => d.dependency.id === id)!;

    expect(by('fs').fromX).toBe(t1.x + t1.width);
    expect(by('fs').toX).toBe(t2.x);
    expect(by('ss').fromX).toBe(t1.x);
    expect(by('ss').toX).toBe(t2.x);
    expect(by('ff').fromX).toBe(t1.x + t1.width);
    expect(by('ff').toX).toBe(t2.x + t2.width);
    expect(by('sf').fromX).toBe(t1.x);
    expect(by('sf').toX).toBe(t2.x + t2.width);
  });
});

describe('dependency clearance (SRS-ELEM-043)', () => {
  it('both ends get a stub before the elbow turns', () => {
    for (const gap of [12, 0, -5]) {
      const d = link('finish-to-start', gap);
      const legs = legLengths(d.path);
      expect(legs[0], `stub out at gap ${gap}`).toBeGreaterThanOrEqual(DEP_STUB);
      expect(legs[legs.length - 1], `stub in at gap ${gap}`).toBeGreaterThanOrEqual(DEP_STUB);
    }
  });

  it('a doubling-back link crosses in the band between the two rows', () => {
    const d = link('finish-to-start', 0);
    const p = points(d.path);
    expect(p).toHaveLength(6); // out, up/down, across, up/down, in
    const laneY = p[2].y;
    expect(laneY).toBeCloseTo((d.fromY + d.toY) / 2, 6);
    // Clear of both bar bands: bars are 22px tall, centred on fromY and toY.
    expect(Math.abs(laneY - d.fromY)).toBeGreaterThan(11);
    expect(Math.abs(laneY - d.toY)).toBeGreaterThan(11);
  });

  it('a doubling-back link on one row runs clear of the bars, not along them', () => {
    const d = link('finish-to-start', -4, true);
    const p = points(d.path);
    expect(p).toHaveLength(6);
    expect(p[2].y).toBeGreaterThan(d.fromY);
    expect(p[2].y).toBeGreaterThan(d.toY);
  });

  it('every segment is horizontal or vertical', () => {
    for (const gap of [12, 0, -5]) {
      const p = points(link('finish-to-start', gap).path);
      for (let i = 1; i < p.length; i++) {
        const dx = Math.abs(p[i].x - p[i - 1].x);
        const dy = Math.abs(p[i].y - p[i - 1].y);
        expect(Math.min(dx, dy), `segment ${i} at gap ${gap}`).toBeLessThan(1e-6);
      }
    }
  });
});
