import { describe, it, expect } from 'vitest';
import { bracketSpanFor } from '../../src/app/bracketSpan';
import { todayISO, addDays } from '../../src/utils/dates';
import type { GanttDocument } from '../../src/types/document';

/**
 * SRS-ELEM-032 — a bracket covers what is selected.
 *
 * The command bar's Insert → Bracket passed `doc.rows[0]` and
 * `today … today + 14` whatever was selected. Row zero is a section header in
 * every shipped template, so the bracket landed across a heading a fortnight
 * from today — nowhere near the work the planner had picked, which is how the
 * owner came to have brackets he had not asked for in places he was not
 * looking. The keyboard, the palette and the selection group already read the
 * span off the selection; the bar is the last caller to.
 *
 * Observed red on 79ccbdb.
 */

function doc(): GanttDocument {
  return {
    schemaVersion: 1,
    title: 'Spans',
    calendar: { scale: 'week', fiscalYearStart: 1, workingDays: [1, 2, 3, 4, 5], holidays: [] },
    rows: [
      { id: 'sec', label: 'Phase 1', groupId: null },
      { id: 'r1', label: 'BSW', groupId: 'sec' },
      { id: 'r2', label: 'MCAL', groupId: 'sec' },
      { id: 'r3', label: 'Validation', groupId: null },
    ],
    tasks: [
      { id: 't1', rowId: 'r1', label: 'Port', start: '2026-06-01', end: '2026-06-12', percentComplete: 0 },
      { id: 't2', rowId: 'r2', label: 'Config', start: '2026-06-08', end: '2026-06-20', percentComplete: 0 },
      { id: 't3', rowId: 'r3', label: 'HIL', start: '2026-07-01', end: '2026-07-10', percentComplete: 0 },
    ],
    milestones: [{ id: 'm1', rowId: 'r3', label: 'Gate', date: '2026-07-15' }],
    brackets: [],
    dependencies: [],
    markers: [],
    style: { theme: 'light', preset: 'default' },
  };
}

describe('a new bracket spans the selection (SRS-ELEM-032)', () => {
  it('covers two selected tasks and the rows they sit on', () => {
    const span = bracketSpanFor(doc(), [
      { kind: 'task', id: 't1' },
      { kind: 'task', id: 't2' },
    ]);
    expect(span.start).toBe('2026-06-01');
    expect(span.end).toBe('2026-06-20');
    expect(span.rowIds).toEqual(['r1', 'r2']);
  });

  it('takes a selected row and the dates of what is on it', () => {
    const span = bracketSpanFor(doc(), [{ kind: 'row', id: 'r3' }]);
    expect(span.rowIds).toEqual(['r3']);
    expect(span.start).toBe('2026-07-01');
    expect(span.end).toBe('2026-07-15'); // the milestone is in the span too
  });

  it('keeps rows in the document order, not the order they were clicked', () => {
    const span = bracketSpanFor(doc(), [
      { kind: 'row', id: 'r3' },
      { kind: 'row', id: 'r1' },
    ]);
    expect(span.rowIds).toEqual(['r1', 'r3']);
  });

  it('brackets the whole plan when nothing is selected', () => {
    const span = bracketSpanFor(doc(), []);
    expect(span.start).toBe('2026-06-01');
    expect(span.end).toBe('2026-07-15');
    expect(span.rowIds).toEqual(['r1', 'r2', 'r3']);
    // And never the section header on its own, which is what it used to be.
    expect(span.rowIds).not.toContain('sec');
  });

  it('falls back to a fortnight only on a document with nothing in it', () => {
    const empty = doc();
    empty.tasks = [];
    empty.milestones = [];
    const span = bracketSpanFor(empty, []);
    expect(span.start).toBe(todayISO());
    expect(span.end).toBe(addDays(todayISO(), 14));
    expect(span.rowIds).toEqual(['sec']);
  });
});
