import { describe, it, expect, beforeEach } from 'vitest';
import { useDocumentStore } from '../../src/stores/documentStore';
import { sampleDocument } from '../../src/utils/sample';

/**
 * SRS-EDIT-022 — REGRESSION tests, not gates.
 *
 * `transact`, `endFrame` and the keyed `update*` overloads did not exist
 * before the fix, so none of this could have been watched going red. The gate
 * is `tests/e2e/undo-granularity.spec.ts`, which drives the real dialog and
 * was red on e221b5f.
 *
 * What these pin down is the part a frame-count assertion in the browser
 * cannot see: that coalescing does not quietly corrupt redo, let an agent's
 * batch join a user's open frame, or outlive the gesture.
 */

const store = () => useDocumentStore.getState();

beforeEach(() => {
  store().setDocument(sampleDocument());
});

function firstTask() {
  return store().doc.tasks[0];
}

describe('gesture frames', () => {
  it('a keyed run of writes is one undo step, and undo takes the whole run', () => {
    const task = firstTask();
    const original = task.label;
    for (const label of ['A', 'AB', 'ABC', 'ABCD']) {
      store().updateTask(task.id, { label }, `task:${task.id}:label`);
    }
    expect(store().past).toHaveLength(1);

    store().undo();
    expect(store().doc.tasks[0].label).toBe(original);
  });

  it('leaving the field and coming back starts a new step', () => {
    const task = firstTask();
    store().updateTask(task.id, { label: 'A' }, `task:${task.id}:label`);
    store().endFrame();
    store().updateTask(task.id, { label: 'AB' }, `task:${task.id}:label`);
    expect(store().past).toHaveLength(2);
  });

  it('a different field is a different step, even on the same subject', () => {
    const task = firstTask();
    store().updateTask(task.id, { label: 'A' }, `task:${task.id}:label`);
    store().updateTask(task.id, { notes: 'n' }, `task:${task.id}:notes`);
    expect(store().past).toHaveLength(2);
  });

  it('an unkeyed write closes whatever frame was open', () => {
    const task = firstTask();
    store().updateTask(task.id, { label: 'A' }, `task:${task.id}:label`);
    store().setScale('week');
    store().updateTask(task.id, { label: 'AB' }, `task:${task.id}:label`);
    expect(store().past).toHaveLength(3);
  });

  it('an agent batch does not join the user’s open frame', () => {
    const task = firstTask();
    store().updateTask(task.id, { label: 'A' }, `task:${task.id}:label`);
    store().applyBatch([{ op: 'set_title', title: 'Agent turn' }], 'agent');
    expect(store().past).toHaveLength(2);

    // One Ctrl+Z takes back the agent's turn and leaves the user's rename.
    store().undo();
    expect(store().doc.title).not.toBe('Agent turn');
    expect(store().doc.tasks[0].label).toBe('A');
  });

  it('transact groups everything inside it, whatever the callee does', () => {
    const before = store().doc.tasks.length;
    const ids = store().doc.tasks.slice(0, 5).map((t) => t.id);
    store().transact(() => {
      for (const id of ids) store().deleteTask(id);
    });
    expect(store().past).toHaveLength(1);
    expect(store().doc.tasks).toHaveLength(before - 5);

    store().undo();
    expect(store().doc.tasks).toHaveLength(before);
  });

  it('a coalesced write does not resurrect redo', () => {
    const task = firstTask();
    store().updateTask(task.id, { label: 'first edit' });
    store().undo();
    expect(store().future).toHaveLength(1);

    // The first write of a new frame clears the redo stack...
    store().updateTask(task.id, { label: 'A' }, `task:${task.id}:label`);
    expect(store().future).toHaveLength(0);

    // ...and the ones that join it must leave it cleared, not restore it.
    store().updateTask(task.id, { label: 'AB' }, `task:${task.id}:label`);
    expect(store().future).toHaveLength(0);
  });

  it('undo never lands inside an open frame', () => {
    const task = firstTask();
    store().updateTask(task.id, { label: 'A' }, `task:${task.id}:label`);
    store().undo();
    // The next keystroke is a new step, not a join onto the frame just undone.
    store().updateTask(task.id, { label: 'B' }, `task:${task.id}:label`);
    const past = store().past.length;
    store().undo();
    expect(store().past).toHaveLength(past - 1);
  });

  it('the revision still moves per write, so the dirty flag and autosave see every edit', () => {
    const task = firstTask();
    const before = store().revision;
    for (const label of ['A', 'AB', 'ABC']) {
      store().updateTask(task.id, { label }, `task:${task.id}:label`);
    }
    expect(store().revision).toBe(before + 3);
    expect(store().past).toHaveLength(1);
  });
});
