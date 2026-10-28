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
