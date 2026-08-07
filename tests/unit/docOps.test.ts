import { describe, it, expect, beforeEach } from 'vitest';
import { applyOp, applyOps, DocOpError, type DocOp } from '../../src/stores/docOps';
import { useDocumentStore } from '../../src/stores/documentStore';
import type { GanttDocument } from '../../src/types/document';

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

const task = (id: string, rowId = 'r1'): DocOp => ({
  op: 'add_task',
  task: { id, rowId, label: id, start: '2026-06-01', end: '2026-06-08' },
});

describe('applyOp — purity and referential integrity', () => {
  it('does not mutate the input document', () => {
    const doc = makeDoc();
    const snapshot = JSON.stringify(doc);
    applyOp(doc, task('t1'));
    expect(JSON.stringify(doc)).toBe(snapshot);
  });

  it('rejects a task on a row that does not exist', () => {
    expect(() => applyOp(makeDoc(), task('t1', 'nope'))).toThrow(/row 'nope' does not exist/);
  });

  it('rejects a duplicate id', () => {
    const doc = applyOp(makeDoc(), task('t1'));
    expect(() => applyOp(doc, task('t1'))).toThrow(/task 't1' already exists/);
  });

  it('rejects a dependency whose endpoints are missing', () => {
    const doc = applyOp(makeDoc(), task('t1'));
    const dep: DocOp = {
      op: 'add_dependency',
      dependency: { id: 'd1', from: 't1', to: 'ghost', type: 'finish-to-start' },
    };
    expect(() => applyOp(doc, dep)).toThrow(/task 'ghost' does not exist/);
  });

  it('rejects a self-referential dependency', () => {
    const doc = applyOp(makeDoc(), task('t1'));
    const dep: DocOp = {
      op: 'add_dependency',
      dependency: { id: 'd1', from: 't1', to: 't1', type: 'finish-to-start' },
    };
    expect(() => applyOp(doc, dep)).toThrow(/links task 't1' to itself/);
  });

  it('rejects an out-of-range fiscal year start', () => {
    expect(() => applyOp(makeDoc(), { op: 'set_fiscal_year_start', month: 13 })).toThrow(/1-12/);
  });

  it('rejects updating a task onto a nonexistent row', () => {
    const doc = applyOp(makeDoc(), task('t1'));
    expect(() => applyOp(doc, { op: 'update_task', id: 't1', patch: { rowId: 'ghost' } })).toThrow(
      /row 'ghost' does not exist/,
    );
  });
});

describe('applyOp — cascade deletes match user-initiated deletion (SRS-ABR-015)', () => {
  it('deleting a task removes its dependencies', () => {
    let doc = applyOps(makeDoc(), [task('t1'), task('t2')]);
    doc = applyOp(doc, {
      op: 'add_dependency',
      dependency: { id: 'd1', from: 't1', to: 't2', type: 'finish-to-start' },
    });
    const after = applyOp(doc, { op: 'delete_task', id: 't1' });
    expect(after.tasks.map((t) => t.id)).toEqual(['t2']);
    expect(after.dependencies).toEqual([]);
  });

  it('deleting a row removes its tasks, milestones, and their dependencies', () => {
    let doc = applyOps(makeDoc(), [task('t1', 'r1'), task('t2', 'r2')]);
    doc = applyOps(doc, [
      {
        op: 'add_milestone',
        milestone: { id: 'm1', rowId: 'r1', label: 'M', date: '2026-06-10' },
      },
      {
        op: 'add_dependency',
        dependency: { id: 'd1', from: 't1', to: 't2', type: 'finish-to-start' },
      },
    ]);

    const after = applyOp(doc, { op: 'delete_row', id: 'r1' });
    expect(after.rows.map((r) => r.id)).toEqual(['r2']);
    expect(after.tasks.map((t) => t.id)).toEqual(['t2']);
    expect(after.milestones).toEqual([]);
    // t1 went with the row, so the dependency that referenced it must go too —
    // otherwise an agent could leave a dangling reference the UI cannot produce.
    expect(after.dependencies).toEqual([]);
  });
});

describe('applyOps — all-or-nothing batching (SRS-ABR-012)', () => {
  it('reduces a sequence into one document', () => {
    const doc = applyOps(makeDoc(), [task('t1'), task('t2'), task('t3')]);
    expect(doc.tasks.map((t) => t.id)).toEqual(['t1', 't2', 't3']);
  });

  it('lets later ops depend on earlier ops in the same batch', () => {
    const doc = applyOps(makeDoc(), [
      { op: 'add_row', row: { id: 'r9', label: 'New', groupId: null } },
      task('t9', 'r9'),
    ]);
    expect(doc.tasks[0].rowId).toBe('r9');
  });

  it('applies nothing when any op in the batch is invalid', () => {
    const before = makeDoc();
    const ops: DocOp[] = [task('t1'), task('t2', 'ghost'), task('t3')];
    expect(() => applyOps(before, ops)).toThrow(DocOpError);
    // The caller's document is untouched — not half-written.
    expect(before.tasks).toEqual([]);
  });

  it('names the offending operation index and kind', () => {
    const ops: DocOp[] = [task('t1'), task('t2', 'ghost')];
    try {
      applyOps(makeDoc(), ops);
      expect.unreachable('should have thrown');
    } catch (err) {
      expect(err).toBeInstanceOf(DocOpError);
      expect((err as DocOpError).opIndex).toBe(1);
      expect((err as DocOpError).kind).toBe('add_task');
    }
  });

  it('is a no-op for an empty batch', () => {
    const before = makeDoc();
    expect(applyOps(before, [])).toBe(before);
  });
});

describe('documentStore — batching, revisions, agent state', () => {
  beforeEach(() => {
    useDocumentStore.setState({
      doc: makeDoc(),
      past: [],
      future: [],
      revision: 0,
      userRevision: 0,
      agentActive: false,
      lastAgentEditAt: null,
    });
  });

  it('a 50-op batch produces exactly one history frame and one store commit (SRS-ABR-013)', () => {
    const ops: DocOp[] = Array.from({ length: 50 }, (_, i) => task(`t${i}`));

    let commits = 0;
    const unsubscribe = useDocumentStore.subscribe(() => {
      commits++;
    });
    useDocumentStore.getState().applyBatch(ops);
    unsubscribe();

    const s = useDocumentStore.getState();
    expect(s.doc.tasks).toHaveLength(50);
    expect(s.past).toHaveLength(1);
    expect(commits).toBe(1);
  });

  it('one undo reverts an entire agent batch', () => {
    const ops: DocOp[] = Array.from({ length: 12 }, (_, i) => task(`t${i}`));
    useDocumentStore.getState().applyBatch(ops);
    expect(useDocumentStore.getState().doc.tasks).toHaveLength(12);

    useDocumentStore.getState().undo();
    expect(useDocumentStore.getState().doc.tasks).toHaveLength(0);
  });

  it('an invalid batch leaves the store untouched and adds no history', () => {
    const store = useDocumentStore.getState();
    expect(() => store.applyBatch([task('t1'), task('t2', 'ghost')])).toThrow(DocOpError);

    const s = useDocumentStore.getState();
    expect(s.doc.tasks).toEqual([]);
    expect(s.past).toEqual([]);
    expect(s.revision).toBe(0);
  });

  it('revision advances on every mutation; userRevision only on user-sourced ones (SRS-ABR-020)', () => {
    const store = useDocumentStore.getState();

    store.applyBatch([task('t1')], 'agent');
    let s = useDocumentStore.getState();
    expect(s.revision).toBe(1);
    expect(s.userRevision).toBe(0);
    expect(s.lastAgentEditAt).not.toBeNull();

    // A normal store action is user-sourced by default.
    useDocumentStore.getState().setTitle('Renamed');
    s = useDocumentStore.getState();
    expect(s.revision).toBe(2);
    expect(s.userRevision).toBe(1);
  });

  it('exposes agent session state for the activity indicator (SRS-ABR-060)', () => {
    useDocumentStore.getState().setAgentActive(true);
    expect(useDocumentStore.getState().agentActive).toBe(true);
    useDocumentStore.getState().setAgentActive(false);
    expect(useDocumentStore.getState().agentActive).toBe(false);
  });
});
