import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';
import { saveAutosave } from '../../src/persistence/autosave';
import { useNoticeStore, notify } from '../../src/stores/noticeStore';
import { sampleDocument } from '../../src/utils/sample';

/**
 * SRS-PERS-027 — a REGRESSION test, not a gate: `saveAutosave` returned
 * `void`, so "it reports failure" could not be expressed against the old code
 * at all. What the old code did is not in dispute — the catch block was empty
 * and commented "Ignore quota errors".
 */

const doc = sampleDocument();

beforeEach(() => {
  useNoticeStore.getState().clear();
});
afterEach(() => {
  vi.restoreAllMocks();
});

describe('autosave', () => {
  it('says when the snapshot did not land', () => {
    expect(saveAutosave(doc)).toBe(true);
    // `localStorage.setItem` lives on Storage.prototype in jsdom; assigning
    // to the instance does nothing.
    vi.spyOn(Storage.prototype, 'setItem').mockImplementation(() => {
      throw new DOMException('quota', 'QuotaExceededError');
    });
    expect(saveAutosave(doc)).toBe(false);
  });

  it('a repeat collapses into a count rather than flooding the region', () => {
    for (let i = 0; i < 5; i++) notify('No space for a local backup.', 'fail');
    const items = useNoticeStore.getState().items;
    expect(items).toHaveLength(1);
    expect(items[0].count).toBe(5);
  });

  it('a different message still gets its own notice', () => {
    notify('No space for a local backup.', 'fail');
    notify('Saved to plan.html.');
    expect(useNoticeStore.getState().items).toHaveLength(2);
  });
});
