import { describe, it, expect, beforeEach, vi } from 'vitest';
import { detachFile, getCurrentHandle } from '../../src/persistence/fileIo';
import { useDocumentStore } from '../../src/stores/documentStore';
import { sampleDocument } from '../../src/utils/sample';

/**
 * The rules that apply whenever the document is saved or replaced.
 *
 * Each of these is a defect the 2026-10-28 review confirmed, written as the
 * behaviour that was missing.
 */

function freshStore() {
  useDocumentStore.setState({
    doc: sampleDocument(),
    past: [],
    future: [],
    revision: 0,
    userRevision: 0,
    savedRevision: 0,
  });
}

describe('releasing the bound file (SRS-PERS-022)', () => {
  it('detachFile clears the save target', () => {
    detachFile();
    expect(getCurrentHandle()).toBeNull();
  });
});

describe('dirty tracking (SRS-EDIT-018, SRS-PERS-023, SRS-PERS-024)', () => {
  beforeEach(freshStore);

  const isDirty = () => {
    const { revision, savedRevision } = useDocumentStore.getState();
    return revision !== savedRevision;
  };

  it('a freshly opened document is clean', () => {
    expect(isDirty()).toBe(false);
  });

  it('an edit makes it dirty, and saving makes it clean again', () => {
    const t = useDocumentStore.getState().doc.tasks[0];
    useDocumentStore.getState().updateTask(t.id, { label: 'Renamed' });
    expect(isDirty()).toBe(true);
    useDocumentStore.getState().markSaved();
    expect(isDirty()).toBe(false);
  });

  it('a document that arrives whole — a file, an embed — starts clean', () => {
    useDocumentStore.getState().setDocument(sampleDocument());
    expect(isDirty()).toBe(false);
  });

  it('a RESTORED snapshot starts dirty, because it is in no file', () => {
    // replaceDocument is the restore path. setDocument would mark it clean and
    // the New and Open guards would then stay silent over recovered work.
    useDocumentStore.getState().replaceDocument(sampleDocument());
    expect(isDirty()).toBe(true);
  });

  it('an agent edit counts as unsaved work too', () => {
    const t = useDocumentStore.getState().doc.tasks[0];
    useDocumentStore.getState().applyBatch(
      [{ op: 'update_task', id: t.id, patch: { label: 'By an agent' } }],
      'agent',
    );
    expect(isDirty()).toBe(true);
  });

  it('a view-only change still counts, because it is stored in the document', () => {
    useDocumentStore.getState().setScale('quarter');
    expect(isDirty()).toBe(true);
  });
});

describe('saveCurrent marks clean only when bytes were written (SRS-PERS-023)', () => {
  beforeEach(freshStore);

  it('a cancelled picker leaves the document dirty', async () => {
    vi.resetModules();
    vi.doMock('../../src/persistence/fileIo', async () => {
      const actual = await vi.importActual<typeof import('../../src/persistence/fileIo')>(
        '../../src/persistence/fileIo',
      );
      return { ...actual, saveToFile: async () => ({ written: false }) };
    });
    const { saveCurrent } = await import('../../src/app/documentActions');
    const t = useDocumentStore.getState().doc.tasks[0];
    useDocumentStore.getState().updateTask(t.id, { label: 'Edited' });

    await saveCurrent();

    const { revision, savedRevision } = useDocumentStore.getState();
    expect(revision).not.toBe(savedRevision);
    vi.doUnmock('../../src/persistence/fileIo');
    vi.resetModules();
  });
});
