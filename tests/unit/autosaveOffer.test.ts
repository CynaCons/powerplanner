import { describe, it, expect, beforeEach } from 'vitest';
import { readFileSync } from 'node:fs';
import { shouldAutosave, saveAutosave, loadAutosave, clearAutosave } from '../../src/persistence/autosave';
import type { GanttDocument } from '../../src/types/document';

/**
 * SRS-PERS-030 — the autosave does not overwrite the snapshot it is offering.
 *
 * SRS-PERS-020 offers the snapshot on next launch rather than applying it, and
 * the debounced write ran unconditionally. App's effect fires on mount with
 * whatever loaded — the sample, normally — so 800ms after every open the
 * sample was written over the backup. The banner still worked that one time,
 * because `loadAutosave` had already read the snapshot into state before the
 * overwrite; the NEXT reload had nothing left to offer.
 *
 * Observed red on d101e0b: a planner set the grid delimiter to Qtr and weekend
 * shading off, confirmed `"gridDelimiter":"quarter","weekendDisplay":"off"` in
 * `powerplanner:autosave:v1`, reloaded, and found the stored document back to
 * the sample's calendar with both keys gone. The validator is innocent —
 * `schema.ts` returns the parsed object untouched — and so is the write. The
 * seed had simply been saved over it.
 */

const doc = (title: string): GanttDocument =>
  ({
    schemaVersion: 1,
    title,
    calendar: { scale: 'month', fiscalYearStart: 1, workingDays: [1, 2, 3, 4, 5], holidays: [], gridDelimiter: 'quarter', weekendDisplay: 'off' },
    rows: [],
    tasks: [],
    milestones: [],
    brackets: [],
    dependencies: [],
    markers: [],
    style: { theme: 'light', preset: 'default' },
  }) as unknown as GanttDocument;

beforeEach(() => clearAutosave());

describe('an unanswered restore offer is not overwritten (SRS-PERS-030)', () => {
  it('refuses to write over the offer while nothing has been touched', () => {
    expect(shouldAutosave({ offerPending: true, dirty: false })).toBe(false);
  });

  it('writes once there is newer work to keep', () => {
    // Newer work wins: an edit made after the banner appeared is the thing
    // most worth protecting, and it is not in the snapshot.
    expect(shouldAutosave({ offerPending: true, dirty: true })).toBe(true);
  });

  it('writes normally once the offer has been answered', () => {
    // Both branches, so the rule cannot be satisfied by refusing every write.
    expect(shouldAutosave({ offerPending: false, dirty: false })).toBe(true);
    expect(shouldAutosave({ offerPending: false, dirty: true })).toBe(true);
  });

  it('survives the second open, which is the one that used to lose the work', () => {
    // The whole defect, played out against the real store.
    saveAutosave(doc('A day of work'));

    // First open: the banner reads the snapshot, then the seed's debounce fires.
    const firstOffer = loadAutosave();
    expect(firstOffer!.title).toBe('A day of work');
    if (shouldAutosave({ offerPending: firstOffer !== null, dirty: false })) saveAutosave(doc('Sample'));

    // Second open, with the banner still unanswered.
    const secondOffer = loadAutosave();
    expect(secondOffer).not.toBeNull();
    expect(secondOffer!.title).toBe('A day of work');
    expect(secondOffer!.calendar.gridDelimiter).toBe('quarter');
    expect(secondOffer!.calendar.weekendDisplay).toBe('off');
  });

  it('is what App actually consults', () => {
    // The rule is only worth stating if the effect asks it. Without this the
    // three cases above pass against an App that still writes unconditionally.
    const app = readFileSync('src/app/App.tsx', 'utf8');
    expect(app).toContain('shouldAutosave({ offerPending: pendingRestore !== null');
    expect(app).toMatch(/if \(!shouldAutosave\([^)]*\)\) return;[\s\S]{0,80}saveAutosave\(doc\)/);
  });
});
