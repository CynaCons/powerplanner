import { describe, it, expect } from 'vitest';
import { applyOps } from '../../src/stores/docOps';
import { sampleDocument } from '../../src/utils/sample';
import type { GanttDocument } from '../../src/types/document';

// Covers SRS-ABR-017 (row ordering) and SRS-ABR-018 (calendar operations).
// These are the operations the 2026-10-23 audit found missing: row order was
// array position that nothing could change, and the calendar could not be set.

const ids = (d: GanttDocument) => d.rows.map((r) => r.id);

function flat(): GanttDocument {
  const doc = sampleDocument();
  return {
    ...doc,
    rows: [
      { id: 'a', label: 'A', groupId: null },
      { id: 'b', label: 'B', groupId: null },
      { id: 'c', label: 'C', groupId: null },
    ],
    tasks: [],
    milestones: [],
    dependencies: [],
  };
}

function grouped(): GanttDocument {
  return {
    ...flat(),
    rows: [
      { id: 'sec1', label: 'Section 1', groupId: null },
      { id: 'p1', label: 'P1', groupId: 'sec1' },
      { id: 'p2', label: 'P2', groupId: 'sec1' },
      { id: 'tail', label: 'Tail', groupId: null },
    ],
  };
}

describe('move_row', () => {
  it('moves a row later in the order', () => {
    const out = applyOps(flat(), [{ op: 'move_row', id: 'a', toIndex: 2 }]);
    expect(ids(out)).toEqual(['b', 'c', 'a']);
  });

  it('moves a row earlier in the order', () => {
    const out = applyOps(flat(), [{ op: 'move_row', id: 'c', toIndex: 0 }]);
    expect(ids(out)).toEqual(['c', 'a', 'b']);
  });

  it('carries a section’s packages with it', () => {
    const out = applyOps(grouped(), [{ op: 'move_row', id: 'sec1', toIndex: 1 }]);
    // The section lands after 'tail' and keeps p1/p2 adjacent and in order.
    expect(ids(out)).toEqual(['tail', 'sec1', 'p1', 'p2']);
  });

  it('clamps an out-of-range index rather than dropping the row', () => {
    const out = applyOps(flat(), [{ op: 'move_row', id: 'a', toIndex: 99 }]);
    expect(ids(out)).toEqual(['b', 'c', 'a']);
    expect(out.rows).toHaveLength(3);
  });

  it('rejects an unknown row', () => {
    expect(() => applyOps(flat(), [{ op: 'move_row', id: 'nope', toIndex: 0 }])).toThrow();
  });
});

describe('calendar operations', () => {
  it('sets working days, de-duplicated and sorted', () => {
    const out = applyOps(flat(), [{ op: 'set_working_days', days: [5, 1, 1, 3] }]);
    expect(out.calendar.workingDays).toEqual([1, 3, 5]);
  });

  it('rejects a day outside 0-6', () => {
    expect(() => applyOps(flat(), [{ op: 'set_working_days', days: [1, 9] }])).toThrow();
  });

  it('sets holidays, sorted', () => {
    const out = applyOps(flat(), [{ op: 'set_holidays', dates: ['2027-12-26', '2027-01-01'] }]);
    expect(out.calendar.holidays).toEqual(['2027-01-01', '2027-12-26']);
  });

  it('rejects a malformed holiday date', () => {
    expect(() => applyOps(flat(), [{ op: 'set_holidays', dates: ['26/12/2027'] }])).toThrow();
  });
});
