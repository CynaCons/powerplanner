import type { GanttDocument } from '../types/document';
import { validateDocument } from './schema';

const KEY = 'powerplanner:autosave:v1';

export function loadAutosave(): GanttDocument | null {
  try {
    const raw = localStorage.getItem(KEY);
    if (!raw) return null;
    return validateDocument(JSON.parse(raw));
  } catch {
    return null;
  }
}

/**
 * Whether the debounced snapshot is allowed to be written (SRS-PERS-030).
 *
 * The write used to be unconditional, and the effect that drives it runs on
 * mount with whatever loaded — the sample, normally. So every open rewrote the
 * backup with the sample 800ms later, before the planner had answered the
 * banner offering to restore it. The offer survived the reload that made it,
 * because `loadAutosave` had already read it into state; the second reload had
 * nothing left to offer. A day's work could be lost by opening the app twice
 * and not answering a banner.
 *
 * Newer work always wins: the moment the document is dirty there is something
 * to lose that the snapshot does not already hold, and saving resumes.
 */
export function shouldAutosave({ offerPending, dirty }: { offerPending: boolean; dirty: boolean }): boolean {
  return dirty || !offerPending;
}

/**
 * Returns false when the snapshot did not land. It used to swallow every
 * write error on a path that fires every 800ms, so a full localStorage meant
 * crash safety was gone and nothing on screen said so.
 */
export function saveAutosave(doc: GanttDocument): boolean {
  try {
    localStorage.setItem(KEY, JSON.stringify(doc));
    return true;
  } catch {
    return false;
  }
}

export function clearAutosave(): void {
  try {
    localStorage.removeItem(KEY);
  } catch {
    // ignore
  }
}
