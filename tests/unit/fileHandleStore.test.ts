import { describe, it, expect } from 'vitest';
import { isHandleForThisDocument } from '../../src/persistence/fileHandleStore';

/**
 * SRS-PERS-036 — a copy must not write the original.
 *
 * Once the binding survives a reload (SRS-PERS-035), a saved plan carries it:
 * duplicate `plan.html` to `plan (copy).html`, open the copy, press Ctrl+S,
 * and without this guard the bytes go to `plan.html`. Silently — a remembered
 * handle needs no picker — and irreversibly, since the original is gone.
 *
 * Only on `file:`. A served page has one file in play and no ambiguity.
 */

const served = { protocol: 'http:', pathname: '/' };
const onDisk = (pathname: string) => ({ protocol: 'file:', pathname });

describe('isHandleForThisDocument', () => {
  it('allows any handle on a served page', () => {
    expect(isHandleForThisDocument({ name: 'something-else.html' }, served)).toBe(true);
  });

  it('allows the handle naming this very file', () => {
    expect(isHandleForThisDocument({ name: 'plan.html' }, onDisk('/C:/plans/plan.html'))).toBe(true);
  });

  it('refuses a handle naming a different file', () => {
    const copy = onDisk('/C:/plans/plan%20(copy).html');
    // The copy may write itself; it may not write the file it was copied from.
    expect(isHandleForThisDocument({ name: 'plan (copy).html' }, copy)).toBe(true);
    expect(isHandleForThisDocument({ name: 'plan.html' }, copy)).toBe(false);
  });

  it('compares the decoded name, not the escaped path segment', () => {
    expect(
      isHandleForThisDocument({ name: 'q3 roadmap.html' }, onDisk('/C:/q3%20roadmap.html')),
    ).toBe(true);
  });

  it('keeps the raw segment when it is not valid escaping', () => {
    // `%zz` throws in decodeURIComponent; a throw here would block every save.
    expect(isHandleForThisDocument({ name: 'odd%zz.html' }, onDisk('/odd%zz.html'))).toBe(true);
  });
});
