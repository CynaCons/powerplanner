import { describe, it, expect } from 'vitest';
import { resolveInsertTarget } from '../../src/app/insertTarget';
import type { GanttDocument } from '../../src/types/document';

/**
 * SRS-EDIT-028 — an insert lands where the planner is looking.
 *
 * Both the insert buttons and the insert keys passed `doc.rows[0]`. In every
 * shipped template that is a section header, and it is usually scrolled off
 * the top, so inserting appeared to do nothing — or hung work off a heading.
 *
 * Observed red on 79ccbdb: there was no target resolution at all, and a task
 * inserted with a row selected landed on the first section row.
 */

/** A section with two rows under it, plus one ungrouped row. */
function doc(): GanttDocument {
  return {
    rows: [
      { id: 'sec', label: 'Platform', groupId: null },
      { id: 'r1', label: 'MCAL', groupId: 'sec' },
      { id: 'r2', label: 'BSW', groupId: 'sec' },
      { id: 'loose', label: 'Loose', groupId: null },
    ],
    tasks: [{ id: 't1', rowId: 'r2', label: 'T', start: '2026-01-01', end: '2026-01-08', percentComplete: 0 }],
    milestones: [{ id: 'm1', rowId: 'r1', label: 'M', date: '2026-02-01' }],
    dependencies: [],
    markers: [],
    brackets: [],
    calendar: { fiscalYearStart: 1, scale: 'month' },
    style: {},
  } as unknown as GanttDocument;
}

describe('where an insert lands', () => {
  it('uses the selected row when it is a plain row', () => {
    expect(resolveInsertTarget(doc(), [{ kind: 'row', id: 'r2' }])).toEqual({ kind: 'row', rowId: 'r2' });
  });

  it('follows a selected task to its own row', () => {
    expect(resolveInsertTarget(doc(), [{ kind: 'task', id: 't1' }])).toEqual({ kind: 'row', rowId: 'r2' });
  });

  it('follows a selected milestone to its own row', () => {
    expect(resolveInsertTarget(doc(), [{ kind: 'milestone', id: 'm1' }])).toEqual({ kind: 'row', rowId: 'r1' });
  });

  it('makes a row under a section rather than hanging work off the heading', () => {
    expect(resolveInsertTarget(doc(), [{ kind: 'row', id: 'sec' }])).toEqual({
      kind: 'newChild',
      parentId: 'sec',
      index: 1,
    });
  });

  it('with nothing selected, prefers the first row that is not a heading', () => {
    expect(resolveInsertTarget(doc(), [])).toEqual({ kind: 'row', rowId: 'r1' });
  });

  it('never returns the first row just because it is first', () => {
    const d = doc();
    const target = resolveInsertTarget(d, []);
    expect(target).not.toEqual({ kind: 'row', rowId: 'sec' });
  });

  it('an empty document has nowhere to put anything', () => {
    const d = doc();
    d.rows = [];
    expect(resolveInsertTarget(d, [])).toBeNull();
  });
});
